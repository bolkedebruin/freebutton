/**
 * WebSocket REPL Client
 * Handles WebSocket connection to the ESP32 for JavaScript REPL
 */
import { getCredentials, presignUrl } from './auth';

export interface WSMessage {
    type: string;
    id?: string;
    success?: boolean;
    value?: string;
    error?: string;
    message?: string;
}

export type ConsoleHandler = (message: string) => void;
export type ConnectionHandler = () => void;

/** Minimal shape of `window.location` needed to derive a ws:// URL. */
export interface WsLocation {
    protocol: string;
    host: string;
}

/**
 * Resolve the REPL WebSocket URL.
 *
 * When `deviceUrl` is empty the IDE was served from the device itself, so we
 * auto-connect to that same origin: loading the IDE from `http://freebutton.local`
 * connects to `ws://freebutton.local/ws/repl` (and `https` → `wss`) with no user
 * action. An explicit `deviceUrl` (e.g. a dev build talking to a remote device)
 * is mapped `http`→`ws` / `https`→`wss` and keeps its `/ws/repl` suffix.
 */
export function resolveReplWsUrl(
    deviceUrl: string,
    loc: WsLocation = window.location,
): string {
    const trimmed = deviceUrl.trim();
    if (trimmed) {
        return trimmed.replace(/^http/, 'ws') + '/ws/repl';
    }
    const wsProtocol = loc.protocol === 'https:' ? 'wss:' : 'ws:';
    return `${wsProtocol}//${loc.host}/ws/repl`;
}

interface PendingRequest {
    resolve: (result: { success: boolean; value?: string; error?: string }) => void;
    reject: (error: Error) => void;
    timeout: number;
}

/**
 * WebSocket client for REPL communication
 */
export class ReplWebSocket {
    private ws: WebSocket | null = null;
    private deviceUrl: string = '';
    private reconnectTimer: number | null = null;
    private reconnectDelay: number = 3000;
    private consoleHandlers: ConsoleHandler[] = [];
    private connectHandlers: ConnectionHandler[] = [];
    private disconnectHandlers: ConnectionHandler[] = [];
    private pendingRequests: Map<string, PendingRequest> = new Map();
    private _isConnected: boolean = false;
    private _wantConnection: boolean = false;
    /** Bumped by connect/disconnect: a presign that finishes late is dropped. */
    private generation: number = 0;
    private _everOpened: boolean = false;

    /**
     * Connect to the device WebSocket endpoint.
     *
     * With no argument (or an empty string) the client auto-connects to the
     * device that served the IDE — the same origin the page was loaded from.
     */
    connect(deviceUrl: string = ''): void {
        this.disconnect();
        this.deviceUrl = deviceUrl;
        this._wantConnection = true;
        const generation = ++this.generation;

        // Derive the ws(s):// endpoint (same origin when deviceUrl is empty).
        const wsUrl = resolveReplWsUrl(deviceUrl);

        // The device authenticates the upgrade with a presigned query string
        // (browsers cannot set headers on a WebSocket). Signed right before
        // each (re)connect, since the URL expires.
        if (!getCredentials(wsUrl)) {
            this.open(wsUrl, wsUrl);
            return;
        }
        presignUrl(wsUrl).then(
            (signedUrl) => {
                if (generation === this.generation && this._wantConnection) this.open(signedUrl, wsUrl);
            },
            (err: unknown) => {
                console.error('Failed to sign the REPL WebSocket URL:', err);
                if (generation === this.generation && this._wantConnection) this.open(wsUrl, wsUrl);
            },
        );
    }

    /** Reconnect to the same device (e.g. after new credentials were saved). */
    reconnect(): void {
        if (this._wantConnection) this.connect(this.deviceUrl);
    }

    /** True when a connection was made at least once (vs. refused from the start). */
    get everOpened(): boolean {
        return this._everOpened;
    }

    /** @param displayUrl logged instead of the signed URL */
    private open(url: string, displayUrl: string): void {
        try {
            this.ws = new WebSocket(url);

            this.ws.onopen = (): void => {
                console.log('REPL WebSocket connected to', displayUrl);
                this._isConnected = true;
                this._everOpened = true;
                this.connectHandlers.forEach(h => h());
            };

            this.ws.onmessage = (event: MessageEvent): void => {
                this.handleMessage(event.data as string);
            };

            this.ws.onclose = (): void => {
                console.log('REPL WebSocket disconnected');
                this._isConnected = false;
                this.disconnectHandlers.forEach(h => h());
                this.scheduleReconnect();
            };

            this.ws.onerror = (error: Event): void => {
                console.error('REPL WebSocket error:', error);
            };
        } catch (err) {
            console.error('Failed to create WebSocket:', err);
        }
    }

    /**
     * Disconnect from the WebSocket
     */
    disconnect(): void {
        if (this.reconnectTimer !== null) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }

        if (this.ws) {
            this.ws.onclose = null; // Prevent reconnect on manual disconnect
            this.ws.close();
            this.ws = null;
        }

        this._isConnected = false;
        this._wantConnection = false;
        this.deviceUrl = '';
        this.generation++;

        // Reject all pending requests
        this.pendingRequests.forEach((request) => {
            clearTimeout(request.timeout);
            request.reject(new Error('WebSocket disconnected'));
        });
        this.pendingRequests.clear();
    }

    /**
     * Check if connected
     */
    get isConnected(): boolean {
        return this._isConnected && this.ws?.readyState === WebSocket.OPEN;
    }

    /**
     * The base URL this client connects to, for display (no /ws/repl suffix).
     * For an auto-connect (empty deviceUrl) this is derived from the page origin,
     * e.g. loading the IDE from http://freebutton.local → "ws://freebutton.local".
     */
    get connectionBaseUrl(): string {
        const trimmed = this.deviceUrl.trim();
        if (trimmed) {
            return trimmed.replace(/^http/, 'ws');
        }
        const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        return `${wsProtocol}//${window.location.host}`;
    }

    /**
     * Evaluate JavaScript code on the device
     */
    async evaluate(code: string): Promise<{ success: boolean; value?: string; error?: string }> {
        if (!this.isConnected) {
            return { success: false, error: 'Not connected to device' };
        }

        const id = this.generateId();

        return new Promise((resolve, reject) => {
            const timeout = window.setTimeout(() => {
                this.pendingRequests.delete(id);
                resolve({ success: false, error: 'Evaluation timeout (30s)' });
            }, 30000);

            this.pendingRequests.set(id, { resolve, reject, timeout });

            this.send({
                type: 'eval',
                id,
                code
            });
        });
    }

    /**
     * Register handler for console output
     */
    onConsole(handler: ConsoleHandler): void {
        this.consoleHandlers.push(handler);
    }

    /**
     * Register handler for connection events
     */
    onConnect(handler: ConnectionHandler): void {
        this.connectHandlers.push(handler);
    }

    /**
     * Register handler for disconnection events
     */
    onDisconnect(handler: ConnectionHandler): void {
        this.disconnectHandlers.push(handler);
    }

    /**
     * Send ping to keep connection alive
     */
    ping(): void {
        this.send({ type: 'ping' });
    }

    private handleMessage(data: string): void {
        try {
            const msg = JSON.parse(data) as WSMessage;

            switch (msg.type) {
                case 'result':
                    if (msg.id) {
                        const pending = this.pendingRequests.get(msg.id);
                        if (pending) {
                            clearTimeout(pending.timeout);
                            this.pendingRequests.delete(msg.id);
                            pending.resolve({
                                success: msg.success ?? false,
                                value: msg.value,
                                error: msg.error
                            });
                        }
                    }
                    break;

                case 'console':
                    if (msg.message) {
                        this.consoleHandlers.forEach(h => h(msg.message!));
                    }
                    break;

                case 'connected':
                    console.log('REPL server confirmed connection');
                    break;

                case 'pong':
                    // Keep-alive response
                    break;

                default:
                    console.log('Unknown message type:', msg.type);
            }
        } catch (err) {
            console.error('Failed to parse WebSocket message:', err);
        }
    }

    private send(msg: object): void {
        if (this.ws?.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(msg));
        }
    }

    private scheduleReconnect(): void {
        // Reconnect whenever a connection is still wanted — including the
        // same-origin auto-connect case where deviceUrl is an empty string.
        if (this._wantConnection && this.reconnectTimer === null) {
            console.log(`Reconnecting in ${this.reconnectDelay / 1000}s...`);
            this.reconnectTimer = window.setTimeout(() => {
                this.reconnectTimer = null;
                if (this._wantConnection) {
                    this.connect(this.deviceUrl);
                }
            }, this.reconnectDelay);
        }
    }

    private generateId(): string {
        return Math.random().toString(36).substring(2, 10);
    }
}

// Singleton instance
let wsInstance: ReplWebSocket | null = null;

/**
 * Get the singleton WebSocket instance
 */
export function getWebSocket(): ReplWebSocket {
    if (!wsInstance) {
        wsInstance = new ReplWebSocket();
    }
    return wsInstance;
}
