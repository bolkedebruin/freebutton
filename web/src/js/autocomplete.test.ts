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
            expect(labels).toEqual(['next', 'prev', 'select', 'getSelected', 'count', 'setItems', 'setFont', 'setStyle', 'setFocusStyle'].map(m => `fb.main.list.${m}`));
            const setFont = freebuttonAPI.fb!.methods.find(m => m.label === 'fb.main.list.setFont');
            expect(setFont?.apply).toBe('fb.main.list.setFont(${1:nameOrIndex}, ${2:font}, ${3:size})');
            expect(setFont?.info).toContain('font "" restores config.json');
            // every setFont / setTopFont documents the "" restore
            const fontMethods = freebuttonAPI.fb!.methods.filter(m => /\.set(Top)?Font$/.test(m.label));
            expect(fontMethods.length).toBeGreaterThanOrEqual(1);
            for (const m of fontMethods) expect(m.info).toContain('restores config.json');
            const select = freebuttonAPI.fb!.methods.find(m => m.label === 'fb.main.list.select');
            expect(select?.apply).toBe('fb.main.list.select(${1:nameOrIndex}, ${2:index})');
        });

        it('suggests font names for setFont / setTopFont (the system fonts without a "style.fonts" table)', () => {
            const withFonts = { ...names, fonts: ['montserrat', 'montserrat_semibold', 'roboto'] };
            for (const code of ['fb.main.display.setFont("volume", "', 'fb.main.list.setFont(0, "', 'fb.key(3).display.setFont("', 'k.display.setTopFont("']) {
                expect(labelsOf(configNameCompletions(createContext(code), withFonts))).toEqual(['montserrat', 'montserrat_semibold', 'roboto']);
            }
            expect(labelsOf(configNameCompletions(createContext('fb.key(1).display.setFont("'), names))).toEqual(['montserrat', 'montserrat_semibold']);
            // The first argument of fb.main.display.setFont is the item.
            expect(labelsOf(configNameCompletions(createContext('fb.main.display.setFont("'), withFonts))).toEqual(['volume', 'clock']);
        });

        it('has generated completion data for the named-style calls', () => {
            const byLabel = (label: string) => freebuttonAPI.fb!.methods.filter(m => m.label === label);
            expect(byLabel('fb.style.set')[0]?.apply).toBe('fb.style.set(${1:style}, ${2:property}, ${3:value})');
            expect(byLabel('fb.main.display.setStyle')[0]?.apply).toBe('fb.main.display.setStyle(${1:idOrName}, ${2:style})');
            expect(byLabel('fb.main.display.setLabelStyle')[0]?.apply).toBe('fb.main.display.setLabelStyle(${1:idOrName}, ${2:style})');
            expect(byLabel('fb.main.list.setFocusStyle')[0]?.apply).toBe('fb.main.list.setFocusStyle(${1:nameOrIndex}, ${2:style})');
            const labels = freebuttonAPI.fb!.methods.map(m => m.label);
            expect(labels.filter(l => /\.display\.setTopStyle$/.test(l)).length).toBeGreaterThanOrEqual(1);
            // every one says that it needs named styles in config.json
            const styleMethods = freebuttonAPI.fb!.methods.filter(m => /\.set(Top|Label|Focus)?Style$/.test(m.label));
            expect(styleMethods.length).toBeGreaterThanOrEqual(6);
            for (const m of styleMethods) expect(m.info, m.label).toContain('config.json has named styles');
        });

        it('suggests named styles for setStyle / setTopStyle / setLabelStyle / setFocusStyle and fb.style.set', () => {
            const withStyles = { ...names, styles: { alarm: ['text_color', 'text_opa'], big: ['text_font', 'text_size'], picked: ['bg_color'] } };
            for (const code of [
                'fb.main.list.setStyle(0, "', 'fb.main.list.setFocusStyle("scenes", "', 'fb.style.set("',
            ]) {
                const result = configNameCompletions(createContext(code), withStyles);
                expect(labelsOf(result), code).toEqual(['alarm', 'big', 'picked']);
                expect(result?.options[0]?.detail).toBe('named style (config.json)');
            }
            // A key and a display item have no background: a style that sets bg_* is not offered
            // (the device throws TypeError for it), as in the item popover.
            const withBg = { ...withStyles, styles: { ...withStyles.styles, dim: ['text_color', 'bg_opa'] } };
            for (const code of [
                'fb.main.display.setStyle("volume", "', 'fb.main.display.setLabelStyle(3, "',
                'fb.key("sceneUp").display.setStyle("', 'k.display.setTopStyle("',
            ]) {
                const result = configNameCompletions(createContext(code), withBg);
                expect(labelsOf(result), code).toEqual(['alarm', 'big']);
                expect(result?.options[0]?.detail).toBe('named style (config.json)');
            }
            expect(labelsOf(configNameCompletions(createContext('fb.main.list.setStyle(0, "'), withBg))).toEqual(['alarm', 'big', 'picked', 'dim']);
            // The first argument of the main display's and the list's calls is the item or the list.
            expect(labelsOf(configNameCompletions(createContext('fb.main.display.setStyle("'), withStyles))).toEqual(['volume', 'clock']);
            expect(labelsOf(configNameCompletions(createContext('fb.main.display.setLabelStyle("'), withStyles))).toEqual(['volume', 'clock']);
            expect(labelsOf(configNameCompletions(createContext('fb.main.list.setFocusStyle("'), withStyles))).toEqual(['scenes', 'rooms']);
            // No named styles in config.json: nothing to offer.
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("'), names))).toEqual([]);
        });

        it('suggests the properties fb.style.set can change: what the style sets, but its font and size', () => {
            const withStyles = { ...names, styles: { alarm: ['text_color', 'text_opa'], big: ['text_font', 'text_size'] } };
            const result = configNameCompletions(createContext('fb.style.set("alarm", "text_'), withStyles);
            expect(labelsOf(result)).toEqual(['text_color', 'text_opa']);
            expect(result?.from).toBe('fb.style.set("alarm", "'.length);
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("big", "'), withStyles))).toEqual([]);
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("nosuch", "'), withStyles))).toEqual([]);
            // A name that is an Object.prototype member is no style: nothing to offer, and no exception.
            for (const name of ['constructor', 'toString', 'hasOwnProperty', '__proto__', 'valueOf']) {
                expect(labelsOf(configNameCompletions(createContext(`fb.style.set("${name}", "`), withStyles)), name).toEqual([]);
            }
            // ... unless config.json has a style of that name.
            const odd = { ...names, styles: Object.assign(Object.create(null), { constructor: ['text_color'] }) };
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("constructor", "'), odd))).toEqual(['text_color']);
            expect(labelsOf(configNameCompletions(createContext('fb.key("sceneUp").display.setStyle("'), odd))).toEqual(['constructor']);
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

describe('icon and image name completions', () => {
    const names = { keys: ['up'], displayItems: ['temp'], lists: [], images: ['radar'], icons: ['rain', 'sun-2'] };
    const labels = (code: string) =>
        configNameCompletions(createContext(code), names)?.options.map(opt => opt.label) ?? null;

    it('offers image names in fb.main.image methods', () => {
        expect(labels('fb.main.image.setIcon("')).toEqual(['radar']);
        expect(labels("fb.main.image.setSrc('ra")).toEqual(['radar']);
    });

    it('offers icon names in the icon argument of setIcon', () => {
        expect(labels('fb.main.display.setIcon("temp", "')).toEqual(['rain', 'sun-2']);
        expect(labels('fb.main.display.setIcon(3, \'su')).toEqual(['rain', 'sun-2']);
        expect(labels('fb.main.image.setIcon("radar", "')).toEqual(['rain', 'sun-2']);
        expect(labels('fb.main.left.display.setIcon("')).toEqual(['rain', 'sun-2']);
        expect(labels('fb.bars[0].right.display.setIcon("')).toEqual(['rain', 'sun-2']);
    });

    it('offers display item names in the first argument of fb.main.display.setIcon', () => {
        expect(labels('fb.main.display.setIcon("')).toEqual(['temp']);
    });
});
