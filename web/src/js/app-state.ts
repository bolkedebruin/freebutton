/**
 * App State - Event-based reactive communication layer
 * Components use emit() and on() to communicate via typed custom events.
 * No central state object - each component manages its own state.
 */

export type LogLevel = 'log' | 'info' | 'warn' | 'error' | 'result' | 'input';

export interface ConsoleLine {
  text: string;
  type: LogLevel;
}

export interface FileEntry {
  name: string;
  size: number;
  type: 'js' | 'json' | 'svg' | 'png' | 'bin' | 'other';
}

export interface DisplayItem {
  x: number;
  y: number;
  width: number;
  height: number;
  type: 'text' | 'number' | 'bar' | 'icon';
  label?: string;
  value?: string;
  unit?: string;
  fontSize?: number;
  color?: string;
}

export interface ButtonConfig {
  position: number;
  label: string;
  topLabel: string;
  ledOn: boolean;
  ledColor: string;
  page: number;
}

export interface PageConfig {
  number: number;
  displayItems: DisplayItem[];
  buttons: ButtonConfig[];
}

// Typed custom event map
declare global {
  interface WindowEventMap {
    // Editor events
    'editor-ready': CustomEvent<void>;
    'editor-content-change': CustomEvent<{ content: string }>;
    'cursor-position': CustomEvent<{ line: number; col: number }>;
    'validation-result': CustomEvent<{ valid: boolean; errors: string[]; errorCount: number }>;

    // Compiler events
    'compile-start': CustomEvent<void>;
    'compile-result': CustomEvent<{ success: boolean; bytecode?: Uint8Array; error?: string; size?: number }>;
    'compiler-ready': CustomEvent<void>;

    // Connection events
    'connection-change': CustomEvent<{ connected: boolean; url: string }>;
    'connection-request': CustomEvent<{ url: string }>;
    'disconnect-request': CustomEvent<void>;

    // Console events
    'console-line': CustomEvent<ConsoleLine>;
    'console-clear': CustomEvent<void>;
    'console-eval': CustomEvent<{ code: string }>;

    // Device events
    'device-config': CustomEvent<{ pages: PageConfig[] }>;

    // File events
    'files-change': CustomEvent<{ files: FileEntry[]; used: number; total: number }>;
    'file-select': CustomEvent<{ name: string }>;
    'file-delete': CustomEvent<{ name: string }>;
    'file-upload': CustomEvent<{ file: File }>;

    // Navigation events
    'toggle-assets': CustomEvent<void>;
    'sidebar-panel': CustomEvent<{ panel: 'assets' | 'examples' }>;
    'load-example': CustomEvent<{ name: string }>;
    'nav-select': CustomEvent<{ item: string }>;
  }
}

/**
 * Dispatch a typed custom event on window
 */
export function emit<K extends keyof WindowEventMap>(
  type: K,
  ...args: WindowEventMap[K] extends CustomEvent<void> ? [] : [WindowEventMap[K] extends CustomEvent<infer D> ? D : never]
): void {
  window.dispatchEvent(new CustomEvent(type, { detail: args[0] }));
}

/**
 * Listen for a typed custom event on window. Returns unsubscribe function.
 */
export function on<K extends keyof WindowEventMap>(
  type: K,
  handler: (event: WindowEventMap[K]) => void
): () => void {
  const listener = handler as EventListener;
  window.addEventListener(type, listener);
  return () => window.removeEventListener(type, listener);
}
