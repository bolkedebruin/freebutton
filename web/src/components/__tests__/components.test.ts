import { describe, it, expect, beforeAll, afterEach } from 'vitest';

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
  it('renders nav items for Code, Assets, Examples, Device, Docs, Settings', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();

    const navItems = el.querySelectorAll('fb-nav-item');
    const labels = Array.from(navItems).map(item => item.getAttribute('label'));

    expect(labels).toContain('Code');
    expect(labels).toContain('Assets');
    expect(labels).toContain('Examples');
    expect(labels).toContain('Device');
    expect(labels).toContain('Docs');
    expect(labels).toContain('Settings');
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
  it('prevents default on Ctrl+S', async () => {
    const el = createElement('fb-app');
    await nextFrame();

    const event = new KeyboardEvent('keydown', { key: 's', ctrlKey: true, bubbles: true, cancelable: true });
    window.dispatchEvent(event);

    expect(event.defaultPrevented).toBe(true);
  });

  it('Ctrl+L dispatches console-clear', async () => {
    const el = createElement('fb-app');
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
  it('Assets click dispatches sidebar-panel with assets', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();

    const events: CustomEvent[] = [];
    const handler = ((e: CustomEvent) => events.push(e)) as EventListener;
    window.addEventListener('sidebar-panel', handler);

    const assetsItem = Array.from(el.querySelectorAll('fb-nav-item')).find(
      item => item.getAttribute('label') === 'Assets'
    );
    assetsItem!.click();
    await nextFrame();

    window.removeEventListener('sidebar-panel', handler);
    expect(events.length).toBe(1);
    expect(events[0].detail.panel).toBe('assets');
  });

  it('Examples click dispatches sidebar-panel with examples', async () => {
    const el = createElement('fb-navbar');
    await nextFrame();

    const events: CustomEvent[] = [];
    const handler = ((e: CustomEvent) => events.push(e)) as EventListener;
    window.addEventListener('sidebar-panel', handler);

    const examplesItem = Array.from(el.querySelectorAll('fb-nav-item')).find(
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
  it('renders icon buttons', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();

    const buttons = el.querySelectorAll('button');
    expect(buttons.length).toBeGreaterThanOrEqual(5); // Play, Check, Upload, Download, Trash + Connect
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
  it('renders file list with correct count', async () => {
    const el = createElement('fb-file-drawer');
    await nextFrame();

    // 4 mock files
    const fileItems = el.querySelectorAll('.group\\/item');
    expect(fileItems.length).toBe(4);
  });

  it('has X delete buttons on file items', async () => {
    const el = createElement('fb-file-drawer');
    await nextFrame();

    const deleteButtons = el.querySelectorAll('.group\\/item button');
    expect(deleteButtons.length).toBe(4);
  });

  it('shows storage indicator', async () => {
    const el = createElement('fb-file-drawer');
    await nextFrame();

    expect(el.textContent).toContain('KB used');
    expect(el.textContent).toContain('128 KB');
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

  it('device name input is present and editable', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    // Flip to settings
    const gearBtn = el.querySelector('[data-testid="gear-icon"]') as HTMLElement;
    gearBtn.click();
    await nextFrame();

    const input = el.querySelector('[data-testid="device-name-input"]') as HTMLInputElement;
    expect(input).not.toBeNull();
    expect(input.value).toBe('btn_fab028');

    // Edit the name
    input.value = 'my_device';
    input.dispatchEvent(new Event('input', { bubbles: true }));
    await nextFrame();

    expect((el as any)._deviceName).toBe('my_device');
  });

  it('renders memory graph with three SVG polylines', async () => {
    const el = createElement('fb-virtual-device');
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
    expect(series).toContain('js');
  });

  it('memory graph legend shows current values', async () => {
    const el = createElement('fb-virtual-device');
    await nextFrame();

    const gearBtn = el.querySelector('[data-testid="gear-icon"]') as HTMLElement;
    gearBtn.click();
    await nextFrame();

    const graph = el.querySelector('[data-testid="memory-graph"]');
    expect(graph?.textContent).toContain('Heap');
    expect(graph?.textContent).toContain('LVGL');
    expect(graph?.textContent).toContain('JS');
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
    const handler = ((e: CustomEvent) => events.push(e)) as EventListener;
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

    // Sidebar starts open with assets
    expect(el.querySelector('.w-\\[200px\\]')).not.toBeNull();

    // Close by toggling assets again
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'assets' } }));
    await nextFrame();
    expect(el.querySelector('.w-0')).not.toBeNull();

    // Reopen
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'assets' } }));
    await nextFrame();
    expect(el.querySelector('.w-\\[200px\\]')).not.toBeNull();
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
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'assets' } }));
    await nextFrame();
    expect(el.querySelector('fb-file-drawer')).not.toBeNull();
  });

  it('examples panel dispatches load-example on click', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();

    // Open examples panel
    window.dispatchEvent(new CustomEvent('sidebar-panel', { detail: { panel: 'examples' } }));
    await nextFrame();

    const events: CustomEvent[] = [];
    const handler = ((e: CustomEvent) => events.push(e)) as EventListener;
    window.addEventListener('load-example', handler);

    const exampleBtn = el.querySelector('.group\\/example') as HTMLElement;
    exampleBtn?.click();
    await nextFrame();

    window.removeEventListener('load-example', handler);
    expect(events.length).toBe(1);
    expect(events[0].detail.name).toBeDefined();
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
