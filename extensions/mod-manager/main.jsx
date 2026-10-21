import React, { useCallback, useEffect, useState } from 'react';
import { useExtension } from '@kppmods/react';
import { ScrollView, View } from 'react-native';
import { Button, KindleProvider, SettingsRow, SettingsToggle } from 'react-kindle';

function NavigationRow({ title, description, onPress, disabled }) {
  return onPress ? (
    <Button variant="secondary" disabled={disabled} onPress={onPress}>
      {title}
    </Button>
  ) : (
    <SettingsRow title={title} description={description} />
  );
}

function ToggleRow({ title, description, currentState, disabled, onPress }) {
  return (
    <SettingsRow title={title} description={description}>
      <SettingsToggle
        value={currentState}
        disabled={disabled}
        onValueChange={onPress}
      />
    </SettingsRow>
  );
}

function PageContainer({ children, ...props }) {
  const extension = useExtension();
  return (
    <KindleProvider firmware={extension.firmware} fontScale={1}>
      <ScrollView
        {...props}
        style={{ flex: 1, backgroundColor: '#FFFFFF' }}
        contentContainerStyle={{ padding: 24 }}
      >
        {children}
      </ScrollView>
    </KindleProvider>
  );
}

function Separator() {
  return <View style={{ height: 8 }} />;
}

function ManagerView() {
  return (
    <ExtensionManager {...{ NavigationRow, ToggleRow, PageContainer, Separator }} />
  );
}

export default function ExtensionManager({
  NavigationRow,
  ToggleRow,
  PageContainer,
  Separator
}) {
  const extension = useExtension();
  const [packages, setPackages] = useState([]);
  const [draft, setDraft] = useState({});
  const [busy, setBusy] = useState(true);
  const [error, setError] = useState('');
  const refresh = useCallback(
    () =>
      extension.management
        .list()
        .then((items) => {
          setPackages(items);
          setDraft(
            items.reduce((values, pkg) => ({ ...values, [pkg.id]: pkg.enabled }), {})
          );
        })
        .catch((failure) => setError(failure.message))
        .then(() => setBusy(false)),
    [extension]
  );
  useEffect(() => {
    refresh();
  }, [refresh]);
  const pending = packages.filter((pkg) => draft[pkg.id] !== pkg.enabled).length;
  const optionalPackages = packages.filter(
    (pkg) => pkg.id !== 'ui-host' && pkg.id !== 'mod-manager'
  );
  function toggle(pkg) {
    if (busy || !pkg.available) return;
    setError('');
    setDraft((previous) => {
      const next = { ...previous },
        visited = new Set(),
        enabled = !previous[pkg.id];
      function change(id) {
        if (visited.has(id)) return;
        visited.add(id);
        next[id] = enabled;
        if (enabled) packages.find((item) => item.id === id).needs.forEach(change);
        else
          packages
            .filter((item) => item.needs.includes(id))
            .forEach((item) => change(item.id));
      }
      change(pkg.id);
      return next;
    });
  }
  function apply() {
    if (busy || !pending) return;
    setBusy(true);
    setError('');
    extension.management
      .apply(packages.filter((pkg) => !draft[pkg.id]).map((pkg) => pkg.id))
      .then(refresh)
      .catch((failure) => {
        setError(failure.message);
        setBusy(false);
      });
  }
  function discard() {
    setDraft(
      packages.reduce((values, pkg) => ({ ...values, [pkg.id]: pkg.enabled }), {})
    );
    setError('');
  }
  const message =
    error ||
    (busy
      ? 'Applying or loading extensions…'
      : pending
        ? pending + ' pending changes. Apply reloads the interface once.'
        : 'Choose extensions, then Apply. Required and dependent extensions change together.');
  return (
    <PageContainer testID="settingsapp.kppmods">
      <NavigationRow
        title="Extensions"
        description={message}
        isShowNavigationIcon={false}
      />
      <NavigationRow
        title="Apply"
        isShowNavigationIcon={false}
        disabled={busy || !pending}
        onPress={apply}
      />
      <NavigationRow
        title="Discard changes"
        isShowNavigationIcon={false}
        disabled={busy || !pending}
        onPress={discard}
      />
      <Separator />
      {optionalPackages.map((pkg) => (
        <React.Fragment key={pkg.id}>
          <ToggleRow
            widgetId={'kppmods-' + pkg.id}
            title={pkg.title}
            description={pkg.available ? '' : pkg.unavailableReason}
            currentState={!!draft[pkg.id]}
            disabled={busy || !pkg.available}
            onPress={() => toggle(pkg)}
          />
          <Separator />
        </React.Fragment>
      ))}
    </PageContainer>
  );
}
export function activate(extension) {
  const hasSettings = [
    'settingsRoot',
    'settingsNavigationRow',
    'settingsSwitchRow',
    'settingsSeparator'
  ].every((name) => extension.capabilities().includes(name));
  if (hasSettings) {
    extension.settingsPages.add({
      id: 'mods',
      title: 'Extensions',
      component: ExtensionManager,
      order: 0
    });
    return;
  }
  const id = extension.tabs.add({
    id: 'mods',
    title: 'Extensions',
    component: ManagerView,
    footer: false
  });
  extension.menuItems.add({
    id: 'mods',
    title: 'Extensions',
    uri:
      'app://com.lab126.KPPMainApp/?view=KPP_MOD_' +
      id.replace(/[^a-zA-Z0-9]/g, '_').toUpperCase(),
    order: 0
  });
}
