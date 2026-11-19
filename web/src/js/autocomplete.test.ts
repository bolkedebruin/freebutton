import { styleNames } from './font-config';
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

        it('suggests button names inside fb.button("', () => {
            const code = 'fb.button("sc';
            const result = configNameCompletions(createContext(code), names);
            expect(labelsOf(result)).toEqual(['sceneUp', 'sceneDown']);
            expect(result?.from).toBe(code.length - 2);
            expect(result?.options[0]?.detail).toBe('button (config.json)');
            // fb.key no longer exists: nothing is offered in it.
            expect(configNameCompletions(createContext('fb.key("sc'), names)).toBeNull();
        });

        it('suggests display item names inside the first argument of the display methods', () => {
            for (const method of ['remove', 'get', 'setLabel', 'setValue', 'setUnit', 'setPosition', 'setPage', 'setIcon', 'setStyle', 'addStyle', 'removeStyle']) {
                const code = `fb.main.display.${method}('`;
                const result = configNameCompletions(createContext(code), names);
                expect(labelsOf(result)).toEqual(['volume', 'clock']);
                expect(result?.from).toBe(code.length);
            }
            for (const gone of ['setFont', 'setLabelStyle']) {
                expect(configNameCompletions(createContext(`fb.main.display.${gone}("`), names), gone).toBeNull();
            }
        });

        it('suggests list names inside the first argument of the fb.main.list methods', () => {
            for (const method of ['next', 'prev', 'select', 'getSelected', 'count', 'setItems', 'setStyle']) {
                const code = `fb.main.list.${method}("r`;
                const result = configNameCompletions(createContext(code), names);
                expect(labelsOf(result)).toEqual(['scenes', 'rooms']);
                expect(result?.from).toBe(code.length - 1);
                expect(result?.options[0]?.detail).toBe('list (config.json)');
            }
            expect(configNameCompletions(createContext('fb.main.list.select("scenes", "'), names)).toBeNull();
            expect(configNameCompletions(createContext('fb.main.list.reset("'), names)).toBeNull();
            for (const gone of ['setFont', 'setFocusStyle', 'addStyle', 'removeStyle']) {
                expect(configNameCompletions(createContext(`fb.main.list.${gone}("`), names), gone).toBeNull();
            }
        });

        it('has generated completion data for the fb.main.list methods', () => {
            const labels = freebuttonAPI.fb!.methods.map(m => m.label).filter(l => l.startsWith('fb.main.list.'));
            expect(labels).toEqual(['next', 'prev', 'select', 'getSelected', 'count', 'setItems', 'setStyle'].map(m => `fb.main.list.${m}`));
            const select = freebuttonAPI.fb!.methods.find(m => m.label === 'fb.main.list.select');
            expect(select?.apply).toBe('fb.main.list.select(${1:nameOrIndex}, ${2:index})');
        });

        it('has generated completion data for fb.button and the style calls, and none for the removed calls', () => {
            const byLabel = (label: string) => freebuttonAPI.fb!.methods.filter(m => m.label === label);
            expect(byLabel('fb.button')[0]?.apply).toBe('fb.button(${1:name})');
            expect(byLabel('fb.style.set')[0]?.apply).toBe('fb.style.set(${1:style}, ${2:property}, ${3:value})');
            expect(byLabel('fb.style.set')[0]?.info).toContain('waits for the display task');
            expect(byLabel('fb.main.display.setStyle')[0]?.apply).toBe('fb.main.display.setStyle(${1:idOrName}, ${2:styles})');
            expect(byLabel('fb.main.display.addStyle')[0]?.apply).toBe('fb.main.display.addStyle(${1:idOrName}, ${2:style})');
            expect(byLabel('fb.main.display.removeStyle')[0]?.apply).toBe('fb.main.display.removeStyle(${1:idOrName}, ${2:style})');
            expect(byLabel('fb.main.list.setStyle')[0]?.apply).toBe('fb.main.list.setStyle(${1:nameOrIndex}, ${2:styles})');
            expect(byLabel('fb.main.display.create')[0]?.apply).toContain('${5:styles}');
            const labels = freebuttonAPI.fb!.methods.map(m => m.label);
            // A button's display has the three; a list has setStyle only.
            for (const method of ['setStyle', 'addStyle', 'removeStyle']) {
                expect(labels.filter(l => l.endsWith(`.display.${method}`) && !l.startsWith('fb.main.display.')).length, method).toBeGreaterThanOrEqual(1);
                expect(byLabel(`fb.main.display.${method}`)[0]?.info, method).toContain('false when there is no such item');
            }
            expect(labels.filter(l => /^fb\.main\.list\.(add|remove)Style$/.test(l))).toEqual([]);
            expect(labels.filter(l => /^fb\.key$|\.set(Top)?Font$|\.set(Top|Label|Focus)Style$/.test(l))).toEqual([]);
        });

        it('suggests the styles an item can name for setStyle / addStyle / removeStyle and display.create; every style for fb.style.set', () => {
            const kinds = { button: ['color'], displayitem: ['color'], list: ['color'] };
            const withStyles = { ...names, styles: { ...kinds, alarm: ['color', 'opacity'], big: ['font-family', 'font-size'], picked: ['selected.background-color'] } };
            for (const code of [
                'fb.main.list.setStyle(0, "', 'fb.main.list.setStyle("scenes", "',
                'fb.main.display.setStyle("volume", "', 'fb.main.display.addStyle(3, "', 'fb.main.display.removeStyle(id, "',
                'fb.button("sceneUp").display.setStyle("', 'k.display.addStyle("', 'fb.bars[0].left.display.removeStyle("',
                'fb.main.display.create(0, 0, 40, 0, "', 'fb.main.display.create(x, y + 1, 40, 0, \'',
            ]) {
                const result = configNameCompletions(createContext(code), withStyles);
                // An item cannot name "button", "displayitem" or "list".
                expect(labelsOf(result), code).toEqual(['alarm', 'big', 'picked']);
                expect(result?.options[0]?.detail).toBe('style (config.json)');
            }
            const all = configNameCompletions(createContext('fb.style.set("'), withStyles);
            expect(labelsOf(all)).toEqual(['button', 'displayitem', 'list', 'alarm', 'big', 'picked']);
            // The first argument of the main display's and the list's calls is the item or the list.
            expect(labelsOf(configNameCompletions(createContext('fb.main.display.setStyle("'), withStyles))).toEqual(['volume', 'clock']);
            expect(labelsOf(configNameCompletions(createContext('fb.main.display.addStyle("'), withStyles))).toEqual(['volume', 'clock']);
            expect(labelsOf(configNameCompletions(createContext('fb.main.list.setStyle("'), withStyles))).toEqual(['scenes', 'rooms']);
            // Not the style argument of display.create.
            expect(configNameCompletions(createContext('fb.main.display.create(0, 0, 40, "'), withStyles)).toBeNull();
            expect(configNameCompletions(createContext('fb.main.display.create(0, 0, 40, 0, "big", 0, "'), withStyles)).toBeNull();
            // The styles are not known: nothing to offer.
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("'), names))).toEqual([]);
        });

        it('a list of styles: the name being typed is completed, after the names before it; a name the list has is not offered again', () => {
            const withStyles = { ...names, styles: styleNames({ styles: { alarm: { color: '#ff0000' }, big: { 'font-size': 48 }, muted: { color: '#808080' } } }) };
            for (const code of ['fb.main.display.setStyle("volume", "big al', 'k.display.setStyle("big   al', 'fb.main.display.create(0, 0, 40, 0, "big al']) {
                const result = configNameCompletions(createContext(code), withStyles);
                // "big" is in the list already: a name given twice counts once.
                expect(labelsOf(result), code).toEqual(['alarm', 'muted']);
                expect(result?.from, code).toBe(code.length - 2);
            }
            const next = 'fb.main.list.setStyle(0, "big alarm ';
            const after = configNameCompletions(createContext(next), withStyles);
            expect(after?.from).toBe(next.length);
            expect(labelsOf(after)).toEqual(['muted']);
            // The first name: every style an item can name.
            expect(labelsOf(configNameCompletions(createContext('fb.button("sceneUp").display.setStyle("'), withStyles))).toEqual(['alarm', 'big', 'muted']);
            // All of them listed: nothing left to offer.
            expect(labelsOf(configNameCompletions(createContext('fb.main.display.setStyle("volume", "big alarm muted '), withStyles))).toEqual([]);
        });

        it('addStyle and removeStyle take one name: none is offered after it', () => {
            const withStyles = { ...names, styles: styleNames({ styles: { alarm: { color: '#ff0000' }, big: { 'font-size': 48 } } }) };
            for (const code of [
                'fb.main.display.addStyle("volume", "', 'fb.main.display.removeStyle("volume", "', 'k.display.addStyle("', 'fb.button("sceneUp").display.removeStyle("',
            ]) {
                expect(labelsOf(configNameCompletions(createContext(code), withStyles)), code).toEqual(['alarm', 'big']);
                expect(labelsOf(configNameCompletions(createContext(code + 'al'), withStyles)), code).toEqual(['alarm', 'big']);
                // A second name: these calls take one, so there is nothing to complete (no result at all).
                expect(configNameCompletions(createContext(code + 'big '), withStyles), code).toBeNull();
                expect(configNameCompletions(createContext(code + 'big al'), withStyles), code).toBeNull();
            }
            // A list has setStyle only.
            expect(configNameCompletions(createContext('fb.main.list.addStyle("scenes", "'), withStyles)).toBeNull();
        });

        it('suggests the properties fb.style.set can set: every one but the font, the size and the line height, in each block the style has', () => {
            const withStyles = {
                ...names,
                styles: styleNames({
                    styles: {
                        alarm: { color: '#ff0000', title: { 'font-size': 20 } }, big: { 'font-family': 'montserrat', 'font-size': 48, 'line-height': 1.5 },
                        picked: { selected: { color: '#0000ff' } }, empty: {},
                    },
                }),
            };
            const six = ['color', 'text-align', 'letter-spacing', 'text-decoration', 'opacity', 'background-color'];
            const result = configNameCompletions(createContext('fb.style.set("alarm", "'), withStyles);
            // Not only what the style sets in config.json: a property is added by the call.
            expect(labelsOf(result)).toEqual([...six, ...six.map(n => `title.${n}`), 'icon.color']);
            expect(result?.options[0]).toMatchObject({ type: 'property', detail: 'property of style "alarm"' });
            const dotted = configNameCompletions(createContext('fb.style.set("alarm", "title.co'), withStyles);
            expect(dotted?.from).toBe('fb.style.set("alarm", "'.length);
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("list", "selected.background-'), withStyles)))
                .toEqual([...six, ...six.map(n => `selected.${n}`)]);
            // A style of a font only: its main block is there; never the font itself.
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("big", "'), withStyles))).toEqual([...six, 'icon.color']);
            // No main block: the "selected" block and the icon colour.
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("picked", "'), withStyles))).toEqual([...six.map(n => `selected.${n}`), 'icon.color']);
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("empty", "'), withStyles))).toEqual(['icon.color']);
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("button", "'), withStyles))).toEqual([...six, ...six.map(n => `title.${n}`), 'icon.color']);
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("nosuch", "'), withStyles))).toEqual([]);
            // A name that is an Object.prototype member is no style: nothing to offer, and no exception.
            for (const name of ['constructor', 'toString', 'hasOwnProperty', '__proto__', 'valueOf']) {
                expect(labelsOf(configNameCompletions(createContext(`fb.style.set("${name}", "`), withStyles)), name).toEqual([]);
            }
            // ... unless config.json has a style of that name.
            const odd = { ...names, styles: styleNames(JSON.parse('{"styles": {"constructor": {"color": "#ff0000"}, "__proto__": {"title": {"color": "#00ff00"}}}}')) };
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("constructor", "'), odd))).toEqual([...six, 'icon.color']);
            expect(labelsOf(configNameCompletions(createContext('fb.style.set("__proto__", "'), odd))).toEqual([...six.map(n => `title.${n}`), 'icon.color']);
            expect(labelsOf(configNameCompletions(createContext('fb.button("sceneUp").display.setStyle("'), odd))).toEqual(['constructor', '__proto__']);
        });

        it('a member of an object that is no API: no completions and no exception, also for Object.prototype names', () => {
            for (const code of ['x.constructor.', 'x.toString.', 'x.hasOwnProperty.', 'x.__proto__.', 'x.valueOf.', 'constructor.', 'toString.']) {
                const result = freebuttonCompletions(createContext(code));
                expect(result?.options ?? [], code).toEqual([]);
            }
        });

        it('works with the closing quote already inserted', () => {
            const code = 'fb.button("")';
            const result = configNameCompletions(createContext(code, 'fb.button("'.length), names);
            expect(labelsOf(result)).toEqual(['sceneUp', 'sceneDown']);
        });

        it('offers nothing outside a name argument', () => {
            for (const code of ['fb.button(', 'fb.button(x', 'fb.main.display.setValue("volume", "', 'fb.main.display.create("',
                'fb.main.display.showPage("', 'print("']) {
                expect(configNameCompletions(createContext(code), names)).toBeNull();
            }
        });

        it('offers no names without a config.json', () => {
            const result = configNameCompletions(createContext('fb.button("'), null);
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
