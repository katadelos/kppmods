import React, { useEffect, useState } from 'react';
import ReactDOM from 'react-dom';
import {
  KindleProvider,
  SettingsRow,
  SettingsToggle,
  Divider,
  inspectCompatibility
} from 'react-kindle';
import Gallery from './gallery.jsx';
import { configureViewport, clickChrome } from './native.jsx';
import {
  initialize,
  entries,
  useRegistry,
  useEvents,
  subscribe,
  resolveIcon,
  runItem,
  navigate,
  resetSettings,
  setActionResult,
  reportError
} from './runtime.js';
import './style.css';

const profiles = {
  pw5: { title: 'Paperwhite 5', width: 632, height: 840, pixels: '1236 × 1648' },
  kt6: { title: 'Kindle 2024', width: 600, height: 810, pixels: '1072 × 1448' },
  scribe: { title: 'Kindle Scribe', width: 760, height: 1013, pixels: '1860 × 2480' }
};
const books = [
  { title: 'The Time Machine', author: 'H. G. Wells', initial: 'TM', pattern: 'lines' },
  {
    title: 'Pride and Prejudice',
    author: 'Jane Austen',
    initial: 'P&P',
    pattern: 'dots'
  },
  {
    title: 'The Adventures of Sherlock Holmes',
    author: 'Arthur Conan Doyle',
    initial: 'SH',
    pattern: 'dark'
  }
];
function Library() {
  return (
    <div className="library">
      <h2>Your library</h2>
      <p>Sample catalogue for local development</p>
      <div className="books">
        {books.map((book) => (
          <button
            key={book.title}
            onClick={() => navigate('app://reader/' + encodeURIComponent(book.title))}
          >
            <div className={'book-cover ' + book.pattern}>
              <span>{book.initial}</span>
            </div>
            <strong>{book.title}</strong>
            <small>{book.author}</small>
          </button>
        ))}
      </div>
    </div>
  );
}
class ErrorBoundary extends React.Component {
  state = { error: null };
  static getDerivedStateFromError(error) {
    return { error };
  }
  componentDidCatch(error) {
    reportError(this.props.owner || 'preview', error);
  }
  render() {
    return this.state.error ? (
      <div className="render-error">
        <h2>Component could not render</h2>
        <pre>{this.state.error.message}</pre>
        <p>Details are in the event inspector.</p>
      </div>
    ) : (
      this.props.children
    );
  }
}
function RegisteredSlider({ item }) {
  const [value, update] = useState(() => item.get?.() ?? item.min);
  useEffect(() => item.subscribe?.(() => update(item.get?.() ?? item.min)), [item]);
  return (
    <label className="quick-slider">
      <span>
        {item.title}
        <span className="current-value" aria-hidden="true">
          {value}
        </span>
      </span>
      <input
        type="range"
        min={item.min}
        max={item.max}
        value={value}
        onChange={(event) => {
          const next = Number(event.target.value);
          update(next);
          try {
            if (item.set)
              Promise.resolve(item.set(next)).catch((error) =>
                reportError(item.owner, error)
              );
            else runItem(item, next);
          } catch (error) {
            reportError(item.owner, error);
          }
        }}
      />
    </label>
  );
}
function QuickSettings({ close }) {
  const buttons = useRegistry('quickSettings'),
    sliders = useRegistry('sliders');
  const [light, setLight] = useState(14),
    [wifi, setWifi] = useState(true);
  return (
    <div className="quick-panel">
      <div className="quick-title">
        <h2>Quick settings</h2>
        <button onClick={close} aria-label="Close quick settings">
          ×
        </button>
      </div>
      <div className="quick-buttons">
        <button onClick={() => setWifi(!wifi)}>
          <span className="quick-symbol">⌁</span>Wi-Fi {wifi ? 'on' : 'off'}
        </button>
        <button onClick={() => navigate('KPP_SETTINGS')}>
          <span className="quick-symbol">⚙</span>All settings
        </button>
        {buttons.map((item) => (
          <button
            key={item.id}
            onClick={() => {
              runItem(item);
              if (item.close !== false) close();
            }}
          >
            {item.icon && <img src={resolveIcon(item.icon)} alt="" />}
            {item.title}
          </button>
        ))}
      </div>
      <label className="quick-slider">
        <span>
          Brightness{' '}
          <span className="current-value" aria-hidden="true">
            {light}
          </span>
        </span>
        <input
          type="range"
          min="0"
          max="24"
          value={light}
          onChange={(e) => setLight(Number(e.target.value))}
        />
      </label>
      {sliders.map((item) => (
        <RegisteredSlider key={item.id} item={item} />
      ))}
      {!sliders.length && (
        <p className="quiet">Package sliders appear here when registered.</p>
      )}
    </div>
  );
}
function Inspector({ view }) {
  const events = useEvents();
  const capabilities = inspectCompatibility(
    localStorage.getItem('kppmods:preview:firmware') || '5.18.6'
  );
  if (view === 'capabilities')
    return (
      <div className="capabilities">
        <p>Browser adapter contracts. These do not certify firmware support.</p>
        {Object.entries(capabilities.features).map(([name, value]) => (
          <div key={name}>
            <span>{name}</span>
            <span
              className={value.available ? 'available' : 'missing'}
              title={value.reason || 'Browser implementation'}
            >
              {value.available ? 'Simulated' : 'Unavailable'}
            </span>
          </div>
        ))}
      </div>
    );
  if (view === 'packages')
    return (
      <div className="package-list">
        {entries.map(({ manifest }) => (
          <div key={manifest.id}>
            <strong>{manifest.title || manifest.id}</strong>
            <span>
              {manifest.id} · {manifest.version}
            </span>
            <small>
              {manifest.ui?.length ? 'React extension' : 'Manifest / native only'}
            </small>
          </div>
        ))}
      </div>
    );
  return (
    <div className="events">
      {events
        .slice()
        .reverse()
        .map((item) => (
          <details
            key={item.seq}
            className={item.name === 'ui:error' ? 'event-error' : ''}
          >
            <summary>
              <span>
                {item.name}
                <small>{item.package}</small>
              </span>
              <time>{item.time}</time>
            </summary>
            <pre>{JSON.stringify(item.data, null, 2)}</pre>
          </details>
        ))}
    </div>
  );
}
function PreviewNavigationRow({ title, description, onPress, disabled }) {
  return (
    <button
      onClick={onPress}
      disabled={disabled}
      style={{ display: 'block', width: '100%', textAlign: 'left' }}
    >
      <SettingsRow title={title} description={description} />
    </button>
  );
}
function PreviewToggleRow({ title, description, currentState, onPress, disabled }) {
  return (
    <SettingsRow title={title} description={description}>
      <SettingsToggle
        value={currentState}
        onValueChange={onPress}
        disabled={disabled}
      />
    </SettingsRow>
  );
}
function PreviewPageContainer({ children }) {
  return (
    <div style={{ overflow: 'auto', height: '100%', padding: 16 }}>{children}</div>
  );
}
function Workbench() {
  const [profile, setProfile] = useState(
    localStorage.getItem('kppmods:preview:profile') || __PREVIEW_PROFILE__
  );
  const [route, setRoute] = useState(
    sessionStorage.getItem('kppmods:preview:route') || 'gallery'
  );
  const [panel, setPanel] = useState(false),
    [inspect, setInspect] = useState('events');
  const [chrome, setChrome] = useState(null);
  const [zoom, setZoom] = useState(0.8);
  const firmware = localStorage.getItem('kppmods:preview:firmware') || '5.18.6';
  const size = profiles[profile] || profiles.pw5;
  const tabs = useRegistry('tabs'),
    homes = useRegistry('home'),
    menuItems = useRegistry('menuItems').filter((item) => !item.hide),
    settingsPages = useRegistry('settingsPages').filter((item) => !item.hide),
    status = useRegistry('status');
  useEffect(
    () =>
      subscribe('navigate', (next) => {
        setRoute(next);
        setPanel(next === 'settings');
      }),
    []
  );
  useEffect(() => subscribe('chrome', setChrome), []);
  useEffect(() => {
    configureViewport({ width: size.width, height: size.height - 94 });
  }, [size]);
  useEffect(() => {
    sessionStorage.setItem('kppmods:preview:route', route);
  }, [route]);
  const changeProfile = (value) => {
    localStorage.setItem('kppmods:preview:profile', value);
    setProfile(value);
  };
  const custom = tabs.find((item) => item.id === route);
  const libraryReplacement = tabs.find((item) => item.replace === 'KPP_LIBRARY');
  const visibleTabs = tabs.filter(
    (item) => !item.hide && !item.replace && item.footer !== false
  );
  function content() {
    const settingsPage = settingsPages.find((item) => route === item.id);
    if (settingsPage)
      return (
        <PreviewPageContainer>
          <PreviewNavigationRow
            title={
              'Back to ' +
              (settingsPages.find((item) => item.id === settingsPage.parent)?.title ||
                'Settings')
            }
            onPress={() => setRoute(settingsPage.parent || 'settings')}
          />
          <settingsPage.component
            NavigationRow={PreviewNavigationRow}
            ToggleRow={PreviewToggleRow}
            PageContainer={React.Fragment}
            Separator={Divider}
            navigateToPage={setRoute}
          />
        </PreviewPageContainer>
      );
    if (route === 'gallery') return <Gallery />;
    if (route === 'home' && homes.length)
      return homes.map((item) => <item.component key={item.id} />);
    if (custom) return <custom.component />;
    if (route === 'library' && libraryReplacement)
      return <libraryReplacement.component />;
    if (route === 'home' || route === 'library') return <Library />;
    if (route === 'settings')
      return (
        <div className="library">
          <h2>Device settings</h2>
          {settingsPages
            .filter((item) => !item.parent)
            .map((item) => (
              <button key={item.id} onClick={() => setRoute(item.id)}>
                {item.title}
              </button>
            ))}
          <p>System services are simulated locally.</p>
          <button onClick={() => setPanel(true)}>Open quick settings</button>
        </div>
      );
    return (
      <div className="library">
        <h2>Navigation captured</h2>
        <p>
          The requested destination belongs to a device app. See its URI in the event
          inspector.
        </p>
        <button onClick={() => setRoute('home')}>Back to Home</button>
      </div>
    );
  }
  return (
    <div className="workbench">
      <header className="toolbar">
        <div className="brand">
          <span className="brand-icon">▦</span>
          <strong>KPPMods</strong>
          <span>Workbench</span>
        </div>
        <span className="local-label">
          <i /> Local preview
        </span>
      </header>
      <main className="workspace">
        <aside className="sidebar">
          <h1>
            Your mod,
            <br />
            on the page.
          </h1>
          <p className="intro">
            Develop the real components.
            <br />
            Keep the device for the final check.
          </p>
          <label className="field">
            Device layout
            <select value={profile} onChange={(e) => changeProfile(e.target.value)}>
              {Object.entries(profiles).map(([id, value]) => (
                <option key={id} value={id}>
                  {value.title}
                </option>
              ))}
            </select>
          </label>
          <label className="field">
            Toolkit firmware style
            <select
              value={firmware}
              onChange={(e) => {
                localStorage.setItem('kppmods:preview:firmware', e.target.value);
                location.reload();
              }}
            >
              <option>5.16.2.1.1</option>
              <option>5.18.6</option>
              <option>5.20.1</option>
            </select>
          </label>
          <label className="field">
            Preview scale{' '}
            <span className="current-value" aria-hidden="true">
              {Math.round(zoom * 100)}%
            </span>
            <input
              type="range"
              min="0.45"
              max="1"
              step="0.05"
              value={zoom}
              onChange={(e) => setZoom(Number(e.target.value))}
            />
          </label>
          <div className="separator" />
          <h2>Surfaces</h2>
          <nav className="surface-nav">
            <button
              className={route === 'gallery' ? 'active' : ''}
              onClick={() => {
                setRoute('gallery');
                setPanel(false);
              }}
            >
              ◫ <span>Toolkit gallery</span>
            </button>
            <button
              className={route === 'home' ? 'active' : ''}
              onClick={() => {
                setRoute('home');
                setPanel(false);
              }}
            >
              ⌂ <span>Home replacement</span>
            </button>
            {visibleTabs.map((item) => (
              <button
                key={item.id}
                className={route === item.id ? 'active' : ''}
                onClick={() => {
                  setRoute(item.id);
                  setPanel(false);
                }}
              >
                □ <span>{item.title}</span>
              </button>
            ))}
            {menuItems.map((item) => (
              <button
                key={item.id}
                onClick={() => (item.uri ? navigate(item.uri) : runItem(item))}
              >
                ⋮ <span>{item.title}</span>
              </button>
            ))}
            <button className={panel ? 'active' : ''} onClick={() => setPanel(!panel)}>
              ☷ <span>Pull-down settings</span>
            </button>
          </nav>
          <div className="separator" />
          <label className="field">
            Device action result
            <select onChange={(e) => setActionResult(e.target.value)}>
              <option value="success">Simulate success</option>
              <option value="failure">Simulate failure</option>
              <option value="timeout">Simulate timeout</option>
            </select>
          </label>
          <button className="reset-settings" onClick={resetSettings}>
            Reset saved settings
          </button>
          <p className="footnote">
            JSX saves reload the preview. Settings stay local. Native commands are
            recorded, never executed.
          </p>
        </aside>
        <section className="stage">
          <div className="stage-caption">
            <strong>{size.title}</strong>
            <span>{size.pixels} panel · approximate layout</span>
          </div>
          <div
            className="device-space"
            style={{
              width: (size.width + 32) * zoom,
              height: (size.height + 76) * zoom
            }}
          >
            <div
              className="device"
              style={{ width: size.width + 32, transform: `scale(${zoom})` }}
            >
              <div
                className="screen"
                style={{ width: size.width, height: size.height }}
              >
                <div className="status-bar">
                  <span>9:41</span>
                  <div>
                    {status.map((item) => (
                      <button
                        key={item.id}
                        title={item.title || item.id}
                        onClick={() => navigate(item.uri)}
                      >
                        <img src={resolveIcon(item.icon)} alt={item.title || item.id} />
                      </button>
                    ))}
                    <span aria-label="Wi-Fi">⌁</span>
                    <span className="battery">▰</span>
                    <button
                      aria-label="Open quick settings"
                      onClick={() => setPanel(!panel)}
                    >
                      ⌄
                    </button>
                  </div>
                </div>
                {chrome && (
                  <div className="chrome-preview">
                    <strong>{chrome.topNavBar?.title}</strong>
                    {chrome.topNavBar?.buttons?.map((button) => (
                      <button key={button.id} onClick={() => clickChrome(button.id)}>
                        {button.label || button.id}
                      </button>
                    ))}
                  </div>
                )}
                <div className="screen-content">
                  <KindleProvider
                    firmware={firmware}
                    applicationId="com.lab126.KPPMainApp"
                    fontScale={1}
                  >
                    <ErrorBoundary
                      key={route + firmware}
                      owner={
                        custom?.owner ||
                        (route === 'library' && libraryReplacement?.owner) ||
                        route
                      }
                    >
                      {content()}
                    </ErrorBoundary>
                  </KindleProvider>
                </div>
                <footer className="kindle-tabs">
                  {!tabs.some(
                    (item) => item.hide && item.stockId === 'chrome.home.button'
                  ) && (
                    <button
                      className={route === 'home' ? 'selected' : ''}
                      onClick={() => setRoute('home')}
                    >
                      Home
                    </button>
                  )}
                  {!tabs.some(
                    (item) => item.hide && item.stockId === 'chrome.library.button'
                  ) && (
                    <button
                      className={route === 'library' ? 'selected' : ''}
                      onClick={() => setRoute('library')}
                    >
                      {libraryReplacement?.title || 'Library'}
                    </button>
                  )}
                  {visibleTabs.map((item) => (
                    <button
                      key={item.id}
                      className={route === item.id ? 'selected' : ''}
                      onClick={() => setRoute(item.id)}
                    >
                      {item.title}
                    </button>
                  ))}
                </footer>
                {panel && (
                  <>
                    <button
                      className="quick-scrim"
                      aria-label="Dismiss settings"
                      onClick={() => setPanel(false)}
                    />
                    <QuickSettings close={() => setPanel(false)} />
                  </>
                )}
              </div>
              <div className="device-mark">kindle</div>
            </div>
          </div>
          <p className="stage-note">
            Browser rendering · native text metrics and e-ink refresh may differ
          </p>
        </section>
        <aside className="inspector">
          <div className="inspector-title">
            <h2>Inspector</h2>
            <span>LIVE</span>
          </div>
          <div className="inspector-tabs">
            {['events', 'packages', 'capabilities'].map((name) => (
              <button
                key={name}
                className={inspect === name ? 'active' : ''}
                onClick={() => setInspect(name)}
              >
                {name}
              </button>
            ))}
          </div>
          <Inspector view={inspect} />
        </aside>
      </main>
    </div>
  );
}
export function start(packageEntries) {
  const profile =
    profiles[localStorage.getItem('kppmods:preview:profile') || __PREVIEW_PROFILE__] ||
    profiles.pw5;
  configureViewport({ width: profile.width, height: profile.height - 94 });
  initialize(
    packageEntries,
    localStorage.getItem('kppmods:preview:firmware') || '5.18.6'
  );
  ReactDOM.render(<Workbench />, document.getElementById('root'));
}
