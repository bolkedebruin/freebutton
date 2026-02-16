import { EditorView } from 'codemirror';
import {
    history,
    defaultKeymap,
    historyKeymap
} from '@codemirror/commands';
import {
    highlightSpecialChars,
    drawSelection,
    highlightActiveLine,
    dropCursor,
    rectangularSelection,
    crosshairCursor,
    lineNumbers,
    highlightActiveLineGutter
} from '@codemirror/view';
import {
    defaultHighlightStyle,
    syntaxHighlighting,
    indentOnInput as indentOnInputLang,
    bracketMatching,
    foldGutter,
    foldKeymap
} from '@codemirror/language';
import { closeBrackets, closeBracketsKeymap } from '@codemirror/autocomplete';
import { searchKeymap, highlightSelectionMatches } from '@codemirror/search';
import { keymap, ViewUpdate } from '@codemirror/view';
import { javascript } from '@codemirror/lang-javascript';
import { githubDark } from '@fsegurai/codemirror-theme-github-dark';
import { indentMore, insertTab } from '@codemirror/commands';
import { acceptCompletion } from '@codemirror/autocomplete';
import { linter, lintGutter, type Diagnostic } from '@codemirror/lint';
import { indentUnit } from '@codemirror/language';
import { freebuttonAutocomplete } from './autocomplete';
import { validateCode } from './validator';
import { emit } from './app-state';
import { examples as examplesList, exampleMap } from './examples';

let editorView: EditorView | null = null;

/**
 * MQuickJS linter - validates code as you type
 */
const mquickjsLinter = linter((view) => {
    const diagnostics: Diagnostic[] = [];
    const code = view.state.doc.toString();

    // Run validation
    const result = validateCode(code);

    // Update status bar
    emit('validation-result', {
        valid: result.valid,
        errors: result.errors.map(e => e.message),
        errorCount: result.errors.length
    });

    // Convert validation errors to CodeMirror diagnostics
    for (const error of result.errors) {
        if (!error.line) continue;

        try {
            // Get the line from the document
            const line = view.state.doc.line(error.line);
            const lineText = view.state.doc.sliceString(line.from, line.to);

            // Calculate position
            let from: number;
            let to: number;

            // Try to extract identifier name from error message
            // Messages like: "'x' is not defined.", "'mqtt.publish' is not defined."
            const identifierMatch = error.message.match(/^'([^']+)'/);
            const identifierName = identifierMatch ? identifierMatch[1] : null;

            if (identifierName && error.column !== undefined) {
                // Search for the identifier in the line text
                // This handles cases where the column doesn't point exactly at the identifier
                const searchStart = Math.max(0, error.column - 10); // Look a bit before the column
                const searchText = lineText.slice(searchStart);
                const identifierPos = searchText.search(new RegExp(`\\b${identifierName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\b`));

                if (identifierPos !== -1) {
                    // Found the identifier, highlight it precisely
                    from = line.from + searchStart + identifierPos;
                    to = from + identifierName.length;
                } else {
                    // Fallback to column-based highlighting
                    from = line.from + error.column;
                    const remainingText = lineText.slice(error.column);
                    const tokenMatch = remainingText.match(/^[\w$]+/);
                    to = tokenMatch ? from + tokenMatch[0].length : Math.min(from + 20, line.to);
                }
            } else if (error.column !== undefined) {
                // No identifier extracted, use column position
                from = line.from + error.column;
                const remainingText = lineText.slice(error.column);
                const tokenMatch = remainingText.match(/^[\w$]+/);
                to = tokenMatch ? from + tokenMatch[0].length : Math.min(from + 20, line.to);
            } else {
                // No column info, highlight the whole line
                from = line.from;
                to = line.to;
            }

            diagnostics.push({
                from,
                to,
                severity: 'error',
                message: error.message
            });
        } catch (e) {
            // Line number out of range, skip this error
            console.warn('Linter error:', e);
        }
    }

    return diagnostics;
});

/**
 * Custom Tab key handler
 * - Accepts completion if menu is open
 * - Inserts tab if cursor is in the middle of a line
 * - Indents if cursor is at start of line
 */
function customTabHandler(target: EditorView): boolean {
    // First, try to accept completion if one is available
    if (acceptCompletion(target)) {
        return true;
    }

    // Get the current selection/cursor position
    const { state } = target;
    const { main } = state.selection;

    // Get the line at cursor
    const line = state.doc.lineAt(main.head);
    const textBeforeCursor = state.sliceDoc(line.from, main.head);

    // If there's only whitespace before cursor, indent the line
    if (/^\s*$/.test(textBeforeCursor)) {
        return indentMore(target);
    }

    // Otherwise, insert a tab character (as spaces)
    return insertTab(target);
}

/**
 * Extension to update cursor position display via events
 */
function cursorPositionExtension() {
    return EditorView.updateListener.of((update: ViewUpdate) => {
        if (update.selectionSet) {
            const pos = update.state.selection.main.head;
            const line = update.state.doc.lineAt(pos);
            emit('cursor-position', { line: line.number, col: pos - line.from + 1 });
        }
    });
}

// Compat re-export: Record<id, code> for legacy consumers
export const examples: Record<string, string> = Object.fromEntries(
    examplesList.map(e => [e.id, e.code])
);

/**
 * Initialize the CodeMirror editor
 * @param parent - The parent element to attach the editor to
 * @returns The initialized editor instance
 */
export function initEditor(parent: HTMLElement): EditorView {
    editorView = new EditorView({
        extensions: [
            // Minimal setup extensions (no autocomplete)
            lineNumbers(),
            highlightActiveLineGutter(),
            highlightSpecialChars(),
            history(),
            foldGutter(),
            drawSelection(),
            dropCursor(),
            indentOnInputLang(),
            syntaxHighlighting(defaultHighlightStyle, { fallback: true }),
            bracketMatching(),
            closeBrackets(),
            rectangularSelection(),
            crosshairCursor(),
            highlightActiveLine(),
            highlightSelectionMatches(),
            keymap.of([
                ...closeBracketsKeymap,
                ...defaultKeymap,
                ...searchKeymap,
                ...historyKeymap,
                ...foldKeymap
            ]),
            // Language and theme
            javascript(),
            githubDark,
            // Editor configuration
            EditorView.lineWrapping,
            indentUnit.of('    '),  // 4 spaces per indent level
            // Custom keymap for Tab
            keymap.of([
                {
                    key: 'Tab',
                    run: customTabHandler
                }
            ]),
            // Linting
            lintGutter(),
            mquickjsLinter,
            // Custom extensions
            cursorPositionExtension(),
            freebuttonAutocomplete  // Our custom autocomplete only (no default autocomplete)
        ],
        parent: parent
    });

    return editorView;
}

/**
 * Get the current code from the editor
 * @returns The editor content
 */
export function getCode(): string {
    if (!editorView) {
        throw new Error('Editor not initialized');
    }
    return editorView.state.doc.toString();
}

/**
 * Set the editor content
 * @param newContent - The new content to set
 */
export function setCode(newContent: string): void {
    if (!editorView) {
        throw new Error('Editor not initialized');
    }
    editorView.dispatch({
        changes: {
            from: 0,
            to: editorView.state.doc.length,
            insert: newContent
        }
    });
}

/**
 * Clear the editor content
 */
export function clearEditor(): void {
    setCode('');
}

/**
 * Load an example script
 * @param name - The name of the example to load
 * @returns True if example was loaded, false otherwise
 */
export function loadExample(name: string): boolean {
    const example = exampleMap[name];
    if (example) {
        setCode(example.code);
        return true;
    }
    return false;
}

/**
 * Get the editor instance
 * @returns The editor instance
 */
export function getEditorView(): EditorView | null {
    return editorView;
}
