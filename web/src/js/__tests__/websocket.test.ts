/**
 * Tests for the REPL WebSocket client — focused on auto-connecting to the
 * device that served the IDE (same origin).
 */
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { ReplWebSocket, resolveReplWsUrl } from '../websocket';

describe('resolveReplWsUrl', () => {
    it('auto-connects to the loading origin over ws when served from http', () => {
        // IDE loaded from http://freebutton.local → ws://freebutton.local/ws/repl
        const url = resolveReplWsUrl('', { protocol: 'http:', host: 'freebutton.local' });
        expect(url).toBe('ws://freebutton.local/ws/repl');
    });

    it('uses wss when the IDE is served over https', () => {
        const url = resolveReplWsUrl('', { protocol: 'https:', host: 'freebutton.local' });
        expect(url).toBe('wss://freebutton.local/ws/repl');
    });

    it('preserves an explicit host:port from the loading origin', () => {
        const url = resolveReplWsUrl('', { protocol: 'http:', host: '192.168.1.50:8080' });
        expect(url).toBe('ws://192.168.1.50:8080/ws/repl');
    });

    it('maps an explicit http device URL to ws', () => {
        const url = resolveReplWsUrl('http://freebutton.local');
        expect(url).toBe('ws://freebutton.local/ws/repl');
    });

    it('maps an explicit https device URL to wss', () => {
        const url = resolveReplWsUrl('https://device.example.com');
        expect(url).toBe('wss://device.example.com/ws/repl');
    });

    it('ignores surrounding whitespace on an explicit URL', () => {
        const url = resolveReplWsUrl('  http://freebutton.local  ');
        expect(url).toBe('ws://freebutton.local/ws/repl');
    });
});

describe('ReplWebSocket auto-connect', () => {
    let opened: string[];
    let originalWebSocket: typeof WebSocket;
    let originalLocation: Location;

    class FakeWebSocket {
        static OPEN = 1;
        readyState = 0;
        onopen: (() => void) | null = null;
        onmessage: (() => void) | null = null;
        onclose: (() => void) | null = null;
        onerror: (() => void) | null = null;
        constructor(public url: string) {
            opened.push(url);
        }
        close(): void {
            this.readyState = 3;
        }
        send(): void {}
    }

    beforeEach(() => {
        opened = [];
        originalWebSocket = globalThis.WebSocket;
        originalLocation = window.location;
        // Pretend the IDE was served from the device at freebutton.local.
        Object.defineProperty(window, 'location', {
            configurable: true,
            value: { protocol: 'http:', host: 'freebutton.local' },
        });
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        globalThis.WebSocket = FakeWebSocket as any;
    });

    afterEach(() => {
        globalThis.WebSocket = originalWebSocket;
        Object.defineProperty(window, 'location', {
            configurable: true,
            value: originalLocation,
        });
        vi.restoreAllMocks();
    });

    it('connects to the device it was loaded from when called with no URL', () => {
        const ws = new ReplWebSocket();
        ws.connect();
        expect(opened).toEqual(['ws://freebutton.local/ws/repl']);
    });

    it('connects to the loading origin when called with an empty URL', () => {
        const ws = new ReplWebSocket();
        ws.connect('');
        expect(opened).toEqual(['ws://freebutton.local/ws/repl']);
    });
});
