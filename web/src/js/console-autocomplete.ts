/**
 * Console Autocomplete
 * Lightweight autocomplete for the REPL console input
 * Reuses the same autocomplete data as the editor
 */

import { freebuttonAPI, globalCompletions, type CompletionItem } from './autocomplete-data';

interface AutocompleteState {
    visible: boolean;
    options: CompletionItem[];
    selectedIndex: number;
    wordStart: number;
}

let state: AutocompleteState = {
    visible: false,
    options: [],
    selectedIndex: 0,
    wordStart: 0
};

type ConsoleInput = HTMLTextAreaElement | HTMLInputElement;

// The input in use and its dropdown. There is one per console (the device's
// and the virtual device's); only the one being typed in shows completions.
let dropdown: HTMLElement | null = null;
let inputElement: ConsoleInput | null = null;
const dropdowns = new Map<ConsoleInput, HTMLElement>();

/** Make `input` the one the completions belong to; another input's dropdown closes. */
function activate(input: ConsoleInput): void {
    if (inputElement === input) return;
    hideDropdown();
    inputElement = input;
    dropdown = dropdowns.get(input) ?? null;
}

/**
 * Initialize autocomplete for a console input (each console has its own)
 */
export function initConsoleAutocomplete(input: ConsoleInput): void {
    // Create dropdown element
    const list = document.createElement('div');
    list.className = 'console-autocomplete';
    list.style.display = 'none';
    input.parentElement?.appendChild(list);
    dropdowns.set(input, list);
    if (!inputElement) activate(input);

    // Event listeners
    input.addEventListener('input', () => { activate(input); handleInput(); });
    input.addEventListener('keydown', ((e: KeyboardEvent) => { if (inputElement === input) handleKeydown(e); }) as EventListener);
    input.addEventListener('blur', () => {
        // Delay hide to allow click on dropdown
        setTimeout(() => { if (inputElement === input) hideDropdown(); }, 150);
    });

    // Click handler for dropdown items
    list.addEventListener('mousedown', (e) => {
        activate(input);
        const target = e.target as HTMLElement;
        const item = target.closest('.autocomplete-item') as HTMLElement;
        if (item) {
            const index = parseInt(item.dataset.index || '0', 10);
            selectItem(index);
            e.preventDefault();
        }
    });
}

/**
 * Handle input changes
 */
function handleInput(): void {
    if (!inputElement) return;

    const value = inputElement.value;
    const cursorPos = inputElement.selectionStart || value.length;

    // Find the word being typed
    const beforeCursor = value.substring(0, cursorPos);
    const match = beforeCursor.match(/(\w+\.?\w*)$/);

    const word = match?.[1];
    if (!word) {
        hideDropdown();
        return;
    }

    state.wordStart = cursorPos - word.length;

    // Get completions
    const completions = getCompletions(word);

    if (completions.length === 0) {
        hideDropdown();
        return;
    }

    state.options = completions;
    state.selectedIndex = 0;
    showDropdown();
}

/**
 * Get matching completions for a word
 */
function getCompletions(word: string): CompletionItem[] {
    const results: CompletionItem[] = [];
    const lowerWord = word.toLowerCase();

    // Check if we're completing after a dot (e.g., "led.")
    if (word.includes('.')) {
        const parts = word.split('.');
        const objName = parts[0];
        const methodPrefix = parts[1] || '';
        const api = objName ? freebuttonAPI[objName] : undefined;

        if (api) {
            for (const method of api.methods) {
                const methodName = method.label.split('.')[1] || method.label;
                if (methodName.toLowerCase().startsWith(methodPrefix.toLowerCase())) {
                    results.push(method);
                }
            }
        }
    } else {
        // Complete object names and global functions
        for (const objName of Object.keys(freebuttonAPI)) {
            if (objName.toLowerCase().startsWith(lowerWord)) {
                results.push({
                    label: objName,
                    type: 'namespace',
                    detail: 'API object',
                    info: `${objName} API`,
                    apply: objName
                });
            }
        }

        // Global functions
        for (const global of globalCompletions) {
            if (global.label.toLowerCase().startsWith(lowerWord)) {
                results.push(global);
            }
        }
    }

    return results.slice(0, 10); // Limit results
}

/**
 * Handle keyboard navigation
 */
function handleKeydown(e: KeyboardEvent): void {
    if (!state.visible) return;

    switch (e.key) {
        case 'ArrowDown':
            e.preventDefault();
            state.selectedIndex = Math.min(state.selectedIndex + 1, state.options.length - 1);
            updateSelection();
            break;

        case 'ArrowUp':
            e.preventDefault();
            state.selectedIndex = Math.max(state.selectedIndex - 1, 0);
            updateSelection();
            break;

        case 'Tab':
        case 'Enter':
            if (state.visible && state.options.length > 0) {
                e.preventDefault();
                selectItem(state.selectedIndex);
            }
            break;

        case 'Escape':
            hideDropdown();
            break;
    }
}

/**
 * Select an item and insert it
 */
function selectItem(index: number): void {
    if (!inputElement || index < 0 || index >= state.options.length) return;

    const item = state.options[index];
    if (!item) return;

    const value = inputElement.value;
    let insertText = item.apply;

    // For REPL, we don't want multiline snippets
    insertText = insertText.replace(/\\n\\t/g, '');

    // Find the position of the first placeholder before stripping
    const firstPlaceholderMatch = insertText.match(/\$\{1:?([^}]*)\}/);
    let cursorOffset = -1;
    let selectionLength = 0;

    if (firstPlaceholderMatch) {
        cursorOffset = insertText.indexOf(firstPlaceholderMatch[0]);
        selectionLength = firstPlaceholderMatch[1]?.length || 0;
    }

    // Strip snippet placeholders like ${1:position} -> just leave the placeholder text
    insertText = insertText.replace(/\$\{\d+:?([^}]*)\}/g, '$1');

    // Handle dot completions - only insert the method part
    const beforeWord = value.substring(0, state.wordStart);
    let finalText: string;

    if (beforeWord.match(/\w+\.$/)) {
        // Already have "obj.", just insert method name
        const methodPart = insertText.split('.').pop() || insertText;
        finalText = beforeWord + methodPart;
        // Adjust cursor offset for method-only insertion
        if (cursorOffset >= 0) {
            const dotIndex = insertText.lastIndexOf('.');
            if (dotIndex >= 0) {
                cursorOffset -= (dotIndex + 1);
            }
        }
    } else {
        finalText = beforeWord + insertText;
    }

    inputElement.value = finalText;
    inputElement.focus();

    // Position cursor at first argument or select it
    if (cursorOffset >= 0) {
        const cursorPos = beforeWord.length + cursorOffset;
        if (selectionLength > 0) {
            // Select the placeholder text so user can type over it
            inputElement.setSelectionRange(cursorPos, cursorPos + selectionLength);
        } else {
            inputElement.setSelectionRange(cursorPos, cursorPos);
        }
    } else {
        // No placeholder, move to end
        inputElement.setSelectionRange(finalText.length, finalText.length);
    }

    hideDropdown();
}

/**
 * Check if autocomplete is currently visible
 */
export function isAutocompleteVisible(): boolean {
    return state.visible;
}

/**
 * Show the autocomplete dropdown
 */
function showDropdown(): void {
    if (!dropdown || !inputElement) return;

    state.visible = true;
    dropdown.style.display = 'block';

    // Render options
    dropdown.innerHTML = state.options.map((opt, i) => {
        const label = opt.label.includes('.') ? opt.label.split('.')[1] : opt.label;
        return `
            <div class="autocomplete-item ${i === state.selectedIndex ? 'selected' : ''}" data-index="${i}">
                <span class="autocomplete-label">${label}</span>
                <span class="autocomplete-detail">${opt.detail}</span>
            </div>
        `;
    }).join('');
}

/**
 * Update selection highlight
 */
function updateSelection(): void {
    if (!dropdown) return;

    const items = dropdown.querySelectorAll('.autocomplete-item');
    items.forEach((item, i) => {
        item.classList.toggle('selected', i === state.selectedIndex);
    });

    // Scroll selected item into view
    const selected = dropdown.querySelector('.selected');
    selected?.scrollIntoView({ block: 'nearest' });
}

/**
 * Hide the dropdown
 */
function hideDropdown(): void {
    if (!dropdown) return;

    state.visible = false;
    dropdown.style.display = 'none';
}
