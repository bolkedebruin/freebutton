import { describe, it, expect } from 'vitest';
import { EditorState } from '@codemirror/state';
import { javascript } from '@codemirror/lang-javascript';
import type { CompletionContext } from '@codemirror/autocomplete';
import { freebuttonCompletions, configNameCompletions, editorCompletions } from './autocomplete';
import { freebuttonAPI } from './autocomplete-data';

/**
 * Create a mock completion context for testing
 */
function createContext(code: string, pos?: number): CompletionContext {
    const state = EditorState.create({
        doc: code,
        extensions: [javascript()]
    });

    const position = pos ?? code.length;

    return {
        state,
        pos: position,
        explicit: false,
        matchBefore: (pattern: RegExp) => {
            const textBefore = code.slice(0, position);
            const match = textBefore.match(new RegExp(pattern.source + '$'));
            if (!match) return null;
            return {
                from: position - match[0].length,
                to: position,
                text: match[0]
            };
        },
        tokenBefore: () => null,
        aborted: false,
        addEventListener: () => {}
    } as CompletionContext;
}

describe('FreeButton Autocomplete', () => {
    describe('Case Sensitivity', () => {
        it('should suggest Math when typing "M"', () => {
            const context = createContext('M');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).toContain('Math');
        });

        it('should NOT suggest Math when typing "m" (lowercase)', () => {
            const context = createContext('m');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            // Should not contain Math (uppercase)
            expect(labels).not.toContain('Math');
            // But should contain mqtt (lowercase API)
            expect(labels).toContain('mqtt');
        });

        it('should suggest Math.atan2 when typing "Math.a"', () => {
            const context = createContext('Math.a');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).toContain('atan2');
            expect(labels).toContain('atan');
            expect(labels).toContain('acos');
            expect(labels).toContain('asin');
        });

        it('should NOT suggest anything when typing "math.a" (lowercase)', () => {
            const context = createContext('math.a');
            const result = freebuttonCompletions(context);

            // "math" is not a recognized API, so should return empty options
            expect(result).not.toBeNull();
            expect(result?.options).toEqual([]);
        });

        it('should suggest Object when typing "O"', () => {
            const context = createContext('O');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).toContain('Object');
        });

        it('should NOT suggest Object when typing "o" (lowercase)', () => {
            const context = createContext('o');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).not.toContain('Object');
        });

        it('should suggest String when typing "S"', () => {
            const context = createContext('S');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).toContain('String');
        });

        it('should NOT suggest String when typing "s" (lowercase)', () => {
            const context = createContext('s');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).not.toContain('String');
            // But should contain sensor (lowercase API)
            expect(labels).toContain('sensor');
        });
    });

    describe('Member Expression Completion', () => {
        it('should suggest Math methods when typing "Math."', () => {
            const context = createContext('Math.');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).toContain('atan2');
            expect(labels).toContain('sin');
            expect(labels).toContain('cos');
            expect(labels).toContain('floor');
        });
    });

    describe('Global Completions', () => {
        it('should suggest global functions when typing partial match', () => {
            const context = createContext('set');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).toContain('setTimeout');
        });

        it('should suggest case-sensitive matches only', () => {
            const context = createContext('Set');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            // setTimeout starts with lowercase 's', not uppercase 'S'
            expect(labels).not.toContain('setTimeout');
        });

        it('should suggest fb when typing "f"', () => {
            const context = createContext('f');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).toContain('fb');
        });
    });

    describe('config.json names', () => {
        const names = { keys: ['sceneUp', 'sceneDown'], displayItems: ['volume', 'clock'], lists: ['scenes', 'rooms'] };
        const labelsOf = (result: ReturnType<typeof configNameCompletions>) =>
            result?.options.map(opt => opt.label) ?? [];

        it('suggests key names inside fb.key("', () => {
            const code = 'fb.key("sc';
            const result = configNameCompletions(createContext(code), names);
            expect(labelsOf(result)).toEqual(['sceneUp', 'sceneDown']);
            expect(result?.from).toBe(code.length - 2);
        });

        it('suggests display item names inside the first argument of the display methods', () => {
            for (const method of ['remove', 'get', 'setLabel', 'setValue', 'setUnit', 'setPosition', 'setPage']) {
                const code = `fb.main.display.${method}('`;
                const result = configNameCompletions(createContext(code), names);
                expect(labelsOf(result)).toEqual(['volume', 'clock']);
                expect(result?.from).toBe(code.length);
            }
        });

        it('suggests list names inside the first argument of the fb.main.list methods', () => {
            for (const method of ['next', 'prev', 'select', 'getSelected', 'count', 'setItems']) {
                const code = `fb.main.list.${method}("r`;
                const result = configNameCompletions(createContext(code), names);
                expect(labelsOf(result)).toEqual(['scenes', 'rooms']);
                expect(result?.from).toBe(code.length - 1);
                expect(result?.options[0]?.detail).toBe('list (config.json)');
            }
            expect(configNameCompletions(createContext('fb.main.list.select("scenes", "'), names)).toBeNull();
            expect(configNameCompletions(createContext('fb.main.list.reset("'), names)).toBeNull();
        });

        it('has generated completion data for the fb.main.list methods', () => {
            const labels = freebuttonAPI.fb!.methods.map(m => m.label).filter(l => l.startsWith('fb.main.list.'));
            expect(labels).toEqual(['next', 'prev', 'select', 'getSelected', 'count', 'setItems'].map(m => `fb.main.list.${m}`));
            const select = freebuttonAPI.fb!.methods.find(m => m.label === 'fb.main.list.select');
            expect(select?.apply).toBe('fb.main.list.select(${1:nameOrIndex}, ${2:index})');
        });

        it('works with the closing quote already inserted', () => {
            const code = 'fb.key("")';
            const result = configNameCompletions(createContext(code, 'fb.key("'.length), names);
            expect(labelsOf(result)).toEqual(['sceneUp', 'sceneDown']);
        });

        it('offers nothing outside a name argument', () => {
            for (const code of ['fb.key(', 'fb.key(x', 'fb.main.display.setValue("volume", "', 'fb.main.display.create("',
                'fb.main.display.showPage("', 'print("']) {
                expect(configNameCompletions(createContext(code), names)).toBeNull();
            }
        });

        it('offers no names without a config.json', () => {
            const result = configNameCompletions(createContext('fb.key("'), null);
            expect(result).not.toBeNull();
            expect(result?.options).toEqual([]);
        });

        it('the editor source falls back to the API outside a name argument', () => {
            expect(labelsOf(editorCompletions(createContext('f')))).toContain('fb');
        });
    });
});
