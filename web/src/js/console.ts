/**
 * Console Panel Controller
 * Manages the console output panel and REPL input
 */

import { getWebSocket } from './websocket';
import { initConsoleAutocomplete, isAutocompleteVisible } from './console-autocomplete';

/**
 * Check if code is incomplete (unclosed brackets, strings, etc.)
 * Returns true if code needs more input before execution
 */
export function isIncompleteCode(code: string): boolean {
    let braces = 0;      // {}
    let parens = 0;      // ()
    let brackets = 0;    // []
    let inString: string | null = null;  // Track quote type
    let inTemplate = false;  // Template literal
    let escaped = false;

    for (let i = 0; i < code.length; i++) {
        const char = code[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (char === '\\') {
            escaped = true;
            continue;
        }

        // Handle strings
        if (inString) {
            if (char === inString) {
                inString = null;
            }
            continue;
        }

        if (inTemplate) {
            if (char === '`') {
                inTemplate = false;
            }
            continue;
        }

        // Start of string/template
        if (char === '"' || char === "'") {
            inString = char;
            continue;
        }
        if (char === '`') {
            inTemplate = true;
            continue;
        }

        // Skip single-line comments
        if (char === '/' && code[i + 1] === '/') {
            const newlineIdx = code.indexOf('\n', i);
            if (newlineIdx === -1) break;
            i = newlineIdx;
            continue;
        }

        // Skip multi-line comments
        if (char === '/' && code[i + 1] === '*') {
            const endIdx = code.indexOf('*/', i + 2);
            if (endIdx === -1) return true;  // Unclosed comment
            i = endIdx + 1;
            continue;
        }

        // Count brackets
        switch (char) {
            case '{': braces++; break;
            case '}': braces--; break;
            case '(': parens++; break;
            case ')': parens--; break;
            case '[': brackets++; break;
            case ']': brackets--; break;
        }
    }

    // Incomplete if any unclosed brackets or strings
    if (braces > 0 || parens > 0 || brackets > 0 || inString || inTemplate) {
        return true;
    }

    // Check if line ends with an operator that expects continuation
    const trimmed = code.trim();
    const continuationOperators = ['+', '-', '*', '/', '%', '&', '|', '^', '=', '<', '>', '?', ':', ',', '&&', '||', '??'];

    for (const op of continuationOperators) {
        if (trimmed.endsWith(op)) {
            return true;
        }
    }

    return false;
}

type LogLevel = 'log' | 'info' | 'warn' | 'error' | 'result' | 'input';

interface ConsoleElements {
    panel: HTMLElement;
    output: HTMLElement;
    input: HTMLTextAreaElement;
    toggleBtn: HTMLElement;
    clearBtn: HTMLElement;
    connectBtn: HTMLElement;
    statusIndicator: HTMLElement;
}

let elements: ConsoleElements | null = null;
let inputHistory: string[] = [];
let historyIndex: number = -1;
let isCollapsed: boolean = true;

/**
 * Auto-resize textarea to fit content
 */
function autoResizeInput(): void {
    if (!elements) return;
    const input = elements.input;
    input.style.height = 'auto';
    input.style.height = Math.min(input.scrollHeight, 150) + 'px';
}

/**
 * Initialize the console panel
 */
export function initConsole(): void {
    const panel = document.getElementById('consolePanel');
    const output = document.getElementById('consoleOutput');
    const input = document.getElementById('consoleInput') as HTMLTextAreaElement;
    const toggleBtn = document.getElementById('consoleToggle');
    const clearBtn = document.getElementById('consoleClear');
    const connectBtn = document.getElementById('consoleConnect');
    const statusIndicator = document.getElementById('consoleStatus');

    if (!panel || !output || !input || !toggleBtn || !clearBtn || !connectBtn || !statusIndicator) {
        console.error('Console elements not found');
        return;
    }

    elements = { panel, output, input, toggleBtn, clearBtn, connectBtn, statusIndicator };

    // Set up event handlers
    toggleBtn.addEventListener('click', toggleConsole);
    clearBtn.addEventListener('click', clearConsole);
    connectBtn.addEventListener('click', handleConnect);

    input.addEventListener('keydown', handleInputKeydown);
    input.addEventListener('input', autoResizeInput);

    // Initialize autocomplete for the console input
    initConsoleAutocomplete(input);

    // Set up WebSocket handlers
    const ws = getWebSocket();
    ws.onConsole(handleConsoleMessage);
    ws.onConnect(handleConnectionChange);
    ws.onDisconnect(handleConnectionChange);

    appendLine('Console ready. Enter device URL and click Connect.', 'info');
}

/**
 * Toggle console panel visibility
 */
export function toggleConsole(): void {
    if (!elements) return;

    isCollapsed = !isCollapsed;
    elements.panel.classList.toggle('collapsed', isCollapsed);
    elements.toggleBtn.textContent = isCollapsed ? '▲' : '▼';

    if (!isCollapsed) {
        elements.input.focus();
    }
}

/**
 * Clear console output
 */
export function clearConsole(): void {
    if (!elements) return;
    elements.output.innerHTML = '';
    appendLine('Console cleared.', 'info');
}

/**
 * Append a line to the console output
 */
export function appendLine(text: string, level: LogLevel = 'log'): void {
    if (!elements) return;

    const line = document.createElement('div');
    line.className = `console-line console-${level}`;

    const timestamp = new Date().toLocaleTimeString();
    const prefix = level === 'input' ? '> ' : '';

    line.innerHTML = `<span class="console-time">[${timestamp}]</span> ${prefix}${escapeHtml(text)}`;

    elements.output.appendChild(line);
    elements.output.scrollTop = elements.output.scrollHeight;
}

/**
 * Handle connection button click
 */
async function handleConnect(): Promise<void> {
    if (!elements) return;

    const deviceUrlInput = document.getElementById('deviceUrl') as HTMLInputElement;
    if (!deviceUrlInput) return;

    const url = deviceUrlInput.value.trim();
    if (!url) {
        appendLine('Please enter a device URL', 'error');
        return;
    }

    const ws = getWebSocket();

    if (ws.isConnected) {
        ws.disconnect();
        elements.connectBtn.textContent = 'Connect';
        appendLine('Disconnected from device', 'info');
    } else {
        elements.connectBtn.textContent = 'Connecting...';
        appendLine(`Connecting to ${url}...`, 'info');
        ws.connect(url);
    }
}

/**
 * Handle WebSocket console message
 */
function handleConsoleMessage(message: string): void {
    // Remove trailing newline if present
    const cleanMessage = message.replace(/\n$/, '');
    if (cleanMessage) {
        appendLine(cleanMessage, 'log');
    }
}

/**
 * Handle WebSocket connection state change
 */
function handleConnectionChange(): void {
    if (!elements) return;

    const ws = getWebSocket();
    const connected = ws.isConnected;

    elements.statusIndicator.className = connected ? 'status-connected' : 'status-disconnected';
    elements.statusIndicator.title = connected ? 'Connected' : 'Disconnected';
    elements.connectBtn.textContent = connected ? 'Disconnect' : 'Connect';

    if (connected) {
        appendLine('Connected to device', 'info');
        // Expand console when connected
        if (isCollapsed) {
            toggleConsole();
        }
    }
}

/**
 * Handle input keydown events
 */
async function handleInputKeydown(event: KeyboardEvent): Promise<void> {
    if (!elements) return;

    const input = elements.input;

    // Let autocomplete handle its own keys when visible
    if (isAutocompleteVisible()) {
        return;
    }

    switch (event.key) {
        case 'Enter':
            // Shift+Enter always inserts newline (default behavior)
            if (event.shiftKey) {
                return;
            }

            event.preventDefault();

            const code = input.value;
            if (!code.trim()) {
                return;
            }

            // Check if code is incomplete (unclosed brackets, etc.)
            if (isIncompleteCode(code)) {
                // Insert newline and let user continue typing
                const start = input.selectionStart || 0;
                const end = input.selectionEnd || 0;
                input.value = code.substring(0, start) + '\n' + code.substring(end);
                input.selectionStart = input.selectionEnd = start + 1;
                autoResizeInput();
            } else {
                // Code is complete, execute it
                await executeInput();
            }
            break;

        case 'ArrowUp':
            // Only navigate history if cursor is at the start (for multi-line)
            if (input.selectionStart === 0 && input.selectionEnd === 0) {
                event.preventDefault();
                navigateHistory(1);
            }
            break;

        case 'ArrowDown':
            // Only navigate history if cursor is at the end (for multi-line)
            if (input.selectionStart === input.value.length && input.selectionEnd === input.value.length) {
                event.preventDefault();
                navigateHistory(-1);
            }
            break;

        case 'Escape':
            input.value = '';
            historyIndex = -1;
            autoResizeInput();
            break;
    }
}

/**
 * Execute the current input
 */
async function executeInput(): Promise<void> {
    if (!elements) return;

    const code = elements.input.value.trim();
    if (!code) return;

    // Add to history
    if (inputHistory[0] !== code) {
        inputHistory.unshift(code);
        if (inputHistory.length > 50) {
            inputHistory.pop();
        }
    }
    historyIndex = -1;

    // Display input (show multi-line code properly)
    appendLine(code, 'input');
    elements.input.value = '';
    autoResizeInput();

    // Execute
    const ws = getWebSocket();
    if (!ws.isConnected) {
        appendLine('Not connected to device', 'error');
        return;
    }

    const result = await ws.evaluate(code);

    if (result.success) {
        if (result.value && result.value !== 'undefined') {
            appendLine(result.value, 'result');
        }
    } else {
        appendLine(result.error || 'Unknown error', 'error');
    }
}

/**
 * Navigate input history
 */
function navigateHistory(direction: number): void {
    if (!elements || inputHistory.length === 0) return;

    historyIndex += direction;

    if (historyIndex < -1) {
        historyIndex = -1;
    } else if (historyIndex >= inputHistory.length) {
        historyIndex = inputHistory.length - 1;
    }

    if (historyIndex === -1) {
        elements.input.value = '';
    } else {
        elements.input.value = inputHistory[historyIndex] || '';
    }

    // Resize and move cursor to end
    autoResizeInput();
    elements.input.setSelectionRange(elements.input.value.length, elements.input.value.length);
}

/**
 * Escape HTML special characters
 */
function escapeHtml(text: string): string {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}
