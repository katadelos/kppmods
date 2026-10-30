const HIDDEN = 'hidden';
const REVEAL = 'Press to show';
const PRIVATE_FIELDS =
  /\.(serial_number|wifi_address|mac_address|bluetooth_address|device_policy_id|imei|imsi|iccid)(\.|$)/;

export function activate(extension) {
  const { React, RN } = extension;
  const capabilities = extension.capabilities();

  function patch(target, name, wrap) {
    const original = target[name];
    if (typeof original !== 'function')
      throw new Error('Invalid Antidox hook: ' + name);
    const replacement = wrap(original);
    target[name] = replacement;
    if (target[name] !== replacement) throw new Error('Cannot override ' + name);
    extension.onDispose(() => {
      if (target[name] === replacement) target[name] = original;
    });
  }

  function mapElements(element, visit) {
    if (Array.isArray(element))
      return element.map((child) => mapElements(child, visit));
    if (!React.isValidElement(element)) return element;
    const children = element.props.children;
    return visit(
      children === undefined
        ? element
        : React.cloneElement(element, {}, mapElements(children, visit))
    );
  }

  function PrivateValue({ element, value }) {
    const [revealed, setRevealed] = React.useState(null);
    const shown = revealed === value;
    const text = shown ? value : REVEAL;
    const { width, flexShrink, ...textStyle } =
      RN.StyleSheet.flatten(element.props.style) || {};
    return React.createElement(
      RN.View,
      { style: { width, flexShrink, alignItems: 'flex-end' } },
      React.createElement(
        'KPPUnifiedButton',
        {
          testID: element.props.testID + '.reveal',
          accessibilityRole: 'button',
          accessibilityLabel: shown ? String(value) + '. Press to hide' : REVEAL,
          type: 0,
          active: false,
          onClicked: () => setRevealed(shown ? null : value),
          style: {
            width: width === undefined ? undefined : '100%',
            maxWidth: '100%',
            alignItems: 'center',
            justifyContent: 'center',
            borderWidth: 1,
            borderColor: '#000000',
            borderRadius: 4,
            paddingHorizontal: 8,
            paddingVertical: 0
          }
        },
        React.cloneElement(
          element,
          { style: textStyle, accessible: false, accessibilityLabel: text },
          text
        )
      )
    );
  }

  const provider = RN.NativeModules.SettingsDataProvider;
  if (provider) {
    if (typeof provider.getUserName === 'function')
      patch(provider, 'getUserName', () => () => HIDDEN);
    if (typeof provider.setDeviceName === 'function')
      patch(
        provider,
        'setDeviceName',
        (original) =>
          function (value) {
            // The stock keyboard can submit even when the unchanged Save button is disabled.
            if (value !== HIDDEN) return original.call(this, value);
          }
      );
    if (typeof provider.getDeviceInfo === 'function')
      patch(
        provider,
        'getDeviceInfo',
        (original) =>
          function (...args) {
            // Redact the UI result without writing the device's saved name.
            return Object.assign({}, original.apply(this, args), {
              DeviceName: HIDDEN
            });
          }
      );
  }

  ['quickSettingsStatusBar', 'quickActionsHeader'].forEach((name) => {
    if (!capabilities.includes(name)) return;
    const component = extension.module(name).default;
    patch(
      component.prototype,
      'render',
      (original) =>
        function (...args) {
          return mapElements(original.apply(this, args), (element) => {
            const id = element.props.testID || '';
            if (
              /^(quicksettings\.statusbar|quickactions\.header)\.devicename$/.test(id)
            )
              return React.cloneElement(
                element,
                { accessibilityLabel: HIDDEN },
                HIDDEN
              );
            if (id === 'quickactions.header.avatar')
              return React.createElement(RN.View, {
                key: element.key,
                style: element.props.style,
                testID: id,
                accessible: false
              });
            return element;
          });
        }
    );
  });

  if (capabilities.includes('wifiSettings')) {
    const component = extension.module('wifiSettings').default;
    ['renderWifiAndBluetoothPageV1', 'renderWifiAndBluetoothPageV2'].forEach((name) => {
      patch(
        component.prototype,
        name,
        (original) =>
          function (...args) {
            return mapElements(original.apply(this, args), (element) => {
              const data = element.props.widgetData;
              if (data !== this.state.wifiWidgetData || !data?.deviceConnected)
                return element;
              // This row only displays the connection; callbacks retain the real network.
              return React.cloneElement(element, {
                widgetData: Object.assign({}, data, { deviceConnected: HIDDEN })
              });
            });
          }
      );
    });
  }

  if (capabilities.includes('wifiNetworks')) {
    const component = extension.module('wifiNetworks').default;
    patch(
      component.prototype,
      'render',
      (original) =>
        function (...args) {
          const view = original.apply(this, args);
          const ssid = this.isConnected && this.state.nList?.[0]?.essid;
          if (!ssid) return view;
          return mapElements(view, (element) => {
            const props = {};
            if (element.props.children === ssid) props.children = HIDDEN;
            // The network confirmation dialog uses this prop only for its text.
            if (element.props.ssid === ssid) props.ssid = HIDDEN;
            const label = element.props.accessibilityLabel;
            if (typeof label === 'string' && label.startsWith(ssid + ' '))
              props.accessibilityLabel = HIDDEN + label.slice(ssid.length);
            return Object.keys(props).length
              ? React.cloneElement(element, props)
              : element;
          });
        }
    );
  }

  if (capabilities.includes('deviceInfo')) {
    const component = extension.module('deviceInfo').default;
    patch(
      component.prototype,
      'renderDeviceInfoRow',
      (original) =>
        function (...args) {
          const id = args[1];
          if (!PRIVATE_FIELDS.test(id)) return original.apply(this, args);
          const value = args[3];
          args[3] = REVEAL;
          const row = original.apply(this, args);
          return React.cloneElement(
            mapElements(row, (element) =>
              element.props.testID === id + '.status'
                ? React.createElement(PrivateValue, { element, value, key: id })
                : element
            ),
            { accessible: false, accessibilityLabel: undefined }
          );
        }
    );
  }

  if (capabilities.includes('dialogLauncher')) {
    const launcher = extension.module('dialogLauncher').default;
    patch(
      launcher,
      'show',
      (original) =>
        function (options) {
          if (
            options.dialogId !== 'device-info' ||
            typeof options.Content !== 'function'
          )
            return original.call(this, options);
          const Content = options.Content;
          return original.call(
            this,
            Object.assign({}, options, {
              Content: function PrivateDeviceInfo(props) {
                return mapElements(Content(props), (element) => {
                  const id = element.props.testID || '';
                  return PRIVATE_FIELDS.test(id) && id.endsWith('.value')
                    ? React.createElement(PrivateValue, {
                        element,
                        value: element.props.children,
                        key: id
                      })
                    : element;
                });
              }
            })
          );
        }
    );
  }

  if (!capabilities.includes('deviceInfo'))
    extension.log(
      'Device names hidden in KPP; Java Settings uses the Antidox framework preload.'
    );
}
