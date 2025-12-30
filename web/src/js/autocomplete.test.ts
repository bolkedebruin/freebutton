import { describe, it, expect } from 'vitest';
import { EditorState } from '@codemirror/state';
import { javascript } from '@codemirror/lang-javascript';
import type { CompletionContext } from '@codemirror/autocomplete';
import { freebuttonCompletions } from './autocomplete';

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
        it('should suggest led methods when typing "led."', () => {
            const context = createContext('led.');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).toContain('count');
            expect(labels).toContain('on');
            expect(labels).toContain('off');
            expect(labels).toContain('setColor');
        });

        it('should suggest button methods when typing "button."', () => {
            const context = createContext('button.');
            const result = freebuttonCompletions(context);

            expect(result).not.toBeNull();
            const labels = result?.options.map(opt =>
                typeof opt === 'string' ? opt : opt.label
            ) || [];

            expect(labels).toContain('count');
            expect(labels).toContain('onClick');
            expect(labels).toContain('setLabel');
        });

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
    });
});
