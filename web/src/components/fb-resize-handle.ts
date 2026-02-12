import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';

@customElement('fb-resize-handle')
export class FbResizeHandle extends LitElement {
  createRenderRoot() { return this; }

  private _dragging = false;
  private _lastPos = 0;

  private get _direction(): 'horizontal' | 'vertical' {
    return (this.dataset.direction as 'horizontal' | 'vertical') ?? 'horizontal';
  }

  private _onMouseDown = (e: MouseEvent) => {
    e.preventDefault();
    this._dragging = true;
    this._lastPos = this._direction === 'horizontal' ? e.clientX : e.clientY;

    const cursorClass = this._direction === 'horizontal' ? 'cursor-col-resize' : 'cursor-row-resize';
    document.body.classList.add('select-none', cursorClass);

    document.addEventListener('mousemove', this._onMouseMove);
    document.addEventListener('mouseup', this._onMouseUp);
  };

  private _onMouseMove = (e: MouseEvent) => {
    if (!this._dragging) return;
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
    this._dragging = false;
    const cursorClass = this._direction === 'horizontal' ? 'cursor-col-resize' : 'cursor-row-resize';
    document.body.classList.remove('select-none', cursorClass);

    document.removeEventListener('mousemove', this._onMouseMove);
    document.removeEventListener('mouseup', this._onMouseUp);
  };

  connectedCallback() {
    super.connectedCallback();
    this.addEventListener('mousedown', this._onMouseDown);
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.removeEventListener('mousedown', this._onMouseDown);
    document.removeEventListener('mousemove', this._onMouseMove);
    document.removeEventListener('mouseup', this._onMouseUp);
  }

  render() {
    const isHorizontal = this._direction === 'horizontal';

    if (isHorizontal) {
      return html`
        <div class="w-full h-full flex items-center justify-center relative group">
          <div class="w-px h-full bg-[linear-gradient(to_bottom,transparent_0%,#3A3A4C_20%,#3A3A4C_80%,transparent_100%)] group-hover:bg-[linear-gradient(to_bottom,transparent_0%,#606068_20%,#606068_80%,transparent_100%)] transition-colors duration-200"></div>
          <div class="absolute z-10 w-[8px] h-[64px] rounded-full bg-[#3A3A4C] group-hover:bg-[#606068] transition-colors duration-200"></div>
        </div>
      `;
    }

    return html`
      <div class="w-full h-full flex items-center justify-center relative group">
        <div class="h-[0.5px] w-full bg-[linear-gradient(to_right,transparent_0%,#3A3A4C_20%,#3A3A4C_80%,transparent_100%)] group-hover:bg-[linear-gradient(to_right,transparent_0%,#606068_20%,#606068_80%,transparent_100%)] transition-colors duration-200"></div>
        <div class="absolute z-10 h-[8px] w-[64px] rounded-full bg-[#3A3A4C] group-hover:bg-[#606068] transition-colors duration-200"></div>
      </div>
    `;
  }
}
