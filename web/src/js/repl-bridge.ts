/**
 * REPL Bridge - Connects WebSocket events to app-state events
 */
import { on, emit } from './app-state';
import { getWebSocket } from './websocket';

let initialized = false;

export function initReplBridge(): void {
  if (initialized) return;
  initialized = true;

  const ws = getWebSocket();

  // Forward device console output to app-state
  ws.onConsole((message: string) => {
    const clean = message.replace(/\n$/, '');
    if (clean) {
      emit('console-line', { text: clean, type: 'log' });
    }
  });

  // Forward connection state changes. Include the resolved base URL so the UI
  // can show where an auto-connect actually connected to.
  ws.onConnect(() => {
    emit('connection-change', { connected: true, url: ws.connectionBaseUrl });
    emit('console-line', { text: `Connected to ${ws.connectionBaseUrl}`, type: 'info' });
  });

  ws.onDisconnect(() => {
    emit('connection-change', { connected: false, url: '' });
    emit('console-line', { text: 'Disconnected from device', type: 'info' });
  });

  // Handle connection requests from toolbar
  on('connection-request', (e) => {
    ws.connect(e.detail.url);
  });

  on('disconnect-request', () => {
    ws.disconnect();
  });

  // Auto-connect to the device that served this IDE. When loaded from
  // http://freebutton.local the REPL connects to ws://freebutton.local/ws/repl
  // with no user action; the toolbar input can still override with an explicit URL.
  ws.connect('');

  // Handle eval requests from console input
  on('console-eval', async (e) => {
    const code = e.detail.code;
    if (!ws.isConnected) {
      emit('console-line', { text: 'Not connected to device', type: 'error' });
      return;
    }
    const result = await ws.evaluate(code);
    if (result.success) {
      if (result.value && result.value !== 'undefined') {
        emit('console-line', { text: result.value, type: 'result' });
      }
    } else {
      emit('console-line', { text: result.error || 'Unknown error', type: 'error' });
    }
  });
}
