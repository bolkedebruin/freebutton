import { LitElement, html } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { pillGeometry, scrollTopAfterDrag, type PillGeometry } from '../js/pill-scroll.js';

/** A drag has to move this far before it is taken as sideways or up / down. */
const DRAG_SLOP_PX = 4;

/** The pill's classes: shape, then the colours at rest or as a scrollbar. */
const PILL_BASE = 'absolute z-10 rounded-full transition-colors duration-200';
const PILL_IDLE = 'bg-[#3A3A4C] group-hover:bg-[#606068]';
const PILL_SCROLLS = 'bg-[#606068] group-hover:bg-[#8A8A94] cursor-grab';

/**
 * Divider between two panes. A drag across it resizes (the `resize` event
 * carries the delta in px).
 *
 * `data-direction="horizontal"` (panes side by side) with
 * `data-scroll-source="<selector>"`: the pill is also the vertical scrollbar
 * of that pane. The pane's scrolling element is the selected element when it
 * is a `.pane-scroll` itself, else the visible `.pane-scroll` inside it that
 * is not another divider's (no `data-pane`); its own scrollbar is hidden
 * (app.css). Only while that element's content is taller than it: the pill
 * then shows the scroll position and a drag up or down on the pill scrolls.
 * The pill always has the same length. Otherwise it rests at mid height and
 * only resizes.
 */
@customElement('fb-resize-handle')
export class FbResizeHandle extends LitElement {
  createRenderRoot() { return this; }

  @state() private _pill: PillGeometry | null = null;

  /** null: no drag; 'pending': direction not known yet. */
  private _drag: null | 'pending' | 'resize' | 'scroll' = null;
  private _onPill = false;
  private _startX = 0;
  private _startY = 0;
  private _lastPos = 0;
  private _poll = 0;
  private _source: Element | null = null;

  private get _direction(): 'horizontal' | 'vertical' {
    return (this.dataset.direction as 'horizontal' | 'vertical') ?? 'horizontal';
  }

  /** The pane's scrolling element, when this divider is its scrollbar and it is shown. */
  private _scrollTarget(): HTMLElement | null {
    const sel = this.dataset.scrollSource;
    if (!sel || this._direction !== 'horizontal') return null;
    const source = document.querySelector<HTMLElement>(sel);
    this._listenTo(source);
    if (!source) return null;
    if (source.classList.contains('pane-scroll')) return source.offsetParent !== null ? source : null;
    for (const el of source.querySelectorAll<HTMLElement>('.pane-scroll:not([data-pane])')) {
      if (el.offsetParent !== null) return el;
    }
    return null;
  }

  /** Scroll events do not bubble: caught on the way down, at the source (which a render can replace). */
  private _listenTo(source: Element | null) {
    if (source === this._source) return;
    this._source?.removeEventListener('scroll', this._measure, true);
    this._source = source;
    this._source?.addEventListener('scroll', this._measure, true);
  }

  /** Recomputes the pill from the pane; re-renders only when it changed. */
  private _measure = () => {
    const target = this._scrollTarget();
    const track = this.clientHeight;
    const next = target
      ? pillGeometry(track, target.clientHeight, target.scrollHeight, target.scrollTop)
      : null;
    const cur = this._pill;
    if (next === null ? cur !== null
      : !cur || cur.len !== next.len || cur.top !== next.top || cur.scrolls !== next.scrolls) {
      this._pill = next;
    }
  };

  private _onMouseDown = (e: MouseEvent) => {
    e.preventDefault();
    this._startX = e.clientX;
    this._startY = e.clientY;
    this._onPill = !!(e.target as Element | null)?.closest?.('[data-pill]');
    if (this._direction === 'horizontal' && this._onPill && this._pill?.scrolls) {
      this._drag = 'pending';   // sideways resizes, up / down scrolls
    } else {
      this._startResize(this._direction === 'horizontal' ? e.clientX : e.clientY);
    }
    document.body.classList.add('select-none');
    document.addEventListener('mousemove', this._onMouseMove);
    document.addEventListener('mouseup', this._onMouseUp);
  };

  private _startResize(pos: number) {
    this._drag = 'resize';
    this._lastPos = pos;
    document.body.classList.add(this._direction === 'horizontal' ? 'cursor-col-resize' : 'cursor-row-resize');
  }

  private _onMouseMove = (e: MouseEvent) => {
    if (this._drag === 'pending') {
      const dx = Math.abs(e.clientX - this._startX);
      const dy = Math.abs(e.clientY - this._startY);
      if (dx < DRAG_SLOP_PX && dy < DRAG_SLOP_PX) return;
      if (dy > dx) {
        this._drag = 'scroll';
        this._lastPos = this._startY;
      } else {
        this._startResize(this._startX);
      }
    }
    if (this._drag === 'scroll') {
      const target = this._scrollTarget();
      const dy = e.clientY - this._lastPos;
      this._lastPos = e.clientY;
      if (target && dy !== 0) {
        target.scrollTop = scrollTopAfterDrag(this.clientHeight, target.clientHeight, target.scrollHeight, target.scrollTop, dy);
        this._measure();
      }
      return;
    }
    if (this._drag !== 'resize') return;
    const pos = this._direction === 'horizontal' ? e.clientX : e.clientY;
    const delta = pos - this._lastPos;
    this._lastPos = pos;

    if (delta !== 0) {
      this.dispatchEvent(new CustomEvent('resize', {
        detail: { delta },
        bubbles: true,
        composed: true,
      }));
    }
  };

  private _onMouseUp = () => {
    this._drag = null;
    document.body.classList.remove('select-none', 'cursor-col-resize', 'cursor-row-resize');
    document.removeEventListener('mousemove', this._onMouseMove);
    document.removeEventListener('mouseup', this._onMouseUp);
  };

  connectedCallback() {
    super.connectedCallback();
    this.addEventListener('mousedown', this._onMouseDown);
    if (this.dataset.scrollSource && typeof document !== 'undefined') {
      // The pane's content also changes without a scroll event (another
      // view, fonts that load, a window resize), so the pill is measured a
      // few times a second too.
      window.addEventListener('resize', this._measure);
      this._poll = window.setInterval(this._measure, 300);
      this._measure();
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeEventListener('mousedown', this._onMouseDown);
    document.removeEventListener('mousemove', this._onMouseMove);
    document.removeEventListener('mouseup', this._onMouseUp);
    this._listenTo(null);
    if (typeof window !== 'undefined') window.removeEventListener('resize', this._measure);
    if (this._poll) { clearInterval(this._poll); this._poll = 0; }
  }

  render() {
    const isHorizontal = this._direction === 'horizontal';

    if (isHorizontal) {
      const pill = this._pill;
      // Without a pane to scroll the pill is centred by the flex container.
      const place = pill ? `top: ${pill.top}px; height: ${pill.len}px;` : '';
      const pillClass = pill
        ? `${PILL_BASE} w-[8px] ${pill.scrolls ? PILL_SCROLLS : PILL_IDLE}`
        : `${PILL_BASE} w-[8px] h-[64px] ${PILL_IDLE}`;
      return html`
        <div class="w-full h-full flex items-center justify-center relative group">
          <div class="w-px h-full bg-[linear-gradient(to_bottom,transparent_0%,#3A3A4C_20%,#3A3A4C_80%,transparent_100%)] group-hover:bg-[linear-gradient(to_bottom,transparent_0%,#606068_20%,#606068_80%,transparent_100%)] transition-colors duration-200"></div>
          <div class=${pillClass} style=${place} data-pill
            data-scrolls=${pill?.scrolls ? 'true' : 'false'}
            title=${pill?.scrolls ? 'Drag up or down to scroll, sideways to resize' : 'Drag to resize'}></div>
        </div>
      `;
    }

    return html`
      <div class="w-full h-full flex items-center justify-center relative group">
        <div class="h-[0.5px] w-full bg-[linear-gradient(to_right,transparent_0%,#3A3A4C_20%,#3A3A4C_80%,transparent_100%)] group-hover:bg-[linear-gradient(to_right,transparent_0%,#606068_20%,#606068_80%,transparent_100%)] transition-colors duration-200"></div>
        <div class="${PILL_BASE} h-[8px] w-[64px] ${PILL_IDLE}"></div>
      </div>
    `;
  }
}
