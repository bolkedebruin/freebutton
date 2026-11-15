import { describe, it, expect, afterEach, vi } from 'vitest';

// Import all components to register custom elements
import '../fb-app.js';
import '../fb-navbar.js';
import '../fb-nav-item.js';
import '../fb-resize-handle.js';
import '../fb-editor-pane.js';
import '../fb-toolbar.js';
import '../fb-code-editor.js';
import '../fb-status-bar.js';
import '../fb-right-pane.js';
import '../fb-virtual-device.js';
import '../fb-tft-display.js';
import '../fb-button-grid.js';
import '../fb-console-pane.js';
import '../fb-console-output.js';
import '../fb-console-input.js';
import '../fb-file-drawer.js';
import type { WorkspaceSnapshot } from '../../js/app-state.js';
import { LOCAL_MESSAGE, workspace } from '../../js/workspace.js';
import { loadDefaultConfig, setPreviewConfig } from '../../js/device-config.js';
import { configToModel, mapImage, mapList } from '../../js/config-model.js';
import { setIconSources, _clearIconSources } from '../../js/icon-sync.js';

function createElement<T extends HTMLElement>(tag: string): T {
  const el = document.createElement(tag) as T;
  document.body.appendChild(el);
  return el;
}

async function nextFrame(): Promise<void> {
  await new Promise(r => setTimeout(r, 0));
}

afterEach(() => {
  document.body.innerHTML = '';
});

describe('Component Registration', () => {
  const tags = [
    'fb-app', 'fb-navbar', 'fb-nav-item', 'fb-resize-handle',
    'fb-editor-pane', 'fb-toolbar', 'fb-code-editor', 'fb-status-bar',
    'fb-right-pane', 'fb-virtual-device', 'fb-tft-display', 'fb-button-grid',
    'fb-console-pane', 'fb-console-output', 'fb-console-input', 'fb-file-drawer',
  ];

  for (const tag of tags) {
    it(`${tag} is registered as a custom element`, () => {
      expect(customElements.get(tag)).toBeDefined();
    });
  }
});

describe('fb-navbar', () => {
  it('renders nav items for App, Images, Fonts, Examples, Device, Docs, Settings', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();

    const navItems = el.querySelectorAll('fb-nav-item');
    const labels = Array.from(navItems).map(item => item.getAttribute('label'));

    expect(labels).not.toContain('Code');
    for (const l of ['App', 'Images', 'Fonts', 'Docs']) expect(labels).toContain(l);
    expect(labels).not.toContain('Assets');
    expect(labels).toContain('Examples');
    expect(labels).toContain('Device');
    expect(labels).toContain('Settings');
  });

  it('App returns to the editor with the file list from another view', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();
    window.dispatchEvent(new CustomEvent('layout-change', { detail: { view: 'device', panel: null } }));
    await nextFrame();
    const got: unknown[] = [];
    const onView = (e: Event) => got.push((e as CustomEvent).detail);
    window.addEventListener('main-view', onView);
    window.addEventListener('sidebar-panel', onView);
    (Array.from(el.querySelectorAll<HTMLElement>('fb-nav-item')).find(i => i.getAttribute('label') === 'App') as HTMLElement).click();
    window.removeEventListener('main-view', onView);
    window.removeEventListener('sidebar-panel', onView);
    expect(got).toEqual([{ view: 'editor' }, { panel: 'app', open: true }]);
  });

  it('Docs opens the Docs view, Fonts opens the panel and the Fonts view', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();
    const got: string[] = [];
    const onView = (e: Event) => got.push('view:' + (e as CustomEvent).detail.view);
    const onPanel = (e: Event) => got.push('panel:' + (e as CustomEvent).detail.panel);
    window.addEventListener('main-view', onView);
    window.addEventListener('sidebar-panel', onPanel);
    const item = (label: string) => Array.from(el.querySelectorAll<HTMLElement>('fb-nav-item')).find(i => i.getAttribute('label') === label) as HTMLElement;
    item('Docs').click();
    item('Fonts').click();
    window.removeEventListener('main-view', onView);
    window.removeEventListener('sidebar-panel', onPanel);
    expect(got).toEqual(['view:docs', 'view:fonts', 'panel:fonts']);

    got.length = 0;
    window.addEventListener('main-view', onView);
    window.addEventListener('sidebar-panel', onPanel);
    item('Images').click();
    item('Examples').click();
    window.removeEventListener('main-view', onView);
    window.removeEventListener('sidebar-panel', onPanel);
    expect(got).toEqual(['view:image', 'panel:images', 'view:example', 'panel:examples']);
  });

  it('every entry is a button, and Device opens the Device view', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();
    const items = Array.from(el.querySelectorAll('fb-nav-item'));
    expect(items.every(i => i.querySelector('button') !== null)).toBe(true);

    const views: CustomEvent[] = [];
    const handler = (e: Event) => views.push(e as CustomEvent);
    window.addEventListener('main-view', handler);
    (items.find(i => i.getAttribute('label') === 'Device') as HTMLElement).click();
    window.removeEventListener('main-view', handler);
    expect(views.map(v => v.detail.view)).toEqual(['device']);
  });

  it('shows what is open (layout-change)', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();
    window.dispatchEvent(new CustomEvent('layout-change', { detail: { view: 'device', panel: 'examples' } }));
    await nextFrame();
    await nextFrame();
    const pressed = Array.from(el.querySelectorAll('fb-nav-item'))
      .filter(i => i.querySelector('button')?.getAttribute('aria-pressed') === 'true')
      .map(i => i.getAttribute('label'));
    expect(pressed).toEqual(['Examples', 'Device']);
  });

  it('has fixed width of 100px', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();

    const nav = el.querySelector('nav');
    expect(nav?.classList.contains('w-[100px]')).toBe(true);
  });

  it('does not expand on hover', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();

    const nav = el.querySelector('nav');
    expect(nav?.classList.contains('hover:w-[264px]')).toBe(false);
  });
});

describe('fb-app keyboard shortcuts', () => {
  it('Ctrl+S dispatches save-request', async () => {
    createElement('fb-app');
    await nextFrame();
    let fired = 0;
    const handler = () => { fired++; };
    window.addEventListener('save-request', handler);
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 's', ctrlKey: true, bubbles: true, cancelable: true }));
    window.removeEventListener('save-request', handler);
    expect(fired).toBe(1);
  });

  it('prevents default on Ctrl+S', async () => {
    createElement('fb-app');
    await nextFrame();

    const event = new KeyboardEvent('keydown', { key: 's', ctrlKey: true, bubbles: true, cancelable: true });
    window.dispatchEvent(event);

    expect(event.defaultPrevented).toBe(true);
  });

  it('Ctrl+L dispatches console-clear', async () => {
    createElement('fb-app');
    await nextFrame();

    const events: Event[] = [];
    const handler = (e: Event) => events.push(e);
    window.addEventListener('console-clear', handler);

    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'l', ctrlKey: true, bubbles: true, cancelable: true }));
    await nextFrame();

    window.removeEventListener('console-clear', handler);
    expect(events.length).toBe(1);
  });
});

describe('fb-navbar sidebar-panel events', () => {
  it('App click dispatches sidebar-panel with app', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();

    const events: CustomEvent[] = [];
    const handler = (e: Event) => { events.push(e as CustomEvent); };
    window.addEventListener('sidebar-panel', handler);

    const assetsItem = Array.from(el.querySelectorAll<HTMLElement>('fb-nav-item')).find(
      item => item.getAttribute('label') === 'App'
    );
    assetsItem!.click();
    await nextFrame();

    window.removeEventListener('sidebar-panel', handler);
    expect(events.length).toBe(1);
    expect(events[0].detail.panel).toBe('app');
  });

  it('Examples click dispatches sidebar-panel with examples', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();

    const events: CustomEvent[] = [];
    const handler = (e: Event) => { events.push(e as CustomEvent); };
    window.addEventListener('sidebar-panel', handler);

    const examplesItem = Array.from(el.querySelectorAll<HTMLElement>('fb-nav-item')).find(
      item => item.getAttribute('label') === 'Examples'
    );
    examplesItem!.click();
    await nextFrame();

    window.removeEventListener('sidebar-panel', handler);
    expect(events.length).toBe(1);
    expect(events[0].detail.panel).toBe('examples');
  });
});

describe('fb-toolbar', () => {
  it('has two named actions, Save and Deploy, and no Compile or Validate', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();

    expect(el.querySelector('[data-testid="toolbar-save"]')?.textContent).toContain('Save');
    expect(el.querySelector('[data-testid="toolbar-deploy"]')?.textContent).toContain('Deploy');
    const titles = Array.from(el.querySelectorAll('button')).map(b => b.getAttribute('title') ?? '');
    expect(titles.some(t => /^(Compile|Validate)$/.test(t))).toBe(false);
  });

  it('shows the file actions only while the editor is the main view', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();
    const ids = ['toolbar-save', 'toolbar-deploy', 'toolbar-more'];
    const shown = () => ids.filter(id => el.querySelector(`[data-testid="${id}"]`));
    expect(shown()).toEqual(ids);
    window.dispatchEvent(new CustomEvent('layout-change', { detail: { view: 'device', panel: 'app' } }));
    await nextFrame();
    expect(shown()).toEqual([]);
    expect(el.querySelector('[data-testid="connect-button"]')).not.toBeNull();
    window.dispatchEvent(new CustomEvent('layout-change', { detail: { view: 'editor', panel: 'app' } }));
    await nextFrame();
    expect(shown()).toEqual(ids);
  });

  it('shows Push images only with the Images panel or an image open', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();
    const push = () => el.querySelector('[data-testid="toolbar-push-images"]') as HTMLButtonElement | null;
    expect(push()).toBeNull();
    window.dispatchEvent(new CustomEvent('layout-change', { detail: { view: 'editor', panel: 'images' } }));
    await nextFrame();
    expect(push()?.textContent).toContain('Push images');
    expect(push()!.disabled).toBe(true); // no device in the test
    window.dispatchEvent(new CustomEvent('layout-change', { detail: { view: 'image', panel: null } }));
    await nextFrame();
    expect(push()).not.toBeNull();
    window.dispatchEvent(new CustomEvent('layout-change', { detail: { view: 'editor', panel: 'app' } }));
    await nextFrame();
    expect(push()).toBeNull();
  });

  it('Deploy is for scripts/main.js only and says so', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();
    const snap = (currentKey: string) => ({ mode: 'device', message: '', files: [], busy: false, currentKey, dirty: false, isNew: false });
    window.dispatchEvent(new CustomEvent('workspace-change', { detail: snap('config.json') }));
    await nextFrame();
    const deploy = el.querySelector('[data-testid="toolbar-deploy"]') as HTMLButtonElement;
    expect(deploy.disabled).toBe(true);
    expect(el.querySelector('[data-testid="deploy-hint"]')?.textContent).toContain('scripts/main.js');

    window.dispatchEvent(new CustomEvent('workspace-change', { detail: snap('scripts/main.js') }));
    await nextFrame();
    expect(deploy.disabled).toBe(false);
    expect(el.querySelector('[data-testid="deploy-hint"]')).toBeNull();
  });

  it('shows the deploy progress and result next to the buttons', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();
    window.dispatchEvent(new CustomEvent('workspace-change', {
      detail: { mode: 'device', message: '', files: [], busy: false, currentKey: 'scripts/main.js', dirty: false, isNew: false },
    }));
    window.dispatchEvent(new CustomEvent('deploy-request'));
    await nextFrame();
    const deploy = el.querySelector('[data-testid="toolbar-deploy"]') as HTMLButtonElement;
    expect(deploy.disabled).toBe(true);
    expect(deploy.textContent).toContain('Deploying');

    window.dispatchEvent(new CustomEvent('deploy-result', { detail: { success: false, message: 'Validation failed' } }));
    await nextFrame();
    expect(deploy.disabled).toBe(false);
    expect(el.querySelector('[data-testid="toolbar-result"]')?.textContent).toContain('Validation failed');
  });

  it('keeps Download and Clear in the more menu', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();
    expect(el.querySelector('[data-testid="toolbar-menu"]')).toBeNull();
    (el.querySelector('[data-testid="toolbar-more"]') as HTMLElement).click();
    await nextFrame();
    expect(el.querySelector('[data-testid="menu-download"]')).not.toBeNull();
    expect(el.querySelector('[data-testid="menu-clear"]')).not.toBeNull();
  });

  it('connects on Enter and offers Disconnect when connected', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();
    const requests: CustomEvent[] = [];
    const handler = (e: Event) => requests.push(e as CustomEvent);
    window.addEventListener('connection-request', handler);
    const input = el.querySelector('input[aria-label="Device address"]') as HTMLInputElement;
    input.value = 'ws://10.0.0.5';
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    window.removeEventListener('connection-request', handler);
    expect(requests.map(r => r.detail.url)).toEqual(['ws://10.0.0.5']);

    window.dispatchEvent(new CustomEvent('connection-change', { detail: { connected: true, url: 'ws://10.0.0.5' } }));
    await nextFrame();
    expect(el.querySelector('[data-testid="connect-button"]')?.textContent).toContain('Disconnect');
    expect(el.querySelector('[data-testid="connection-state"]')?.textContent).toContain('Connected');
  });

  it('has a connection input with placeholder', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();

    const input = el.querySelector('input[placeholder="ws://device.local"]');
    expect(input).not.toBeNull();
  });

  it('has a connection indicator dot', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();

    // Starts disconnected (red), turns green on connection
    const dot = el.querySelector('.bg-status-red') || el.querySelector('.bg-status-green');
    expect(dot).not.toBeNull();
  });
});

describe('fb-code-editor', () => {
  it('renders CodeMirror container', async () => {
    const el = createElement('fb-code-editor');
    await nextFrame();

    const container = el.querySelector('#cm-editor');
    expect(container).not.toBeNull();
  });

  it('uses translucent background for ripple effect', async () => {
    const el = createElement('fb-code-editor');
    await nextFrame();

    const bgElements = el.querySelectorAll('.bg-black\\/70');
    expect(bgElements.length).toBeGreaterThanOrEqual(1);
  });
});

describe('fb-status-bar', () => {
  it('shows line and column info', async () => {
    const el = createElement('fb-status-bar');
    await nextFrame();

    expect(el.textContent).toContain('Ln');
    expect(el.textContent).toContain('Col');
  });

  it('shows valid indicator', async () => {
    const el = createElement('fb-status-bar');
    await nextFrame();

    expect(el.textContent).toContain('Valid');
    const greenText = el.querySelector('.text-status-green');
    expect(greenText).not.toBeNull();
  });

  it('shows error icon and text when invalid', async () => {
    const el = createElement('fb-status-bar');
    await nextFrame();

    window.dispatchEvent(new CustomEvent('validation-result', {
      detail: { valid: false, errors: ['some error'], errorCount: 1 }
    }));
    await nextFrame();

    expect(el.textContent).toContain('1 error');
    expect(el.querySelector('.text-status-red')).not.toBeNull();
  });
});

describe('fb-file-drawer', () => {
  const snapshot = (over: Partial<WorkspaceSnapshot> = {}): WorkspaceSnapshot => ({
    mode: 'device',
    message: '',
    files: [
      { key: 'config.json', size: 10082 },
      { key: 'scripts/main.js', size: 1200 },
      { key: 'scripts/util.js', size: 300 },
    ],
    busy: false,
    currentKey: 'scripts/main.js',
    dirty: false,
    isNew: false,
    ...over,
  });

  async function drawerWith(snap: WorkspaceSnapshot, panel: 'app' | 'images' | 'fonts' = 'app') {
    const el = createElement('fb-file-drawer');
    (el as unknown as { panel: string }).panel = panel;
    await nextFrame();
    window.dispatchEvent(new CustomEvent('workspace-change', { detail: snap }));
    await nextFrame();
    return el;
  }

  it('one panel per kind: App (files and Other), Images, Fonts', async () => {
    const snap = snapshot({
      files: [
        { key: 'config.json', size: 10082 },
        { key: 'icons/sun.svg', size: 800 },
        { key: 'screenshot-main.rgb565', size: 153608 },
        { key: 'scripts/main.js', size: 1200 },
      ],
    });
    const keys = (el: Element, id: string) => Array.from(el.querySelectorAll(`[data-testid="${id}"] [data-key]`)).map(n => n.getAttribute('data-key'));
    const app = await drawerWith(snap);
    expect(app.querySelector('[data-testid="panel-title"]')!.textContent).toBe('App');
    expect(keys(app, 'user-files')).toEqual(['config.json', 'scripts/main.js']);
    expect(keys(app, 'other-files')).toEqual(['screenshot-main.rgb565']);
    expect(app.querySelector('[data-testid="icon-files"]')).toBeNull();
    expect(app.querySelector('[data-testid="fonts-files"]')).toBeNull();

    const icons = await drawerWith(snap, 'images');
    expect(icons.querySelector('[data-testid="panel-title"]')!.textContent).toBe('Images');
    expect(keys(icons, 'icon-files')).toEqual(['icons/sun.svg']);
    expect(icons.querySelector('[data-testid="user-files"]')).toBeNull();
    expect(icons.querySelector('[data-testid="other-files"]')).toBeNull();
  });

  it('shows used and free space of the store behind each panel', async () => {
    const MB = 1024 * 1024;
    const storage = { fs: { total: 4 * MB, used: MB }, media: { total: 8 * MB, used: 2 * MB, free: 5 * MB } };
    const space = (el: Element) => el.querySelector('[data-testid="storage-space"]')!.textContent!.replace(/\s+/g, ' ').trim();
    const snap = snapshot({ storage, readOnly: { fonts: [] } });
    expect(space(await drawerWith(snap))).toBe('1.0 MB used 3.0 MB free');
    expect(space(await drawerWith(snap, 'images'))).toBe('1.0 MB used 3.0 MB free');
    expect(space(await drawerWith(snap, 'fonts'))).toBe('2.0 MB used 5.0 MB free');
    // Without the device's numbers: what the listed files add up to (App only).
    expect(space(await drawerWith(snapshot()))).toContain('3 files');
    expect((await drawerWith(snapshot(), 'images')).querySelector('[data-testid="storage-space"]')).toBeNull();
  });

  it('has no Examples or System group (examples live in the Examples panel)', async () => {
    const el = await drawerWith(snapshot({
      readOnly: { examples: [{ key: 'button.js', size: 900 }], web: [{ key: 'index.html.gz', size: 250000 }] },
    }));
    expect(el.querySelector('[data-testid="examples-files"]')).toBeNull();
    expect(el.querySelector('[data-testid="system-files"]')).toBeNull();
    expect(el.textContent).not.toContain('index.html.gz');
  });

  it('a file the IDE cannot open can be deleted and opens the asset view', async () => {
    const el = await drawerWith(snapshot({ files: [{ key: 'screenshot-main.rgb565', size: 153608 }] }));
    const row = el.querySelector('[data-key="screenshot-main.rgb565"]')!;
    expect(row.querySelector('[data-testid="delete-file"]')).not.toBeNull();

    const views: CustomEvent[] = [];
    const handler = (e: Event) => views.push(e as CustomEvent);
    window.addEventListener('main-view', handler);
    (row.querySelector('[data-testid="open-file"]') as HTMLElement).click();
    await nextFrame();
    window.removeEventListener('main-view', handler);
    expect(views.map(v => v.detail)).toEqual([{ view: 'image', key: 'screenshot-main.rgb565' }]);
  });

  it('fonts panel: the fonts, upload, and a row opens the Fonts view', async () => {
    const el = await drawerWith(snapshot({
      readOnly: { fonts: [{ key: 'medium.ttf', size: 198000 }, { key: 'roboto.ttf', size: 160000 }] },
    }), 'fonts');
    const fonts = el.querySelector('[data-testid="fonts-files"]')!;
    expect(fonts.querySelector('[data-testid="upload-font"]')).not.toBeNull();
    expect(fonts.querySelector('[data-testid="font-upload-input"]')!.getAttribute('accept')).toContain('.ttf');
    expect(fonts.textContent).toContain('medium.ttf');
    expect(fonts.textContent).toContain('roboto.ttf');
    expect(el.querySelector('[data-testid="user-files"]')).toBeNull();

    const views: CustomEvent[] = [];
    const handler = (e: Event) => views.push(e as CustomEvent);
    window.addEventListener('main-view', handler);
    (fonts.querySelector('[data-testid="open-fonts"]') as HTMLElement).click();
    window.removeEventListener('main-view', handler);
    expect(views.map(v => v.detail.view)).toEqual(['fonts']);
  });

  it('explains what pushing icons does, next to the icons', async () => {
    const el = await drawerWith(snapshot({ files: [{ key: 'icons/sun.svg', size: 800 }] }), 'images');
    const status = el.querySelector('[data-testid="icon-files"] [data-testid="icon-sync-status"]')!;
    expect(status.textContent).toContain('every save of config.json');
    expect(status.textContent).toContain('Push images');
  });

  it('hides the device-only sections without a device', async () => {
    const local = snapshot({ mode: 'local', message: LOCAL_MESSAGE, files: [{ key: 'icons/sun.svg', size: 800 }] });
    const fonts = await drawerWith(local, 'fonts');
    expect(fonts.querySelector('[data-testid="upload-font"]')).toBeNull();
    expect(fonts.textContent).toContain('connect to one');
    const images = await drawerWith(local, 'images');
    expect(images.querySelector('[data-testid="icon-sync-status"]')).toBeNull();
  });

  it('offers delete only for deletable files (not config.json)', async () => {
    const el = await drawerWith(snapshot());
    const deletable = Array.from(el.querySelectorAll('[data-testid="delete-file"]'))
      .map(b => b.closest('[data-key]')!.getAttribute('data-key'));
    expect(deletable).toEqual(['scripts/main.js', 'scripts/util.js']);
  });

  it('marks the open file as unsaved', async () => {
    const el = await drawerWith(snapshot({ dirty: true }));
    const main = el.querySelector('[data-key="scripts/main.js"]')!;
    expect(main.textContent).toContain('●');
  });

  it('shows a not-yet-saved main.js when it does not exist yet', async () => {
    const el = await drawerWith(snapshot({ files: [], isNew: true }));
    expect(el.querySelector('[data-testid="unsaved-file"]')?.textContent).toContain('scripts/main.js');
  });

  it('explains the browser-only fallback when no device storage is reachable', async () => {
    const el = await drawerWith(snapshot({ mode: 'local', message: LOCAL_MESSAGE, files: [] }));
    expect(el.querySelector('[data-testid="storage-source"]')?.textContent).toContain('Browser');
    expect(el.querySelector('[data-testid="workspace-message"]')?.textContent).toContain('browser session only');
  });

  it('shows storage totals', async () => {
    const el = await drawerWith(snapshot());
    expect(el.textContent).toContain('3 files');
    expect(el.textContent).toContain('KB used');
  });
});

describe('fb-button-grid with explicit positions', () => {
  it('renders one key per position, empty when unconfigured', async () => {
    const el = createElement('fb-button-grid') as any;
    el.currentPage = 1;
    el.buttons = [{ position: 7, label: 'Open', topLabel: 'Raam', ledOn: false, ledColor: 'rgb(0,0,0)', page: 1 }];
    el.positions = [7, 8];
    await nextFrame();
    const keys = Array.from(el.querySelectorAll('[data-position]')).map((n: any) => n.getAttribute('data-position'));
    expect(keys).toEqual(['7', '8']);
    expect(el.querySelector('[data-position="7"]').textContent).toContain('Raam');
    expect(el.querySelector('[data-position="8"]').textContent.trim()).toBe('');
  });
});

describe('fb-virtual-device layout from config.json', () => {
  it('renders connectors top to bottom as on the device: the display (TFT only), then bars', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();
    window.dispatchEvent(new CustomEvent('device-config', {
      detail: {
        pages: [{ number: 1, displayItems: [], buttons: [
          { position: 7, label: 'Open', topLabel: 'Raam', ledOn: false, ledColor: 'rgb(0,0,0)', page: 1 },
          { position: 8, label: 'Dicht', topLabel: 'Raam', ledOn: false, ledColor: 'rgb(0,0,0)', page: 1 },
        ] }],
        connectors: [{ id: 2, type: 'display' }, { id: 3, type: 'bar' }],
      },
    }));
    await nextFrame();
    expect(el.querySelectorAll('fb-tft-display').length).toBe(1);
    const grids = Array.from(el.querySelectorAll('fb-button-grid')).map(g => g.getAttribute('data-connector'));
    // The display's keys (positions 5/6 here) are presses on the screen
    // halves: no key row for them, as on the physical device.
    expect(grids).toEqual(['3']);
    expect(el.querySelector('fb-tft-display')?.getAttribute('data-connector')).toBe('2');
    await nextFrame();
    expect(el.textContent).toContain('Open');
    expect(el.textContent).toContain('Dicht');
    // Only page 1 exists now.
    const tabs = Array.from(el.querySelectorAll('button')).map(b => b.textContent?.trim()).filter(t => t?.startsWith('Page'));
    expect(tabs).toEqual(['Page 1']);
    // Restore the sample layout for later tests.
    loadDefaultConfig();
  });
});

describe('fb-virtual-device', () => {
  it('renders page tabs', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    const tabs = el.querySelectorAll('button');
    const tabTexts = Array.from(tabs).map(b => b.textContent?.trim());
    expect(tabTexts).toContain('Page 1');
    expect(tabTexts).toContain('Page 2');
  });

  it('renders TFT display', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    const tft = el.querySelector('fb-tft-display');
    expect(tft).not.toBeNull();
  });

  it('renders button grid', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    const grid = el.querySelector('fb-button-grid');
    expect(grid).not.toBeNull();
  });

  it('switches pages when tab is clicked', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    const tabs = el.querySelectorAll('button');
    const page2Tab = Array.from(tabs).find(b => b.textContent?.trim() === 'Page 2');
    expect(page2Tab).not.toBeUndefined();

    page2Tab!.click();
    await nextFrame();

    // Page 2 tab should now have the active styling
    expect(page2Tab!.classList.contains('bg-white/95')).toBe(true);
  });

  it('renders gear icon next to tabs', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    const gearBtn = el.querySelector('[data-testid="gear-icon"]');
    expect(gearBtn).not.toBeNull();
  });

  it('clicking gear toggles flipped class on flip container', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    const flipInner = el.querySelector('.device-flip-inner');
    expect(flipInner?.classList.contains('flipped')).toBe(false);

    const gearBtn = el.querySelector('[data-testid="gear-icon"]') as HTMLElement;
    gearBtn.click();
    await nextFrame();

    expect(flipInner?.classList.contains('flipped')).toBe(true);
  });

  it('settings card shows device info fields', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    // Flip to settings
    const gearBtn = el.querySelector('[data-testid="gear-icon"]') as HTMLElement;
    gearBtn.click();
    await nextFrame();

    const fields = el.querySelectorAll('[data-field]');
    const fieldNames = Array.from(fields).map(f => f.getAttribute('data-field'));
    expect(fieldNames).toContain('IPv4');
    expect(fieldNames).toContain('MAC');
    expect(fieldNames).toContain('Firmware');
    expect(fieldNames).toContain('Uptime');
  });

  it('shows placeholders (not fake values) when no device is connected', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    const gearBtn = el.querySelector('[data-testid="gear-icon"]') as HTMLElement;
    gearBtn.click();
    await nextFrame();

    const ipv4 = el.querySelector('[data-field="IPv4"]');
    const mac = el.querySelector('[data-field="MAC"]');
    // Old hardcoded placeholders must be gone; unconnected shows an em dash.
    expect(ipv4?.textContent?.trim()).toBe('—');
    expect(mac?.textContent?.trim()).toBe('—');
    expect(el.textContent).not.toContain('192.168.1.42');
    expect(el.textContent).not.toContain('AA:BB:CC:DD:EE:FF');
  });

  it('renders real device info from a device-info event', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    window.dispatchEvent(new CustomEvent('device-info', {
      detail: {
        data: {
          deviceName: 'btn_live01', ipv4: '10.0.0.7', ipv6: 'fe80::abcd', ssid: 'MyWifi',
          rssi: -47, mac: '11:22:33:44:55:66', firmware: '9.9.9', uptime: 3661,
          heapFree: 184320, heapTotal: 327680, heapMinFree: 153600,
          lvglFree: 40960, lvglTotal: 61440,
        },
      },
    }));
    await nextFrame();

    const gearBtn = el.querySelector('[data-testid="gear-icon"]') as HTMLElement;
    gearBtn.click();
    await nextFrame();

    expect(el.querySelector('[data-field="IPv4"]')?.textContent?.trim()).toBe('10.0.0.7');
    expect(el.querySelector('[data-field="IPv6"]')?.textContent?.trim()).toBe('fe80::abcd');
    expect(el.querySelector('[data-field="SSID"]')?.textContent?.trim()).toBe('MyWifi');
    expect(el.querySelector('[data-field="MAC"]')?.textContent?.trim()).toBe('11:22:33:44:55:66');
    expect(el.querySelector('[data-field="Firmware"]')?.textContent?.trim()).toBe('9.9.9');
    expect(el.querySelector('[data-field="Signal"]')?.textContent?.trim()).toBe('-47 dBm');
    // Uptime 3661s -> 0d 1h 1m
    expect(el.querySelector('[data-field="Uptime"]')?.textContent?.trim()).toBe('0d 1h 1m');
  });

  it('shows firmware with commit, the model and the heap low-water mark', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    window.dispatchEvent(new CustomEvent('device-info', {
      detail: {
        data: {
          deviceName: 'btn_live02', ipv4: '10.0.0.8', ipv6: '', ssid: 'w',
          rssi: -50, mac: 'aa', firmware: '3.1.0-alpha.1', uptime: 10,
          commit: '30df729', dirty: true, model: 'Model 1 Version 9',
          heapFree: 72620, heapTotal: 270028, heapMinFree: 35852,
          lvglFree: 20496, lvglTotal: 57164,
        },
      },
    }));
    await nextFrame();
    expect(el.querySelector('[data-testid="heap-min"]')?.textContent?.replace(/\s+/g, ' ').trim()).toBe('min 35 KB');

    (el.querySelector('[data-testid="gear-icon"]') as HTMLElement).click();
    await nextFrame();
    expect(el.querySelector('[data-field="Firmware"]')?.textContent?.trim()).toBe('3.1.0-alpha.1 (30df729-dirty)');
    expect(el.querySelector('[data-field="Model"]')?.textContent?.trim()).toBe('Model 1 Version 9');
  });

  it('memory graph shows real KB values with a quantifier', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    window.dispatchEvent(new CustomEvent('device-info', {
      detail: {
        data: {
          deviceName: 'btn', ipv4: '1.1.1.1', ipv6: '::1', ssid: 's', rssi: -50,
          mac: 'a', firmware: 'f', uptime: 1,
          heapFree: 184320 /* 180 KB */, heapTotal: 327680, heapMinFree: 100000,
          lvglFree: 40960 /* 40 KB */, lvglTotal: 61440,
        },
      },
    }));
    await nextFrame();

    const gearBtn = el.querySelector('[data-testid="gear-icon"]') as HTMLElement;
    gearBtn.click();
    await nextFrame();

    const graph = el.querySelector('[data-testid="memory-graph"]');
    expect(graph?.textContent).toContain('180 KB'); // heapFree in KB
    expect(graph?.textContent).toContain('40 KB');  // lvglFree in KB
  });

  it('back side is read-only and hides the page tabs', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();
    const tabs = el.querySelector('[data-testid="page-tabs"]') as HTMLElement;
    expect(tabs.classList.contains('invisible')).toBe(false);

    (el.querySelector('[data-testid="gear-icon"]') as HTMLElement).click();
    await nextFrame();

    // No fields that look editable but are never saved.
    expect(el.querySelector('.device-flip-back input')).toBeNull();
    // The tabs belong to the front face.
    expect(tabs.classList.contains('invisible')).toBe(true);
    expect(Array.from(tabs.querySelectorAll('button')).every(b => (b as HTMLButtonElement).disabled)).toBe(true);
  });

  it('shows the device name on the TFT once the device reported it', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();
    expect(el.querySelector('[data-testid="tft-device-name"]')?.textContent?.trim()).toBe('');

    window.dispatchEvent(new CustomEvent('device-info', {
      detail: {
        data: {
          deviceName: 'btn_a00001', ipv4: '1.1.1.1', ipv6: '::1', ssid: 's', rssi: -50,
          mac: 'a', firmware: 'f', uptime: 1,
          heapFree: 1, heapTotal: 2, heapMinFree: 1, lvglFree: 1, lvglTotal: 2,
        },
      },
    }));
    await nextFrame();
    await nextFrame();
    expect(el.querySelector('[data-testid="tft-device-name"]')?.textContent?.trim()).toBe('btn_a00001');
  });

  it('outlines the config entry under the editor cursor and reveals an entry on click', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();
    const { configToModel } = await import('../../js/config-model.js');
    const { setPreviewConfig, clearPreviewConfig } = await import('../../js/device-config.js');
    setPreviewConfig(configToModel({
      buttons: [{ position: 1, label: 'A' }],
      displayitems: [{ x: 0, y: 0, label: 'Temp' }, { x: 50, y: 0, label: 'Hum' }],
    }));
    await nextFrame();
    const item = el.querySelector('[data-ref-kind="displayitem"][data-ref]') as HTMLElement;
    expect(item).not.toBeNull();
    const [list, index] = item.getAttribute('data-ref')!.split(':');

    window.dispatchEvent(new CustomEvent('config-selection', {
      detail: { ref: { kind: 'displayitem', list, index: Number(index) } },
    }));
    await nextFrame();
    await nextFrame();
    expect(el.querySelectorAll('[data-selected]').length).toBe(1);
    expect(el.querySelector('[data-selected]')?.getAttribute('data-ref')).toBe(`${list}:${index}`);

    const reveals: CustomEvent[] = [];
    const handler = (e: Event) => reveals.push(e as CustomEvent);
    window.addEventListener('config-reveal', handler);
    (el.querySelector('[data-ref-kind="displayitem"][data-ref]') as HTMLElement).click();
    window.removeEventListener('config-reveal', handler);
    expect(reveals.length).toBe(1);
    expect(reveals[0].detail.ref).toEqual({ kind: 'displayitem', list, index: Number(index) });
    clearPreviewConfig();
  });

  it('renders real (heap + lvgl) SVG polylines once device data arrives', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    window.dispatchEvent(new CustomEvent('device-info', {
      detail: {
        data: {
          deviceName: 'btn', ipv4: '1.1.1.1', ipv6: '::1', ssid: 's', rssi: -50,
          mac: 'a', firmware: 'f', uptime: 1,
          heapFree: 184320, heapTotal: 327680, heapMinFree: 100000,
          lvglFree: 40960, lvglTotal: 61440,
        },
      },
    }));
    await nextFrame();

    // Flip to settings
    const gearBtn = el.querySelector('[data-testid="gear-icon"]') as HTMLElement;
    gearBtn.click();
    await nextFrame();

    const graph = el.querySelector('[data-testid="memory-graph"]');
    expect(graph).not.toBeNull();

    const lines = graph!.querySelectorAll('polyline[data-series]');
    const series = Array.from(lines).map(l => l.getAttribute('data-series'));
    expect(series).toContain('heap');
    expect(series).toContain('lvgl');
    // The fabricated "js" series (no firmware source) must be gone.
    expect(series).not.toContain('js');
  });

  it('memory graph shows a waiting placeholder before any device data', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    const gearBtn = el.querySelector('[data-testid="gear-icon"]') as HTMLElement;
    gearBtn.click();
    await nextFrame();

    const graph = el.querySelector('[data-testid="memory-graph"]');
    expect(graph?.textContent).toContain('Heap');
    expect(graph?.textContent).toContain('LVGL');
    expect(graph?.querySelector('[data-testid="memory-empty"]')).not.toBeNull();
    expect(graph?.querySelectorAll('polyline[data-series]').length).toBe(0);
  });
});

describe('fb-tft-display', () => {
  it('renders display items', async () => {
    const el = createElement('fb-tft-display') as any;
    el.displayItems = [
      { x: 0, y: 5, width: 100, height: 50, type: 'text', label: 'Amsterdam', value: 'time', fontSize: 42 },
      { x: 0, y: 50, width: 100, height: 50, type: 'number', label: 'Button+ Likes', value: '67005', fontSize: 18 },
    ];
    await nextFrame();

    expect(el.textContent).toContain('Amsterdam');
    expect(el.textContent).toContain('Button+ Likes');
  });

  it('shows live time', async () => {
    const el = createElement('fb-tft-display') as any;
    el.displayItems = [
      { x: 0, y: 5, width: 100, height: 50, type: 'text', label: 'Amsterdam', value: 'time', fontSize: 42 },
    ];
    await nextFrame();

    // Time should contain a colon (HH:MM format)
    const text = el.textContent || '';
    expect(text).toMatch(/\d+:\d{2}/);
  });
});

describe('fb-tft-display fonts', () => {
  const scale = 283 / 320;
  const fontPx = (el: Element | null) => parseFloat((el as HTMLElement).style.fontSize);

  it('draws value and caption at the configured sizes, markup as spans', async () => {
    setPreviewConfig(configToModel({
      style: { fonts: [{ name: 'roboto', src: 'roboto.ttf' }] },
      displayitems: [{ name: 'temp', font: 'roboto', fontsize: 40, label: 'Temp', unit: '\\s{20}°C' }],
    }));
    const el = createElement('fb-tft-display') as any;
    const item = configToModel({
      style: { fonts: [{ name: 'roboto', src: 'roboto.ttf' }] },
      displayitems: [{ name: 'temp', font: 'roboto', fontsize: 40, label: 'Temp', unit: '\\s{20}°C' }],
    }).pages[0]!.displayItems[0]!;
    el.displayItems = [{ ...item, value: '21\\cff0000!' }];
    await nextFrame();
    const box = el.querySelector('[data-display-item="temp"]')!;
    expect(fontPx(box.querySelector('[data-caption] span'))).toBeCloseTo(14 * scale, 1);
    const spans = Array.from(box.querySelectorAll('[data-value] [data-rich] span')) as HTMLElement[];
    expect(spans.map(s => s.textContent)).toEqual(['21', '!', '°C']);
    expect(fontPx(spans[0]!)).toBeCloseTo(40 * scale, 1);
    expect(spans[1]!.style.color).toMatch(/ff0000|255, 0, 0/);
    expect(fontPx(spans[2]!)).toBeCloseTo(20 * scale, 1);
    // Without the device fonts loaded: the CSS fallback.
    expect(spans[0]!.style.fontFamily).toContain('sans-serif');
    loadDefaultConfig();
  });

  it('uses the legacy size of fontsize without a font (fontsize no longer ignored)', async () => {
    const el = createElement('fb-tft-display') as any;
    el.displayItems = [{ x: 0, y: 0, width: 50, height: 0, type: 'text', label: 'L', value: 'v', fontSize: 4 }];
    await nextFrame();
    expect(fontPx(el.querySelector('[data-value] span'))).toBeCloseTo(48 * scale, 1);
  });

  it('a list at a px fontsize', async () => {
    const el = createElement('fb-tft-display') as any;
    el.lists = [mapList({ name: 'l', fontsize: 20, items: ['a'] })];
    await nextFrame();
    expect(fontPx(el.querySelector('[data-list="l"]'))).toBeCloseTo(20 * scale, 1);
  });
});

describe('previews follow config.json "style"', () => {
  const scale = 283 / 320;
  const fontPx = (el: Element | null) => parseFloat((el as HTMLElement).style.fontSize);
  const rgb = (el: Element | null) => (el as HTMLElement).style.color;

  it('defaults: gold #ffd700 captions and top labels, white values, grey units', async () => {
    loadDefaultConfig();
    const model = configToModel({
      buttons: [{ position: 1, label: 'A', toplabel: 'T' }],
      displayitems: [{ name: 'd', label: 'Temp', unit: '°C' }],
    });
    const grid = createElement('fb-button-grid') as any;
    grid.buttons = model.pages[0]!.buttons;
    const tft = createElement('fb-tft-display') as any;
    tft.displayItems = model.pages[0]!.displayItems.map(i => ({ ...i, value: '21' }));
    await nextFrame();
    expect(rgb(grid.querySelector('[data-top-label]'))).toMatch(/#ffd700|255, 215, 0/);
    expect(rgb(grid.querySelector('[data-label]'))).toMatch(/#ffffff|255, 255, 255/);
    expect(rgb(tft.querySelector('[data-caption]'))).toMatch(/#ffd700|255, 215, 0/);
    expect(rgb(tft.querySelector('[data-unit]'))).toMatch(/#999999|153, 153, 153/);
    expect(tft.innerHTML).not.toContain('255,211,95');
  });

  it('a style changes fonts, sizes and colours of items without their own fields', async () => {
    const cfg = {
      style: {
        title: { text_color: '#00ff00' },
        key: { label: { text_size: 40, text_color: '#ff0000' } },
        display: { value: { text_size: 30 }, unit: { text_size: 12, text_color: '#0000ff' } },
        list: { text_color: '#112233', selected: { bg_color: '#445566', text_color: '#ffffff', text_size: 20 } },
      },
      buttons: [{ position: 1, label: 'A', toplabel: 'T' }, { position: 2, label: 'B', fontsize: 24 }],
      displayitems: [{ name: 'd', label: 'Temp', unit: '°C' }],
      lists: [{ name: 'l', items: ['a', 'b'], selected: 0 }],
    };
    const model = configToModel(cfg);
    setPreviewConfig(model);
    const grid = createElement('fb-button-grid') as any;
    grid.buttons = model.pages[0]!.buttons;
    const tft = createElement('fb-tft-display') as any;
    tft.displayItems = model.pages[0]!.displayItems.map(i => ({ ...i, value: '21' }));
    tft.lists = model.pages[0]!.lists;
    await nextFrame();
    const mini = Math.min(113 / 160, 42 / 80);
    expect(fontPx(grid.querySelector('[data-position="1"] [data-label]'))).toBeCloseTo(40 * mini, 1);
    expect(fontPx(grid.querySelector('[data-position="2"] [data-label]'))).toBeCloseTo(24 * mini, 1); // local wins
    expect(rgb(grid.querySelector('[data-position="1"] [data-label]'))).toMatch(/#ff0000|255, 0, 0/);
    expect(rgb(grid.querySelector('[data-top-label]'))).toMatch(/#00ff00|0, 255, 0/);
    expect(rgb(tft.querySelector('[data-caption]'))).toMatch(/#00ff00|0, 255, 0/);
    expect(fontPx(tft.querySelector('[data-value] span'))).toBeCloseTo(30 * scale, 1);
    expect(fontPx(tft.querySelector('[data-unit] span'))).toBeCloseTo(12 * scale, 1);
    expect(rgb(tft.querySelector('[data-unit]'))).toMatch(/#0000ff|0, 0, 255/);
    const focus = tft.querySelector('[data-list-focus]') as HTMLElement;
    expect(focus.style.background).toMatch(/445566|68, 85, 102/);
    const [a, b] = Array.from(tft.querySelectorAll('[data-list-item]')) as HTMLElement[];
    expect(a!.style.color).toMatch(/#ffffff|255, 255, 255/);
    expect(fontPx(a!)).toBeCloseTo(20 * scale, 1);
    expect(b!.style.color).toMatch(/#112233|17, 34, 51/);
    loadDefaultConfig();
  });
});

describe('fb-button-grid fonts', () => {
  it('label medium 32 / top semibold 18 by default, scaled; configured fonts otherwise', async () => {
    const scale = Math.min(113 / 160, 42 / 80);
    const model = configToModel({ buttons: [
      { position: 1, label: 'A', toplabel: 'T' },
      { position: 2, label: 'B', toplabel: 'U', fontsize: 24, topfontsize: 12 },
    ] });
    const el = createElement('fb-button-grid') as any;
    el.buttons = model.pages[0]!.buttons;
    await nextFrame();
    const px = (sel: string, pos: number) => parseFloat((el.querySelector(`[data-position="${pos}"] ${sel}`) as HTMLElement).style.fontSize);
    expect(px('[data-label]', 1)).toBeCloseTo(32 * scale, 1);
    expect(px('[data-top-label]', 1)).toBeCloseTo(18 * scale, 1);
    expect(px('[data-label]', 2)).toBeCloseTo(24 * scale, 1);
    expect(px('[data-top-label]', 2)).toBeCloseTo(12 * scale, 1);
    expect((el.querySelector('[data-position="1"] [data-top-label]') as HTMLElement).style.fontWeight).toBe('600');
  });
});

describe('fb-tft-display lists', () => {
  const list = (over: Record<string, unknown> = {}) => mapList({
    name: 'scenes', x: 4, y: 18, width: 92, rows: 3, fontsize: 3,
    items: ['Bed Time', 'Evening', 'Movie'], selected: 1,
    color: '808080', focuscolor: 'ffd45f', focustextcolor: '000000', ...over,
  })!;
  // fontsize 3 = 32 px on the 320 px screen; the preview screen is 283 px wide.
  const rowH = 32 * 1.25 * 283 / 320;

  it('renders a roller: rows tall, the selected item in the middle row on the focus bar', async () => {
    const el = createElement('fb-tft-display') as any;
    el.lists = [list()];
    await nextFrame();
    const box = el.querySelector('[data-list="scenes"]') as HTMLElement;
    expect(box).not.toBeNull();
    expect(box.style.left).toBe('4%');
    expect(box.style.top).toBe('18%');
    expect(box.style.width).toBe('92%');
    expect(parseFloat(box.style.height)).toBeCloseTo(3 * rowH, 1);
    const focus = box.querySelector('[data-list-focus]') as HTMLElement;
    expect(parseFloat(focus.style.top)).toBeCloseTo(rowH, 1);
    expect(focus.style.background).toMatch(/ffd45f|255, 212, 95/i);
    const items = Array.from(box.querySelectorAll('[data-list-item]')) as HTMLElement[];
    expect(items.map(i => i.textContent?.trim())).toEqual(['Bed Time', 'Evening', 'Movie']);
    const selected = box.querySelector('[data-selected]') as HTMLElement;
    expect(selected.textContent?.trim()).toBe('Evening');
    expect(selected.style.color).toMatch(/#000000|0, 0, 0/);
    expect(items[0].style.color).toMatch(/808080|128, 128, 128/);
    // Selected (index 1) sits in the middle row: no shift.
    const strip = items[0].parentElement as HTMLElement;
    expect(strip.style.transform).toMatch(/translateY\(0(px)?\)/);
  });

  it('does not wrap: the first item selected leaves the rows above empty', async () => {
    const el = createElement('fb-tft-display') as any;
    el.lists = [list({ selected: 0, rows: 5 })];
    await nextFrame();
    const strip = el.querySelector('[data-list-item]').parentElement as HTMLElement;
    const shift = parseFloat(/translateY\((-?[\d.]+)px\)/.exec(strip.style.transform)![1]);
    expect(shift).toBeCloseTo(2 * rowH, 1);
  });
});

describe('fb-tft-display images', () => {
  const scale = 283 / 320;

  it('places an image at device pixels: placeholder until the SVG is known, then the icon', async () => {
    _clearIconSources();
    const el = createElement('fb-tft-display') as any;
    el.images = [mapImage({ name: 'radar', x: 160, y: 120, width: 64, height: 32, icon: 'rain' })];
    await nextFrame();
    const box = el.querySelector('[data-image="radar"]') as HTMLElement;
    expect(parseFloat(box.style.left)).toBeCloseTo(160 * scale, 1);
    expect(parseFloat(box.style.top)).toBeCloseTo(120 * scale, 1);
    expect(parseFloat(box.style.width)).toBeCloseTo(64 * scale, 1);
    expect(box.querySelector('[data-icon-missing]')).not.toBeNull();

    setIconSources([['rain', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"><path d="M0 0h24v24H0z"/></svg>']]);
    await nextFrame();
    const img = el.querySelector('[data-image="radar"] img') as HTMLImageElement;
    expect(img.getAttribute('src')).toMatch(/^data:image\/svg\+xml/);
    _clearIconSources();
  });

  it('shows a src image as a labelled box', async () => {
    const el = createElement('fb-tft-display') as any;
    el.images = [mapImage({ name: 'pic', width: 10, height: 10, src: 'photo.bin' })];
    await nextFrame();
    expect(el.querySelector('[data-image="pic"]')?.textContent?.trim()).toBe('photo.bin');
  });
});

describe('fb-virtual-device lists', () => {
  it('shows a list only on its page, live from a config.json draft', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();
    setPreviewConfig(configToModel({
      lists: [{ name: 'scenes', page: 2, items: ['Bed Time', 'Evening'], selected: 1 }],
    }));
    await nextFrame();
    expect(el.querySelector('[data-list="scenes"]')).toBeNull();
    const tab2 = Array.from(el.querySelectorAll('button')).find(b => b.textContent?.trim() === 'Page 2') as HTMLElement;
    tab2.click();
    await nextFrame();
    expect(el.querySelector('[data-list="scenes"] [data-selected]')?.textContent?.trim()).toBe('Evening');
    loadDefaultConfig();
  });
});

describe('fb-button-grid', () => {
  const testButtons = [
    { position: 5, label: 'Tolomeo', topLabel: '', ledOn: true, ledColor: 'rgb(255,0,0)', page: 1 },
    { position: 6, label: 'Green Lamp', topLabel: 'Virtual', ledOn: true, ledColor: 'rgb(255,0,0)', page: 1 },
    { position: 7, label: 'Like!', topLabel: 'Button+', ledOn: false, ledColor: 'rgb(0,0,0)', page: 1 },
    { position: 8, label: 'Kantoor', topLabel: '', ledOn: true, ledColor: 'rgb(255,0,0)', page: 1 },
  ];

  it('renders 4 buttons', async () => {
    const el = createElement('fb-button-grid') as any;
    el.buttons = testButtons;
    await nextFrame();

    const buttons = el.querySelectorAll('button');
    expect(buttons.length).toBe(4);
  });

  it('shows button labels', async () => {
    const el = createElement('fb-button-grid') as any;
    el.buttons = testButtons;
    await nextFrame();

    expect(el.textContent).toContain('Tolomeo');
    expect(el.textContent).toContain('Green Lamp');
    expect(el.textContent).toContain('Like!');
    expect(el.textContent).toContain('Kantoor');
  });

  it('shows LED indicators', async () => {
    const el = createElement('fb-button-grid') as any;
    el.buttons = testButtons;
    await nextFrame();

    // LED on = red border, LED off = black border
    const redLeds = el.querySelectorAll('.border-\\[rgb\\(255\\,0\\,0\\)\\]');
    const blackLeds = el.querySelectorAll('.border-black');
    expect(redLeds.length + blackLeds.length).toBe(4);
  });
});

describe('fb-console-pane', () => {
  it('renders header with Console title', async () => {
    const el = createElement('fb-console-pane');
    await nextFrame();

    expect(el.textContent).toContain('Console');
  });

  it('has a Clear button', async () => {
    const el = createElement('fb-console-pane');
    await nextFrame();

    const clearBtn = Array.from(el.querySelectorAll('button')).find(
      b => b.textContent?.trim() === 'Clear'
    );
    expect(clearBtn).not.toBeUndefined();
  });

  it('Clear button dispatches console-clear event', async () => {
    const el = createElement('fb-console-pane');
    await nextFrame();

    const events: Event[] = [];
    const handler = (e: Event) => events.push(e);
    window.addEventListener('console-clear', handler);

    const clearBtn = Array.from(el.querySelectorAll('button')).find(
      b => b.textContent?.trim() === 'Clear'
    );
    clearBtn!.click();
    await nextFrame();

    window.removeEventListener('console-clear', handler);
    expect(events.length).toBe(1);
  });
});

describe('fb-console-output', () => {
  it('renders welcome message on mount', async () => {
    const el = createElement('fb-console-output');
    await nextFrame();

    const lines = el.querySelectorAll('.p-3 > div');
    expect(lines.length).toBe(1);
    expect(lines[0].textContent).toContain('Console ready');
  });

  it('renders lines from console-line events', async () => {
    const el = createElement('fb-console-output');
    await nextFrame();

    window.dispatchEvent(new CustomEvent('console-line', { detail: { text: 'Hello world', type: 'log' } }));
    await nextFrame();

    const lines = el.querySelectorAll('.p-3 > div');
    expect(lines.length).toBe(2); // welcome + new line
    expect(lines[1].textContent).toContain('Hello world');
  });

  it('clears lines on console-clear event', async () => {
    const el = createElement('fb-console-output');
    await nextFrame();

    window.dispatchEvent(new CustomEvent('console-clear'));
    await nextFrame();

    const lines = el.querySelectorAll('.p-3 > div');
    expect(lines.length).toBe(0);
  });

  it('applies correct color classes for input lines', async () => {
    const el = createElement('fb-console-output');
    await nextFrame();

    // Welcome message uses info type which maps to text-[#4fc3f7]
    const infoLines = el.querySelectorAll('.text-\\[\\#4fc3f7\\]');
    expect(infoLines.length).toBeGreaterThan(0);
  });

  it('applies correct color classes for result lines', async () => {
    const el = createElement('fb-console-output');
    await nextFrame();

    window.dispatchEvent(new CustomEvent('console-line', { detail: { text: '42', type: 'result' } }));
    await nextFrame();

    const resultLines = el.querySelectorAll('.text-status-green');
    expect(resultLines.length).toBeGreaterThan(0);
  });

  it('applies correct color classes for log lines', async () => {
    const el = createElement('fb-console-output');
    await nextFrame();

    window.dispatchEvent(new CustomEvent('console-line', { detail: { text: 'some log', type: 'log' } }));
    await nextFrame();

    const logLines = el.querySelectorAll('.text-gray-100');
    expect(logLines.length).toBeGreaterThan(0);
  });
});

describe('fb-console-input', () => {
  it('renders inline prompt', async () => {
    const el = createElement('fb-console-input');
    await nextFrame();

    expect(el.textContent).toContain('>');
  });

  it('has textarea with placeholder', async () => {
    const el = createElement('fb-console-input');
    await nextFrame();

    const textarea = el.querySelector('textarea[placeholder="Type JavaScript..."]');
    expect(textarea).not.toBeNull();
  });

  it('dispatches console-eval on Enter with code', async () => {
    const el = createElement('fb-console-input');
    await nextFrame();

    const textarea = el.querySelector('textarea') as HTMLTextAreaElement;
    textarea.value = 'sensor.count()';

    const events: CustomEvent[] = [];
    const handler = (e: Event) => { events.push(e as CustomEvent); };
    window.addEventListener('console-eval', handler);

    textarea.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    await nextFrame();

    window.removeEventListener('console-eval', handler);
    expect(events.length).toBe(1);
    expect(events[0].detail.code).toBe('sensor.count()');
  });

  it('clears input after execution', async () => {
    const el = createElement('fb-console-input');
    await nextFrame();

    const textarea = el.querySelector('textarea') as HTMLTextAreaElement;
    textarea.value = 'test()';

    textarea.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    await nextFrame();

    expect(textarea.value).toBe('');
  });

  it('navigates history with ArrowUp', async () => {
    const el = createElement('fb-console-input');
    await nextFrame();

    const textarea = el.querySelector('textarea') as HTMLTextAreaElement;

    // Execute two commands to build history
    textarea.value = 'first()';
    textarea.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    await nextFrame();

    textarea.value = 'second()';
    textarea.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    await nextFrame();

    // Arrow up should recall last command
    textarea.selectionStart = 0;
    textarea.selectionEnd = 0;
    textarea.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowUp', bubbles: true }));
    await nextFrame();

    expect(textarea.value).toBe('second()');
  });
});

describe('fb-resize-handle', () => {
  it('renders horizontal handle by default', async () => {
    const el = createElement('fb-resize-handle');
    await nextFrame();

    // Horizontal handle has w-px line
    const line = el.querySelector('.w-px');
    expect(line).not.toBeNull();
  });

  it('renders vertical handle when direction is vertical', async () => {
    const el = createElement('fb-resize-handle');
    el.dataset.direction = 'vertical';
    await nextFrame();

    // Force re-render
    (el as any).requestUpdate?.();
    await nextFrame();

    const line = el.querySelector('.h-\\[0\\.5px\\]');
    expect(line).not.toBeNull();
  });

  it('dispatches resize events on drag', async () => {
    const el = createElement('fb-resize-handle');
    await nextFrame();

    const events: CustomEvent[] = [];
    el.addEventListener('resize', ((e: CustomEvent) => {
      events.push(e);
    }) as EventListener);

    // Simulate mousedown
    el.dispatchEvent(new MouseEvent('mousedown', { clientX: 100, clientY: 100 }));

    // Simulate mousemove
    document.dispatchEvent(new MouseEvent('mousemove', { clientX: 110, clientY: 100 }));

    expect(events.length).toBe(1);
    expect(events[0].detail.delta).toBe(10);

    // Simulate mouseup to clean up
    document.dispatchEvent(new MouseEvent('mouseup'));
  });
});

describe('fb-editor-pane', () => {
  it('renders toolbar, code editor, and status bar', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();

    expect(el.querySelector('fb-toolbar')).not.toBeNull();
    expect(el.querySelector('fb-code-editor')).not.toBeNull();
    expect(el.querySelector('fb-status-bar')).not.toBeNull();
  });

  it('renders file drawer sidebar by default', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();

    expect(el.querySelector('fb-file-drawer')).not.toBeNull();
  });

  it('toggles assets sidebar on sidebar-panel event', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();

    const sidebar = el.querySelector('[data-testid="sidebar"]') as HTMLElement;
    // Sidebar starts open with assets
    expect(sidebar.style.width).not.toBe('0px');

    // Close by toggling assets again
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'app' } }));
    await nextFrame();
    expect(sidebar.style.width).toBe('0px');

    // Reopen
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'app' } }));
    await nextFrame();
    expect(sidebar.style.width).not.toBe('0px');
  });

  it('switches from assets to examples panel', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();

    // Starts with assets
    expect(el.querySelector('fb-file-drawer')).not.toBeNull();

    // Switch to examples
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'examples' } }));
    await nextFrame();

    // Assets gone, examples visible
    expect(el.querySelector('fb-file-drawer')).toBeNull();
    expect(el.textContent).toContain('Examples');
    expect(el.textContent).toContain('LED Blink');
  });

  it('switches from examples back to assets', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();

    // Switch to examples
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'examples' } }));
    await nextFrame();
    expect(el.querySelector('fb-file-drawer')).toBeNull();

    // Switch back to assets
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'app' } }));
    await nextFrame();
    expect(el.querySelector('fb-file-drawer')).not.toBeNull();
  });

  it('an example is shown read-only in the main pane: no dialog, no file made or opened', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'examples' } }));
    await nextFrame();

    const create = vi.spyOn(workspace, 'create').mockResolvedValue(false);
    const open = vi.spyOn(workspace, 'open').mockResolvedValue(false);
    const item = el.querySelectorAll('[data-example]')[1] as HTMLElement;
    item.click();
    await nextFrame();
    await nextFrame();

    expect(document.querySelector('fb-dialog [data-testid="dialog"]')).toBeNull();
    expect(create).not.toHaveBeenCalled();
    expect(open).not.toHaveBeenCalled();
    const view = el.querySelector('fb-example-view [data-testid="example-view"]')!;
    expect(view.getAttribute('data-example-id')).toBe(item.getAttribute('data-example'));
    expect(view.querySelector('.cm-content')?.getAttribute('contenteditable')).toBe('false');
    expect(el.querySelector('fb-code-editor')!.classList.contains('hidden')).toBe(true);
    create.mockRestore();
    open.mockRestore();
  });

  it('a selection replaces the whole middle pane: Device and Docs close the list of another entry', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();
    const sidebar = el.querySelector('[data-testid="sidebar"]') as HTMLElement;
    const layouts: Array<{ view: string; panel: string | null }> = [];
    const onLayout = (e: Event) => layouts.push((e as CustomEvent).detail);
    window.addEventListener('layout-change', onLayout);
    const go = async (name: string, detail: object) => { window.dispatchEvent(new CustomEvent(name, { detail })); await nextFrame(); };

    await go('main-view', { view: 'example' });
    await go('sidebar-panel', { panel: 'examples', open: true });
    expect(el.querySelector('[data-example]')).not.toBeNull();
    expect(el.querySelector('fb-example-view')).not.toBeNull();

    await go('main-view', { view: 'device' });
    expect(sidebar.style.width).toBe('0px');
    expect(el.querySelector('[data-example]')).toBeNull();
    expect(el.querySelector('fb-example-view')).toBeNull();
    expect(layouts[layouts.length - 1]).toEqual({ view: 'device', panel: null });

    await go('main-view', { view: 'fonts' });
    expect(layouts[layouts.length - 1]).toEqual({ view: 'fonts', panel: 'fonts' });
    await go('main-view', { view: 'docs' });
    expect(layouts[layouts.length - 1]).toEqual({ view: 'docs', panel: null });
    // Back to the editor from any other view: the editor with the App list.
    await go('main-view', { view: 'editor' });
    expect(layouts[layouts.length - 1]).toEqual({ view: 'editor', panel: 'app' });
    await go('main-view', { view: 'fonts' });
    await go('main-view', { view: 'editor' });
    expect(layouts[layouts.length - 1]).toEqual({ view: 'editor', panel: 'app' });
    // In the editor the list can be folded, and stays folded.
    await go('sidebar-panel', { panel: 'app' });
    await go('main-view', { view: 'editor' });
    expect(layouts[layouts.length - 1]).toEqual({ view: 'editor', panel: null });
    window.removeEventListener('layout-change', onLayout);
  });

  it('Images and Fonts take dropped files; other views do not', async () => {
    const uploads = await import('../../js/uploads.js');
    const dropped = vi.spyOn(uploads, 'uploadDropped').mockResolvedValue({ images: 1, fonts: 0 });
    const el = createElement('fb-editor-pane');
    await nextFrame();
    const middle = el.querySelector('[data-testid="middle"]') as HTMLElement;
    const file = new File(['<svg/>'], 'sun.svg');
    const drag = (type: string) => {
      const e = new Event(type, { bubbles: true, cancelable: true }) as Event & { dataTransfer: unknown };
      e.dataTransfer = { types: ['Files'], files: [file], dropEffect: 'none' };
      middle.dispatchEvent(e);
      return e;
    };

    // The editor (App) is shown: a drop is left to the browser.
    expect(drag('dragover').defaultPrevented).toBe(false);
    drag('drop');
    expect(dropped).not.toHaveBeenCalled();

    window.dispatchEvent(new CustomEvent('main-view', { detail: { view: 'image' } }));
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'images', open: true } }));
    await nextFrame();
    drag('dragenter');
    await nextFrame();
    expect(el.querySelector('[data-testid="drop-overlay"]')?.textContent).toContain('.svg');
    expect(drag('dragover').defaultPrevented).toBe(true);
    drag('drop');
    await nextFrame();
    expect(dropped.mock.calls[0][0]).toEqual([file]);
    expect(el.querySelector('[data-testid="drop-overlay"]')).toBeNull();
    dropped.mockRestore();
  });

  it('the image view without a file shows the images, or how to add one', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();
    window.dispatchEvent(new CustomEvent('main-view', { detail: { view: 'image' } }));
    await nextFrame();
    expect(el.querySelector('[data-testid="image-overview-empty"]')?.textContent).toContain('No images yet');
  });

  it('shows a view in place of the editor and keeps the editor alive', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();
    const editor = el.querySelector('fb-code-editor') as HTMLElement;
    expect(editor.classList.contains('hidden')).toBe(false);

    window.dispatchEvent(new CustomEvent('main-view', { detail: { view: 'device' } }));
    await nextFrame();
    expect(el.querySelector('fb-device-view')).not.toBeNull();
    expect(el.querySelector('fb-code-editor')).toBe(editor);
    expect(editor.classList.contains('hidden')).toBe(true);
    expect(el.querySelector('[data-testid="view-title"]')?.textContent).toContain('Device');

    (el.querySelector('[data-testid="back-to-editor"]') as HTMLElement).click();
    await nextFrame();
    expect(el.querySelector('fb-device-view')).toBeNull();
    expect(editor.classList.contains('hidden')).toBe(false);
  });
});

describe('fb-device-view', () => {
  it('offers a screenshot only with a device, and has the device actions', async () => {
    await import('../fb-device-view.js');
    const el = createElement('fb-device-view');
    await nextFrame();
    const shot = el.querySelector('[data-testid="take-screenshot"]') as HTMLButtonElement;
    expect(shot).not.toBeNull();
    expect(shot.disabled).toBe(true);
    expect(el.querySelector('[data-testid="device-offline"]')).not.toBeNull();
    expect(el.querySelector('[data-testid="device-reboot"]')).not.toBeNull();
    expect(el.querySelector('fb-device-info')).not.toBeNull();

    window.dispatchEvent(new CustomEvent('workspace-change', {
      detail: { mode: 'device', message: '', files: [], busy: false, currentKey: 'scripts/main.js', dirty: false, isNew: false },
    }));
    await nextFrame();
    expect(shot.disabled).toBe(false);
    expect(el.querySelector('[data-testid="device-offline"]')).toBeNull();
  });

  it('update: firmware and filesystem buttons need a device, refuse a wrong file, show the phases', async () => {
    await import('../fb-device-view.js');
    const el = createElement('fb-device-view');
    await nextFrame();
    const button = () => el.querySelector('[data-testid="update-firmware"]') as HTMLButtonElement;
    expect(button().disabled).toBe(true);
    window.dispatchEvent(new CustomEvent('workspace-change', {
      detail: { mode: 'device', message: '', files: [], busy: false, currentKey: 'scripts/main.js', dirty: false, isNew: false },
    }));
    await nextFrame();
    expect(button().disabled).toBe(false);

    // A picked file that is not an ESP32 image: said at once, nothing sent.
    const view = el as unknown as { _updatePicked(kind: string, e: Event): Promise<void>; _fw: unknown; _fwName: string };
    expect((el.querySelector('[data-testid="update-filesystem"]') as HTMLButtonElement).disabled).toBe(false);
    const file = new File([new Uint8Array([0x50, 0x4b])], 'bundle.zip');
    await view._updatePicked('filesystem', { target: { files: [file], value: '' } } as unknown as Event);
    await nextFrame();
    expect(el.querySelector('[data-testid="update-progress"]')!.textContent).toContain('not a LittleFS filesystem image');
    await view._updatePicked('firmware', { target: { files: [file], value: '' } } as unknown as Event);
    await nextFrame();
    const progress = () => el.querySelector('[data-testid="update-progress"]')!;
    expect(progress().getAttribute('data-phase')).toBe('failed');
    expect(progress().textContent).toContain('not an ESP32 firmware image');

    view._fwName = 'firmware.bin';
    view._fw = { phase: 'uploading', percent: 42 };
    await nextFrame();
    expect(progress().textContent).toContain('Uploading firmware.bin · 42%');
    expect(progress().querySelector('[role="progressbar"]')!.getAttribute('aria-valuenow')).toBe('42');
    expect(button().disabled).toBe(true);
    view._fw = { phase: 'applying' };
    await nextFrame();
    expect(progress().textContent).toContain('Applying');
    view._fw = { phase: 'done', message: 'The device is running 3.2.0.' };
    await nextFrame();
    expect(progress().textContent).toContain('Updated. The device is running 3.2.0.');
    expect(button().disabled).toBe(false);
  });
});

describe('fb-font-view', () => {
  it('lists the device fonts with the config name; system fonts have no delete', async () => {
    await import('../fb-font-view.js');
    const el = createElement('fb-font-view');
    await nextFrame();
    window.dispatchEvent(new CustomEvent('workspace-change', {
      detail: {
        mode: 'device', message: '', files: [], busy: false, currentKey: 'scripts/main.js', dirty: false, isNew: false,
        readOnly: { fonts: [{ key: 'medium.ttf', size: 198000 }, { key: 'Roboto-Bold.ttf', size: 160000 }] },
      },
    }));
    await nextFrame();
    const rows = Array.from(el.querySelectorAll('[data-font-key]'));
    expect(rows.map(r => r.getAttribute('data-font-key'))).toEqual(['medium.ttf', 'Roboto-Bold.ttf']);
    expect(rows[0].querySelector('[data-testid="delete-font"]')).toBeNull();
    expect(rows[0].textContent).toContain('system');
    expect(rows[1].querySelector('[data-testid="delete-font"]')).not.toBeNull();
    expect(rows[1].textContent).toContain('"Roboto_Bold"');
    // The device's sizes to compare at.
    expect(Array.from(el.querySelectorAll('[data-font-px]')).map(b => b.getAttribute('data-font-px'))).toEqual(['14', '24', '32', '48', '72']);
  });
});

describe('fb-image-view', () => {
  it('says that an unknown type cannot be shown and offers Delete', async () => {
    await import('../fb-image-view.js');
    const el = createElement('fb-image-view') as HTMLElement & { fileKey: string };
    el.fileKey = 'notes.bin';
    await nextFrame();
    await nextFrame();
    expect(el.querySelector('[data-testid="image-unknown"]')).not.toBeNull();
    expect(el.querySelector('[data-testid="image-delete"]')).not.toBeNull();
  });
});

describe('fb-right-pane', () => {
  it('renders virtual device and console', async () => {
    const el = createElement('fb-right-pane');
    await nextFrame();

    expect(el.querySelector('fb-virtual-device')).not.toBeNull();
    expect(el.querySelector('fb-console-pane')).not.toBeNull();
  });

  it('renders resize handle between device and console', async () => {
    const el = createElement('fb-right-pane');
    await nextFrame();

    expect(el.querySelector('fb-resize-handle')).not.toBeNull();
  });

  it('has bordered containers', async () => {
    const el = createElement('fb-right-pane');
    await nextFrame();

    const bordered = el.querySelectorAll('.rounded-2xl');
    expect(bordered.length).toBeGreaterThanOrEqual(2); // device + console
  });
});
