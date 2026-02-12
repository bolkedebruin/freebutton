import { LitElement, html } from 'lit';
import { customElement } from 'lit/decorators.js';

interface ConsoleLine {
  text: string;
  type: 'input' | 'result' | 'error' | 'warning' | 'log' | 'info';
}

const mockLines: ConsoleLine[] = [
  { text: '[14:32:01] Connected to ws://freebutton.local/ws/repl', type: 'log' },
  { text: '> led.count()', type: 'input' },
  { text: '  8', type: 'result' },
  { text: '> led.setColor(0, 255, 0, 0)', type: 'input' },
  { text: '  undefined', type: 'result' },
  { text: '> button.setLabel(5, "Tolomeo")', type: 'input' },
  { text: '  undefined', type: 'result' },
  { text: '> sensor.getValue(0)', type: 'input' },
  { text: '  22.5', type: 'result' },
  { text: '[14:32:15] Temperature changed: 22.5°C', type: 'log' },
  { text: '> mqtt.publish(0, "home/temp", "22.5")', type: 'input' },
  { text: '  undefined', type: 'result' },
];

const colorMap: Record<ConsoleLine['type'], string> = {
  input: 'text-[#4fc3f7]',
  result: 'text-status-green',
  error: 'text-status-red',
  warning: 'text-status-orange',
  log: 'text-gray-100',
  info: 'text-[#4fc3f7]',
};

@customElement('fb-console-output')
export class FbConsoleOutput extends LitElement {
  createRenderRoot() { return this; }

  render() {
    return html`
      <div class="p-3 font-mono text-xs leading-5 bg-bg-default">
        ${mockLines.map(line => html`
          <div class="${colorMap[line.type]}">${line.text}</div>
        `)}
      </div>
    `;
  }
}
