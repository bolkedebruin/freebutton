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
import { keymap, ViewUpdate, Decoration, MatchDecorator, ViewPlugin, type DecorationSet } from '@codemirror/view';
import { javascript } from '@codemirror/lang-javascript';
import { json } from '@codemirror/lang-json';
import { Compartment, EditorSelection, EditorState, type Extension } from '@codemirror/state';
import { githubDark } from '@fsegurai/codemirror-theme-github-dark';
import { indentMore, insertTab } from '@codemirror/commands';
import { acceptCompletion } from '@codemirror/autocomplete';
import { linter, lintGutter, forceLinting, nextDiagnostic, type Diagnostic } from '@codemirror/lint';
import { indentUnit } from '@codemirror/language';
import { freebuttonAutocomplete } from './autocomplete';
import { validateCode } from './validator';
import { emit, on, sameConfigRef, type ConfigRef } from './app-state';
import { itemAt, rangeOf } from './config-locate';
import { getConfigNames } from './device-config';
import { validateJson, validateConfigShape, type FileKind } from './fs-rules';
import { fontWarnings } from './font-config';
import { fontBucketKeys } from './font-loader';

let editorView: EditorView | null = null;
// Language-specific extensions (syntax, linter, autocomplete), swapped per file.
const languageCompartment = new Compartment();
let baseExtensions: Extension[] = [];
let currentKind: FileKind = 'js';
// Set when the open file is config.json, so the JSON linter adds shape checks.
let currentIsConfig = false;

/** A run-time error of the script in the virtual device (1-based line and column). */
export interface RuntimeError {
    line: number;
    column?: number;
    message: string;
}

let runtimeError: RuntimeError | null = null;

/**
 * Mark (or with null: unmark) the line where the script running in the
 * virtual device failed. Only shown while a script is the open file; it goes
 * away when the text changes (the positions are those of the text that ran).
 * It is not a validation error: Save and Deploy are not held back by it.
 */
export function showRuntimeError(error: RuntimeError | null): void {
    if (error === runtimeError) return;
    runtimeError = error;
    if (editorView && currentKind === 'js') forceLinting(editorView);
}

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

    // Convert validation errors (and warnings) to CodeMirror diagnostics
    for (const error of [...result.errors, ...result.warnings]) {
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
                severity: error.severity === 'warning' ? 'warning' : 'error',
                message: error.message
            });
        } catch (e) {
            // Line number out of range, skip this error
            console.warn('Linter error:', e);
        }
    }

    // An error of the script running in the virtual device, until the text changes.
    if (runtimeError) {
        const lineNo = Math.min(Math.max(1, runtimeError.line), view.state.doc.lines);
        const line = view.state.doc.line(lineNo);
        const from = Math.min(line.to, line.from + Math.max(0, (runtimeError.column ?? 1) - 1));
        diagnostics.push({
            from: from === line.to && line.length > 0 ? line.from : from,
            to: line.to, severity: 'error', message: runtimeError.message, source: 'virtual device',
        });
    }

    return diagnostics;
});

/**
 * JSON linter - syntax errors at the reported offset; config.json also gets
 * the structural check that save enforces.
 */
const jsonLinter = linter((view) => {
    const code = view.state.doc.toString();
    const diagnostics: Diagnostic[] = [];
    const err = validateJson(code);
    if (err) {
        const from = Math.min(err.offset ?? 0, code.length);
        diagnostics.push({ from, to: Math.min(from + 1, code.length), severity: 'error', message: err.message });
    } else if (currentIsConfig) {
        const cfg = JSON.parse(code);
        for (const message of validateConfigShape(cfg)) {
            diagnostics.push({ from: 0, to: Math.min(1, code.length), severity: 'error', message });
        }
        // Fonts missing from the device's fonts bucket (when it was listed), too many instances.
        for (const message of fontWarnings(cfg, fontBucketKeys())) {
            diagnostics.push({ from: 0, to: Math.min(1, code.length), severity: 'warning', message });
        }
    }
    const errors = diagnostics.filter(d => d.severity === 'error');
    emit('validation-result', {
        valid: errors.length === 0,
        errors: errors.map(d => d.message),
        errorCount: errors.length
    });
    return diagnostics;
});

/** Plain text: nothing to validate, keep the status bar honest. */
const textStatus = EditorView.updateListener.of((update: ViewUpdate) => {
    if (update.docChanged) emit('validation-result', { valid: true, errors: [], errorCount: 0 });
});

function languageExtensions(kind: FileKind): Extension {
    if (kind === 'json') return [json(), jsonLinter];
    if (kind === 'js') return [javascript(), mquickjsLinter, freebuttonAutocomplete];
    return [textStatus];
}

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

// The config.json entry under the cursor, as last published.
let lastConfigRef: ConfigRef | null = null;

function publishConfigSelection(ref: ConfigRef | null): void {
    if (ref === lastConfigRef || sameConfigRef(ref, lastConfigRef)) return;
    lastConfigRef = ref;
    emit('config-selection', { ref });
}

/**
 * config.json only: tell the virtual device which entry (key, display item,
 * list, image) the cursor is in, so it can outline it.
 */
/**
 * Private-use characters (the code points icon fonts use, e.g. the Material
 * Design Icons in config.json labels) get the class cm-fb-glyph. Its CSS puts
 * the device's fonts first, so the icon is drawn as on the device; the
 * editor's own fonts would draw a placeholder box and hide the device font.
 */
const GLYPH_CHARS = /[\uE000-\uF8FF\u{F0000}-\u{FFFFD}\u{100000}-\u{10FFFD}]+/gu;
const glyphDecorator = new MatchDecorator({
  regexp: GLYPH_CHARS,
  decoration: Decoration.mark({ class: 'cm-fb-glyph' }),
});
const deviceGlyphExtension = ViewPlugin.fromClass(class {
  decorations: DecorationSet;
  constructor(view: EditorView) { this.decorations = glyphDecorator.createDeco(view); }
  update(update: ViewUpdate) { this.decorations = glyphDecorator.updateDeco(update, this.decorations); }
}, { decorations: v => v.decorations });

function configSelectionExtension() {
    return EditorView.updateListener.of((update: ViewUpdate) => {
        if (!currentIsConfig || !(update.selectionSet || update.docChanged)) return;
        publishConfigSelection(itemAt(update.state, update.state.selection.main.head));
    });
}

/**
 * Initialize the CodeMirror editor
 * @param parent - The parent element to attach the editor to
 * @returns The initialized editor instance
 */
export function initEditor(parent: HTMLElement): EditorView {
    baseExtensions = [
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
            // Theme (language lives in languageCompartment)
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
            // Linting (the linter itself is per language)
            lintGutter(),
            // Custom extensions
            cursorPositionExtension(),
            configSelectionExtension(),
            deviceGlyphExtension,
            docChangeExtension(),
    ];
    editorView = new EditorView({
        state: createState('', currentKind),
        parent: parent
    });
    relintOnConfigNames();

    return editorView;
}

let relintListening = false;

/**
 * The script linter checks fb.key("x") etc. against the loaded config.json:
 * lint the open script again when its key / display item names change.
 */
function relintOnConfigNames(): void {
    if (relintListening) return;
    relintListening = true;
    let names = JSON.stringify(getConfigNames());
    on('device-config', () => {
        const next = JSON.stringify(getConfigNames());
        if (next === names) return;
        names = next;
        if (editorView && currentKind === 'js') forceLinting(editorView);
    });
}

function createState(doc: string, kind: FileKind): EditorState {
    return EditorState.create({
        doc,
        extensions: [...baseExtensions, languageCompartment.of(languageExtensions(kind))],
    });
}

/** Notify listeners (workspace dirty tracking) whenever the document changes. */
function docChangeExtension() {
    return EditorView.updateListener.of((update: ViewUpdate) => {
        if (update.docChanged) {
            runtimeError = null;
            emit('editor-doc-change');
        }
    });
}

/**
 * Replace the editor with a different file: new document, language for `kind`
 * (JS = MQuickJS linter + FreeButton autocomplete, JSON = JSON linter, text =
 * none) and a fresh undo history, so undo cannot jump back into another file.
 */
export function openDocument(content: string, kind: FileKind, opts: { isConfig?: boolean } = {}): void {
    if (!editorView) {
        throw new Error('Editor not initialized');
    }
    currentKind = kind;
    currentIsConfig = !!opts.isConfig;
    runtimeError = null;
    editorView.setState(createState(content, kind));
    // setState is not a transaction, so update listeners don't see it.
    emit('editor-doc-change');
    publishConfigSelection(null);
}

/**
 * Select a config.json entry and scroll it into view (a click on the virtual
 * device). False when config.json is not the open file or the entry is gone.
 */
export function revealConfigItem(ref: Pick<ConfigRef, 'list' | 'index'>): boolean {
    if (!editorView || !currentIsConfig) return false;
    const range = rangeOf(editorView.state, ref);
    if (!range) return false;
    editorView.dispatch({
        // Head at the start: that is the end the view scrolls to.
        selection: EditorSelection.single(range.to, range.from),
        effects: EditorView.scrollIntoView(range.from, { y: 'start', yMargin: 48 }),
    });
    editorView.focus();
    return true;
}

/** The editor's state while config.json is the open file; null otherwise. */
export function configState(): EditorState | null {
    return editorView && currentIsConfig ? editorView.state : null;
}

/**
 * Apply text changes to the open config.json (an edit made on the virtual
 * device, config-edit.ts) as one transaction: one step for undo, the file
 * becomes dirty and the preview follows like after typing. The cursor goes
 * to the first change (or `at`, a position in the text before the change)
 * and the view scrolls only when that is not visible; the editor does not
 * take the focus. False when config.json is not the open file.
 */
export function applyConfigChanges(
    changes: Array<{ from: number; to: number; insert: string }>,
    opts: { at?: number; userEvent?: string } = {},
): boolean {
    if (!editorView || !currentIsConfig) return false;
    const set = editorView.state.changes(changes);
    const at = changes.length > 0 ? changes[0].from : opts.at;
    const pos = at === undefined ? undefined : set.mapPos(Math.min(at, editorView.state.doc.length), 1);
    if (changes.length === 0 && pos === undefined) return true;
    editorView.dispatch({
        changes: set,
        ...(pos === undefined ? {} : {
            selection: EditorSelection.cursor(pos),
            effects: EditorView.scrollIntoView(pos, { y: 'nearest', yMargin: 48 }),
        }),
        userEvent: opts.userEvent ?? 'input.device',
    });
    return true;
}

/** Move the cursor to the first problem the linter reports. False when there is none. */
export function gotoFirstDiagnostic(): boolean {
    if (!editorView) return false;
    editorView.dispatch({ selection: { anchor: 0 } });
    const found = nextDiagnostic(editorView);
    editorView.focus();
    return found;
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
 * Get the editor instance
 * @returns The editor instance
 */
export function getEditorView(): EditorView | null {
    return editorView;
}
