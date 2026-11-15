/**
 * Menus and small popovers, drawn in one layer at the end of <body>.
 *
 * A menu that is positioned inside its pane is painted with that pane: a
 * pane that comes later in the page (or any ancestor with a backdrop filter,
 * a transform or `overflow: hidden`) covers or clips it. The toolbar's
 * "more" menu was hidden behind the file header and the editor that way.
 * The layer here is a child of <body>, fixed to the viewport, above the
 * panes and below the dialogs, so no pane can cover or clip what it holds.
 *
 *   openPopover({ anchor, content, ... })  one at a time; the owner calls
 *                                          update() when its state changes
 *   placeBox(anchor, size, viewport, ...)  where it goes: next to the
 *                                          anchor, flipped or moved to stay
 *                                          inside the viewport (pure)
 *
 * Closes on Escape, a press outside, or when the focus moves out. The
 * listeners on the document exist only while a popover is open. The focus
 * goes back to where it was (or `returnFocus`) on close.
 */
import { nothing, render, type TemplateResult } from 'lit';

export interface Box { left: number; top: number; width: number; height: number }

/**
 * Side of the anchor and the edge that lines up with it: 'bottom-start' is
 * below the anchor, left edges aligned (a drop-down menu); 'left-start' is
 * left of it, top edges aligned.
 */
export type Placement = 'bottom-start' | 'bottom-end' | 'left-start' | 'right-start';

export type CloseReason = 'escape' | 'outside' | 'blur' | 'api';

const GAP = 6;
const MARGIN = 8;

/**
 * Top-left corner for a box of `size` next to `anchor`. It goes to the
 * other side of the anchor when it does not fit on the asked one and fits
 * there, and is then moved to lie inside the viewport (`margin` from its
 * edges; a box larger than the viewport starts at the margin).
 */
export function placeBox(
  anchor: Box, size: { width: number; height: number }, viewport: { width: number; height: number },
  placement: Placement = 'bottom-start', gap = GAP, margin = MARGIN,
): { left: number; top: number } {
  let left: number;
  let top: number;
  if (placement === 'left-start' || placement === 'right-start') {
    const leftOf = anchor.left - gap - size.width;
    const rightOf = anchor.left + anchor.width + gap;
    const fitsLeft = leftOf >= margin;
    const fitsRight = rightOf + size.width <= viewport.width - margin;
    left = placement === 'left-start' ? (fitsLeft || !fitsRight ? leftOf : rightOf) : (fitsRight || !fitsLeft ? rightOf : leftOf);
    top = anchor.top;
  } else {
    const below = anchor.top + anchor.height + gap;
    const above = anchor.top - gap - size.height;
    top = below + size.height <= viewport.height - margin || above < margin ? below : above;
    left = placement === 'bottom-end' ? anchor.left + anchor.width - size.width : anchor.left;
  }
  const clamp = (v: number, max: number) => Math.max(margin, Math.min(v, max));
  return {
    left: Math.round(clamp(left, viewport.width - margin - size.width)),
    top: Math.round(clamp(top, viewport.height - margin - size.height)),
  };
}

export interface PopoverOptions {
  /**
   * What it belongs to: an element (measured at every placement), a box in
   * viewport px, or a function that finds the element (one that is drawn
   * again while the popover is open); null = stay where it is.
   */
  anchor: Element | Box | (() => Element | null);
  placement?: Placement;
  /** The content; called again on update(). */
  content: () => TemplateResult;
  /** 'menu': the arrow keys, Home and End move between its menu items. */
  role?: 'menu' | 'dialog';
  label?: string;
  testid?: string;
  /** Classes of the box (the size; the look is the IDE's). */
  className?: string;
  onClose?: (reason: CloseReason) => void;
  /** Where the focus goes on close; default: the anchor element, else the element that had it at open; null: nowhere. */
  returnFocus?: HTMLElement | (() => HTMLElement | null) | null;
}

export interface PopoverHandle {
  readonly el: HTMLElement;
  readonly isOpen: boolean;
  /** Draw the content again and place it again. */
  update(): void;
  close(reason?: CloseReason): void;
}

const LOOK = 'rounded-xl border border-border-default bg-bg-primary shadow-2xl text-text-default';
const FOCUSABLE = 'button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])';
const MENU_ITEM = '[role^="menuitem"]:not([disabled])';

let current: PopoverHandle | null = null;

function layer(): HTMLElement {
  let el = document.getElementById('fb-popover-layer');
  if (!el) {
    el = document.createElement('div');
    el.id = 'fb-popover-layer';
    // Above the panes and the settings modal (z 50), below the dialogs (z 60).
    el.style.cssText = 'position: fixed; inset: 0; z-index: 55; pointer-events: none;';
    document.body.appendChild(el);
  }
  return el;
}

function anchorBox(anchor: Element | Box | null): Box | null {
  if (!anchor || !(anchor instanceof Element)) return anchor;
  if (!anchor.isConnected) return null;
  const r = anchor.getBoundingClientRect();
  return { left: r.left, top: r.top, width: r.width, height: r.height };
}

/** The open popover, if any. */
export function currentPopover(): PopoverHandle | null {
  return current;
}

/** Open a popover; one that is open closes first. */
export function openPopover(opts: PopoverOptions): PopoverHandle {
  current?.close('api');
  const before = document.activeElement instanceof HTMLElement ? document.activeElement : null;
  const el = document.createElement('div');
  el.className = `${LOOK} ${opts.className ?? ''}`;
  el.style.cssText = 'position: fixed; left: 0; top: 0; pointer-events: auto; max-height: calc(100vh - 16px); max-width: calc(100vw - 16px); overflow-y: auto; visibility: hidden;';
  el.tabIndex = -1;
  el.setAttribute('role', opts.role ?? 'dialog');
  if (opts.label) el.setAttribute('aria-label', opts.label);
  if (opts.testid) el.setAttribute('data-testid', opts.testid);
  let open = true;

  const anchorEl = () => (typeof opts.anchor === 'function' ? opts.anchor() : opts.anchor);

  const place = () => {
    const box = anchorBox(anchorEl());
    if (!box) { el.style.visibility = ''; return; }
    const r = el.getBoundingClientRect();
    const at = placeBox(box, { width: r.width, height: r.height },
      { width: window.innerWidth, height: window.innerHeight }, opts.placement);
    el.style.left = `${at.left}px`;
    el.style.top = `${at.top}px`;
    el.style.visibility = '';
  };

  const draw = () => {
    render(opts.content(), el);
    place();
  };

  const inside = (node: EventTarget | null) => node instanceof Node && el.contains(node);
  // A function anchor is an item of the page, not the popover's own button.
  const onAnchor = (node: EventTarget | null) =>
    opts.anchor instanceof Element && node instanceof Node && opts.anchor.contains(node);

  const onPointerDown = (e: Event) => {
    // A press on the anchor is the anchor's to handle (a menu button closes its menu).
    if (inside(e.target) || onAnchor(e.target)) return;
    handle.close('outside');
  };

  const onKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      handle.close('escape');
      return;
    }
    if (opts.role !== 'menu' || !inside(e.target)) return;
    const items = Array.from(el.querySelectorAll<HTMLElement>(MENU_ITEM));
    if (items.length === 0) return;
    const at = items.indexOf(document.activeElement as HTMLElement);
    let next = -1;
    if (e.key === 'ArrowDown') next = (at + 1) % items.length;
    else if (e.key === 'ArrowUp') next = (at <= 0 ? items.length : at) - 1;
    else if (e.key === 'Home') next = 0;
    else if (e.key === 'End') next = items.length - 1;
    if (next < 0) return;
    e.preventDefault();
    items[next].focus();
  };

  // Tab out of it: it closes, the focus stays where Tab put it.
  const onFocusOut = (e: FocusEvent) => {
    const to = e.relatedTarget;
    if (to instanceof Node && !inside(to) && !onAnchor(to)) handle.close('blur');
  };

  const handle: PopoverHandle = {
    el,
    get isOpen() { return open; },
    update() { if (open) draw(); },
    close(reason: CloseReason = 'api') {
      if (!open) return;
      open = false;
      if (current === handle) current = null;
      document.removeEventListener('pointerdown', onPointerDown, true);
      document.removeEventListener('keydown', onKeyDown, true);
      window.removeEventListener('resize', place);
      el.removeEventListener('focusout', onFocusOut);
      const hadFocus = inside(document.activeElement);
      render(nothing, el);
      el.remove();
      // Default: the button it hangs on (a click does not focus a button in every browser), else what had the focus.
      const fallback = opts.anchor instanceof HTMLElement ? opts.anchor : before;
      const target = typeof opts.returnFocus === 'function' ? opts.returnFocus()
        : opts.returnFocus === undefined ? fallback : opts.returnFocus;
      if (reason === 'escape' || reason === 'api') {
        if (hadFocus || reason === 'escape') target?.focus();
      } else if (reason === 'outside') {
        // The press may put the focus on what was pressed: only when it lands nowhere.
        setTimeout(() => {
          const a = document.activeElement;
          if (!a || a === document.body) target?.focus();
        }, 0);
      }
      opts.onClose?.(reason);
    },
  };

  layer().appendChild(el);
  current = handle;
  draw();
  document.addEventListener('pointerdown', onPointerDown, true);
  document.addEventListener('keydown', onKeyDown, true);
  window.addEventListener('resize', place);
  el.addEventListener('focusout', onFocusOut);
  (el.querySelector<HTMLElement>('[data-autofocus]') ?? el.querySelector<HTMLElement>(FOCUSABLE) ?? el).focus();
  return handle;
}
