import { useEffect, useRef, useState } from 'react';
import { ScrollView, View } from 'react-native';
import { KindleProvider, Text, Image } from 'react-kindle';
import { useApps, useExtension, useSetting } from '@kppmods/react';

// Stock KPP registers this native host; registering it again is an error.
const NativeButton = 'KPPUnifiedButton';

function AppTile({ app, onLaunch, columns, disabled }) {
  return (
    <NativeButton
      accessibilityRole="button"
      accessibilityLabel={app.title}
      disabled={disabled}
      type={0}
      active={false}
      onClicked={() => onLaunch(app)}
      style={{ ...styles.tile, width: `${100 / columns}%` }}
    >
      {app.icon ? (
        <Image source={app.icon} accessibilityLabel="" style={styles.icon} />
      ) : (
        <View style={styles.monogram}>
          <Text size={30} style={styles.initials}>
            {app.title
              .trim()
              .split(/\s+/)
              .slice(0, 2)
              .map((word) => word[0])
              .join('')
              .toUpperCase()}
          </Text>
        </View>
      )}
      <Text size={18} numberOfLines={2} style={styles.label}>
        {app.title}
      </Text>
    </NativeButton>
  );
}

export default function Launcher() {
  const extension = useExtension();
  const packageApps = useApps();
  const [installedApps, setInstalledApps] = useState([]);
  const [loading, setLoading] = useState(true);
  const [launching, setLaunching] = useState(false);
  const mounted = useRef(false);
  const apps = packageApps
    .concat(installedApps)
    .sort((a, b) => a.title.localeCompare(b.title));
  const [error, setError] = useState('');
  const [width, setWidth] = useState(0);
  const columns = Math.max(2, Math.min(6, Math.floor((width || 480) / 120)));

  function refresh() {
    setLoading(true);
    setError('');
    extension.management
      .apps()
      .then((installed) => {
        if (mounted.current) {
          setInstalledApps(installed);
          setLoading(false);
        }
      })
      .catch((error) => {
        if (mounted.current) {
          setError(String(error.message || error));
          setLoading(false);
        }
      });
  }
  useEffect(() => {
    mounted.current = true;
    refresh();
    return () => {
      mounted.current = false;
    };
  }, []);

  function launch(app) {
    if (launching) return;
    setError('');
    setLaunching(true);
    const pending = app.script
      ? extension.management.launchApp(app.script)
      : extension.execute(`${app.package}:${app.action}`, app.args);
    pending.then(
      () => {
        if (mounted.current) setLaunching(false);
      },
      (error) => {
        if (mounted.current) {
          setLaunching(false);
          setError(String(error.message || error));
        }
      }
    );
  }

  return (
    <KindleProvider firmware={extension.firmware} presentation="unified" fontScale={1}>
      <ScrollView style={styles.screen} contentContainerStyle={styles.content}>
        <View style={styles.header}>
          <Text variant="title" size={32}>
            {extension.options.title || 'Apps'}
          </Text>
          <NativeButton
            accessibilityRole="button"
            accessibilityLabel="Refresh apps"
            disabled={loading || launching}
            type={0}
            active={false}
            onClicked={refresh}
          >
            <View style={styles.refresh}>
              <Text size={18}>{loading ? 'Loading…' : 'Refresh'}</Text>
            </View>
          </NativeButton>
        </View>
        {error ? <Text style={{ marginBottom: 16 }}>{error}</Text> : null}
        {!loading && !apps.length && <Text>Install an app to add it here.</Text>}
        <View
          style={styles.grid}
          onLayout={(event) => setWidth(event.nativeEvent.layout.width)}
        >
          {apps.map((app) => (
            <AppTile
              key={app.script || `${app.package}:${app.id}`}
              app={app}
              onLaunch={launch}
              columns={columns}
              disabled={launching}
            />
          ))}
        </View>
      </ScrollView>
    </KindleProvider>
  );
}

function LauncherSettings({ NavigationRow, ToggleRow, PageContainer, Separator }) {
  const [replaceLibrary, setReplaceLibrary] = useSetting('replaceLibrary', false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');

  function select(value) {
    if (saving || value === replaceLibrary) return;
    setSaving(true);
    setError('');
    setReplaceLibrary(value).then(
      () => setSaving(false),
      (failure) => {
        setSaving(false);
        setError(String(failure.message || failure));
      }
    );
  }

  return (
    <PageContainer testID="settingsapp.kppmods.launcher">
      <ToggleRow
        widgetId="kppmods-launcher-add-tab"
        title="Add Apps tab"
        description="Keep Library and show Apps as an additional tab."
        currentState={!replaceLibrary}
        disabled={saving}
        onPress={() => select(false)}
      />
      <Separator />
      <ToggleRow
        widgetId="kppmods-launcher-replace-library"
        title="Replace Library"
        description="Show Apps in place of the stock Library view."
        currentState={replaceLibrary}
        disabled={saving}
        onPress={() => select(true)}
      />
      {error ? (
        <NavigationRow
          title="Could not save launcher location"
          description={error}
          isShowNavigationIcon={false}
        />
      ) : null}
    </PageContainer>
  );
}

export function activate(extension) {
  function updateTab() {
    extension.tabs.remove('apps');
    extension.tabs.add({
      id: 'apps',
      title: extension.options.title || 'Apps',
      component: Launcher,
      replace: extension.settings.get('replaceLibrary', false)
        ? 'KPP_LIBRARY'
        : undefined,
      order: 20
    });
  }
  updateTab();
  extension.settings.subscribe(({ key }) => {
    if (key === 'replaceLibrary') updateTab();
  });
  extension.settingsPages.add({
    id: 'preferences',
    title: 'App Launcher',
    parent: 'mod-manager:mods',
    component: LauncherSettings
  });
  (extension.options.hiddenTabs || []).forEach(extension.tabs.hide);
}

const styles = {
  screen: { flex: 1, backgroundColor: '#FFFFFF' },
  content: { padding: 24 },
  header: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    marginBottom: 24
  },
  refresh: { paddingHorizontal: 16, paddingVertical: 12 },
  grid: { flexDirection: 'row', flexWrap: 'wrap', alignItems: 'flex-start' },
  tile: {
    minHeight: 148,
    paddingHorizontal: 8,
    paddingVertical: 16,
    alignItems: 'center'
  },
  icon: { width: 72, height: 72, marginBottom: 12 },
  monogram: {
    width: 72,
    height: 72,
    marginBottom: 12,
    borderRadius: 17,
    borderWidth: 1,
    borderColor: '#888888',
    backgroundColor: '#FFFFFF',
    alignItems: 'center',
    justifyContent: 'center'
  },
  initials: { color: '#000000' },
  label: { textAlign: 'center', alignSelf: 'stretch' }
};
