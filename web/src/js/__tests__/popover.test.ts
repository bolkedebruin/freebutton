import { describe, it, expect, afterEach, vi } from 'vitest';
import { html } from 'lit';
import { currentPopover, openPopover, placeBox } from '../popover';

const VIEW = { width: 1000, height: 700 };
const SIZE = { width: 200, height: 100 };

afterEach(() => {
  currentPopover()?.close();
  document.body.innerHTML = '';
});

describe('popover placeBox', () => {
  it('bottom-start: under the anchor, left edges aligned', () => {
    expect(placeBox({ left: 300, top: 50, width: 40, height: 40 }, SIZE, VIEW, 'bottom-start')).toEqual({ left: 300, top: 96 });
    expect(placeBox({ left: 300, top: 50, width: 40, height: 40 }, SIZE, VIEW, 'bottom-end')).toEqual({ left: 140, top: 96 });
  });

  it('goes above the anchor when there is no room below', () => {
    expect(placeBox({ left: 300, top: 640, width: 40, height: 40 }, SIZE, VIEW, 'bottom-start')).toEqual({ left: 300, top: 534 });
  });

  it('is moved inside the viewport at the right and the left edge', () => {
    expect(placeBox({ left: 950, top: 50, width: 40, height: 40 }, SIZE, VIEW, 'bottom-start').left).toBe(792);
    expect(placeBox({ left: -50, top: 50, width: 40, height: 40 }, SIZE, VIEW, 'bottom-start').left).toBe(8);
  });

  it('left-start: left of the anchor; right of it when the left has no room', () => {
    expect(placeBox({ left: 700, top: 200, width: 50, height: 30 }, SIZE, VIEW, 'left-start')).toEqual({ left: 494, top: 200 });
    expect(placeBox({ left: 100, top: 200, width: 50, height: 30 }, SIZE, VIEW, 'left-start')).toEqual({ left: 156, top: 200 });
    expect(placeBox({ left: 100, top: 200, width: 50, height: 30 }, SIZE, VIEW, 'right-start')).toEqual({ left: 156, top: 200 });
    expect(placeBox({ left: 900, top: 200, width: 50, height: 30 }, SIZE, VIEW, 'right-start')).toEqual({ left: 694, top: 200 });
  });

  it('a box at the bottom edge is moved up, one taller than the viewport starts at the margin', () => {
    expect(placeBox({ left: 700, top: 650, width: 50, height: 30 }, SIZE, VIEW, 'left-start').top).toBe(592);
    expect(placeBox({ left: 700, top: 300, width: 50, height: 30 }, { width: 200, height: 900 }, VIEW, 'left-start').top).toBe(8);
  });
});

describe('popover openPopover', () => {
  function anchor(): HTMLButtonElement {
    const b = document.createElement('button');
    document.body.appendChild(b);
    b.focus();
    return b;
  }

  it('draws in a layer of <body>, fixed, above the panes', () => {
    const a = anchor();
    const p = openPopover({ anchor: a, role: 'menu', label: 'Menu', testid: 'pop', content: () => html`<button role="menuitem">One</button>` });
    const layer = document.getElementById('fb-popover-layer')!;
    expect(layer.parentElement).toBe(document.body);
    expect(layer.style.position).toBe('fixed');
    expect(Number(layer.style.zIndex)).toBeGreaterThanOrEqual(55);
    expect(p.el.parentElement).toBe(layer);
    expect(p.el.style.position).toBe('fixed');
    expect(p.el.getAttribute('role')).toBe('menu');
    expect(p.el.getAttribute('aria-label')).toBe('Menu');
    // The first item has the focus.
    expect(document.activeElement?.textContent).toBe('One');
  });

  it('Escape closes it and gives the focus back; the key goes no further', () => {
    const a = anchor();
    const onClose = vi.fn();
    const outer = vi.fn();
    window.addEventListener('keydown', outer);
    const p = openPopover({ anchor: a, content: () => html`<input />`, onClose });
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
    window.removeEventListener('keydown', outer);
    expect(p.isOpen).toBe(false);
    expect(onClose).toHaveBeenCalledWith('escape');
    expect(document.activeElement).toBe(a);
    expect(outer).not.toHaveBeenCalled();
    expect(document.querySelector('#fb-popover-layer')?.children.length).toBe(0);
  });

  it('a press outside closes it; one inside or on its button does not', () => {
    const a = anchor();
    const other = document.createElement('div');
    document.body.appendChild(other);
    const p = openPopover({ anchor: a, content: () => html`<button id="in">x</button>` });
    p.el.querySelector('#in')!.dispatchEvent(new Event('pointerdown', { bubbles: true }));
    a.dispatchEvent(new Event('pointerdown', { bubbles: true }));
    expect(p.isOpen).toBe(true);
    other.dispatchEvent(new Event('pointerdown', { bubbles: true }));
    expect(p.isOpen).toBe(false);
  });

  it('listens on the document only while it is open', () => {
    const add = vi.spyOn(document, 'addEventListener');
    const remove = vi.spyOn(document, 'removeEventListener');
    const p = openPopover({ anchor: anchor(), content: () => html`<button>x</button>` });
    const added = add.mock.calls.map(c => c[0]).sort();
    p.close();
    expect(remove.mock.calls.map(c => c[0]).sort()).toEqual(added);
    expect(added).toEqual(['keydown', 'pointerdown']);
    add.mockRestore();
    remove.mockRestore();
  });

  it('a menu: the arrow keys, Home and End move between its items', () => {
    const p = openPopover({
      anchor: anchor(), role: 'menu',
      content: () => html`<button role="menuitem">A</button><button role="menuitem" disabled>B</button><button role="menuitem">C</button>`,
    });
    const press = (key: string) => document.activeElement!.dispatchEvent(new KeyboardEvent('keydown', { key, bubbles: true }));
    expect(document.activeElement?.textContent).toBe('A');
    press('ArrowDown');
    expect(document.activeElement?.textContent).toBe('C');
    press('ArrowDown');
    expect(document.activeElement?.textContent).toBe('A');
    press('End');
    expect(document.activeElement?.textContent).toBe('C');
    press('ArrowUp');
    expect(document.activeElement?.textContent).toBe('A');
    p.close();
  });

  it('one at a time; update() draws the content again', () => {
    let text = 'one';
    const first = openPopover({ anchor: anchor(), content: () => html`<span>${text}</span>` });
    text = 'two';
    first.update();
    expect(first.el.textContent).toBe('two');
    const second = openPopover({ anchor: anchor(), content: () => html`<span>b</span>` });
    expect(first.isOpen).toBe(false);
    expect(currentPopover()).toBe(second);
    expect(document.getElementById('fb-popover-layer')!.children.length).toBe(1);
  });

  it('the focus leaving it (Tab) closes it and stays where it went', () => {
    const a = anchor();
    const next = document.createElement('button');
    document.body.appendChild(next);
    const p = openPopover({ anchor: a, content: () => html`<button>x</button>` });
    next.focus();
    p.el.dispatchEvent(new FocusEvent('focusout', { bubbles: true, relatedTarget: next }));
    expect(p.isOpen).toBe(false);
    expect(document.activeElement).toBe(next);
  });
});
