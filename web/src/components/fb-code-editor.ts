import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';

@customElement('fb-code-editor')
export class FbCodeEditor extends LitElement {
  createRenderRoot() { return this; }

  render() {
    const lineCount = 17;

    return html`
      <div class="flex h-full font-mono text-sm">
        <div class="w-12 py-3 text-right pr-3 select-none bg-black/70 text-gray-200 border-r border-border-default">
          ${Array.from({ length: lineCount }, (_, i) => html`<div>${i + 1}</div>`)}
        </div>
        <div class="flex-1 py-3 pl-4 bg-black/70 overflow-auto custom-scrollbar">
          ${this._renderCode()}
        </div>
      </div>
    `;
  }

  private _renderCode() {
    // prettier-ignore
    return html`
      <div><span class="text-[#8b949e]">// LED Blink Example</span></div>
      <div><span class="text-[#ff7b72]">var</span> <span class="text-text-default">position</span> <span class="text-text-default">=</span> <span class="text-[#79c0ff]">0</span><span class="text-text-default">;</span></div>
      <div><span class="text-[#ff7b72]">var</span> <span class="text-text-default">isOn</span> <span class="text-text-default">=</span> <span class="text-[#ff7b72]">false</span><span class="text-text-default">;</span></div>
      <div>&nbsp;</div>
      <div><span class="text-[#ff7b72]">function</span> <span class="text-[#d2a8ff]">toggleLed</span><span class="text-text-default">() {</span></div>
      <div><span class="text-text-default">  </span><span class="text-[#ff7b72]">if</span> <span class="text-text-default">(isOn) {</span></div>
      <div><span class="text-text-default">    </span><span class="text-[#7ee787]">led</span><span class="text-text-default">.</span><span class="text-[#d2a8ff]">off</span><span class="text-text-default">(position);</span></div>
      <div><span class="text-text-default">  } </span><span class="text-[#ff7b72]">else</span> <span class="text-text-default">{</span></div>
      <div><span class="text-text-default">    </span><span class="text-[#7ee787]">led</span><span class="text-text-default">.</span><span class="text-[#d2a8ff]">setColor</span><span class="text-text-default">(position, </span><span class="text-[#79c0ff]">255</span><span class="text-text-default">, </span><span class="text-[#79c0ff]">0</span><span class="text-text-default">, </span><span class="text-[#79c0ff]">0</span><span class="text-text-default">);</span></div>
      <div><span class="text-text-default">  }</span></div>
      <div><span class="text-text-default">  isOn = !isOn;</span></div>
      <div><span class="text-text-default">  </span><span class="text-[#7ee787]">console</span><span class="text-text-default">.</span><span class="text-[#d2a8ff]">log</span><span class="text-text-default">(</span><span class="text-[#a5d6ff]">"LED "</span> <span class="text-text-default">+ position + </span><span class="text-[#a5d6ff]">": "</span> <span class="text-text-default">+ isOn);</span></div>
      <div><span class="text-text-default">}</span></div>
      <div>&nbsp;</div>
      <div><span class="text-[#d2a8ff]">setTimeout</span><span class="text-text-default">(toggleLed, </span><span class="text-[#79c0ff]">1000</span><span class="text-text-default">);</span></div>
      <div>&nbsp;</div>
      <div>&nbsp;</div>
    `;
  }
}
