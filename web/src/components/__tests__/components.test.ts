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

  it('has a green connection indicator dot', async () => {
    const el = createElement('fb-toolbar');
    await nextFrame();

    const dot = el.querySelector('.bg-status-green');
    expect(dot).not.toBeNull();
  });
});

describe('fb-code-editor', () => {
  it('renders line numbers', async () => {
    const el = createElement('fb-code-editor');
    await nextFrame();

    const lineNumbers = el.querySelectorAll('.text-gray-200 > div');
    expect(lineNumbers.length).toBeGreaterThan(0);
  });

  it('uses translucent background for ripple effect', async () => {
    const el = createElement('fb-code-editor');
    await nextFrame();

    const bgElements = el.querySelectorAll('.bg-black\\/70');
    expect(bgElements.length).toBe(2); // gutter + code area
  });

  it('renders mock code with syntax highlighting', async () => {
    const el = createElement('fb-code-editor');
    await nextFrame();

    // Check for keyword coloring (var, function, if, else)
    const keywords = el.querySelectorAll('.text-\\[\\#ff7b72\\]');
    expect(keywords.length).toBeGreaterThan(0);
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
});

describe('fb-tft-display', () => {
  it('renders display items', async () => {
    const el = createElement('fb-tft-display');
    await nextFrame();

    expect(el.textContent).toContain('Amsterdam');
    expect(el.textContent).toContain('Button+ Likes');
  });

  it('shows live time', async () => {
    const el = createElement('fb-tft-display');
    await nextFrame();

    // Time should contain a colon (HH:MM format)
    const text = el.textContent || '';
    expect(text).toMatch(/\d+:\d{2}/);
  });
});

describe('fb-button-grid', () => {
  it('renders 4 buttons', async () => {
    const el = createElement('fb-button-grid');
    await nextFrame();

    const buttons = el.querySelectorAll('button');
    expect(buttons.length).toBe(4);
  });

  it('shows button labels', async () => {
    const el = createElement('fb-button-grid');
    await nextFrame();

    expect(el.textContent).toContain('Tolomeo');
    expect(el.textContent).toContain('Green Lamp');
    expect(el.textContent).toContain('Like!');
    expect(el.textContent).toContain('Kantoor');
  });

  it('shows LED indicators', async () => {
    const el = createElement('fb-button-grid');
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
});

describe('fb-console-output', () => {
  it('renders mock output lines', async () => {
    const el = createElement('fb-console-output');
    await nextFrame();

    const lines = el.querySelectorAll('.p-3 > div');
    expect(lines.length).toBeGreaterThanOrEqual(11);
  });

  it('applies correct color classes for input lines', async () => {
    const el = createElement('fb-console-output');
    await nextFrame();

    const inputLines = el.querySelectorAll('.text-\\[\\#4fc3f7\\]');
    expect(inputLines.length).toBeGreaterThan(0);
  });

  it('applies correct color classes for result lines', async () => {
    const el = createElement('fb-console-output');
    await nextFrame();

    const resultLines = el.querySelectorAll('.text-status-green');
    expect(resultLines.length).toBeGreaterThan(0);
  });

  it('applies correct color classes for log lines', async () => {
    const el = createElement('fb-console-output');
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

  it('has input with placeholder', async () => {
    const el = createElement('fb-console-input');
    await nextFrame();

    const input = el.querySelector('input[placeholder="Type JavaScript..."]');
    expect(input).not.toBeNull();
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

  it('renders file drawer sidebar', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();

    expect(el.querySelector('fb-file-drawer')).not.toBeNull();
  });

  it('toggles sidebar on toggle-assets event', async () => {
    const el = createElement('fb-editor-pane');
    await nextFrame();

    // Sidebar should start open (w-[200px])
    const sidebar = el.querySelector('.w-\\[200px\\]');
    expect(sidebar).not.toBeNull();

    // Dispatch toggle event
    window.dispatchEvent(new CustomEvent('toggle-assets'));
    await nextFrame();

    // Sidebar should now be closed (w-0)
    const closed = el.querySelector('.w-0');
    expect(closed).not.toBeNull();

    // Toggle again to open
    window.dispatchEvent(new CustomEvent('toggle-assets'));
    await nextFrame();

    const reopened = el.querySelector('.w-\\[200px\\]');
    expect(reopened).not.toBeNull();
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
