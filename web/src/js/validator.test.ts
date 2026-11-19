/**
 * MQuickJS Validator Tests
 * Tests ES5 strict mode and MQuickJS stricter mode enforcement
 */

import { describe, it, expect } from 'vitest';
import { validateCode } from './validator';
import { styleNames } from './font-config';

describe('MQuickJS Validator', () => {
    describe('Strict Mode Violations', () => {
        it('should reject with statement', () => {
            const result = validateCode('with (obj) { x = 1; }');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('with');
        });

        it('should reject direct eval', () => {
            const result = validateCode('eval("1 + 2");');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('eval');
        });

        it('should allow indirect eval', () => {
            const result = validateCode('(1, eval)("1 + 2");');
            expect(result.valid).toBe(true);
            expect(result.errors).toHaveLength(0);
        });
    });

    describe('Value Boxing', () => {
        it('should reject new Number()', () => {
            const result = validateCode('var n = new Number(42);');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('Number');
            expect(result.errors[0]?.message).toContain('boxing');
        });

        it('should reject new String()', () => {
            const result = validateCode('var s = new String("hello");');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('String');
        });

        it('should reject new Boolean()', () => {
            const result = validateCode('var b = new Boolean(true);');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('Boolean');
        });

        it('should allow direct conversion', () => {
            const result = validateCode(`
                var n = Number(42);
                var s = String("hello");
                var b = Boolean(true);
            `);
            expect(result.valid).toBe(true);
        });
    });

    describe('Array Holes', () => {
        it('should reject array with holes', () => {
            const result = validateCode('var arr = [1, , 3];');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('hole');
        });

        it('should allow explicit undefined', () => {
            const result = validateCode('var arr = [1, undefined, 3];');
            expect(result.valid).toBe(true);
        });
    });

    describe('ES6+ Features', () => {
        it('should reject arrow functions', () => {
            const result = validateCode('var fn = () => {};');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('Arrow');
        });

        it('should reject classes', () => {
            const result = validateCode('class Foo {}');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('Class');
        });

        it('should reject template literals', () => {
            const result = validateCode('var s = `hello ${name}`;');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('Template');
        });

        it('should reject destructuring', () => {
            const result = validateCode('var {x, y} = obj;');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('Destructuring');
        });

        it('should reject spread operator', () => {
            const result = validateCode('var arr = [...other];');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('Spread');
        });

        it('should reject rest parameters', () => {
            const result = validateCode('function fn(...args) {}');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('Rest');
        });
    });

    describe('Valid ES5 Code', () => {
        it('should accept simple function', () => {
            const result = validateCode(`
                function hello() {
                    print("Hello, World!");
                }
                hello();
            `);
            expect(result.valid).toBe(true);
            expect(result.errors).toHaveLength(0);
        });

        it('should accept LED blink example using fb hierarchy', () => {
            const result = validateCode(`
                function blink() {
                    fb.main.left.led.setColor(255, 0, 0);
                    setTimeout(function() {
                        fb.main.left.led.off();
                        setTimeout(blink, 500);
                    }, 500);
                }
                blink();
            `);
            expect(result.valid).toBe(true);
        });

        it('should accept for loops', () => {
            const result = validateCode(`
                for (var i = 0; i < 10; i++) {
                    print(i);
                }
            `);
            expect(result.valid).toBe(true);
        });

        it('should accept object literals', () => {
            const result = validateCode(`
                var obj = {
                    name: "test",
                    value: 42,
                    method: function() {
                        return this.value;
                    }
                };
            `);
            expect(result.valid).toBe(true);
        });

        it('should accept arrays', () => {
            const result = validateCode(`
                var arr = [1, 2, 3, 4, 5];
                arr.push(6);
                arr[0] = 10;
            `);
            expect(result.valid).toBe(true);
        });
    });

    describe('Allowed ES5+ Extensions', () => {
        it('should allow for...of with arrays', () => {
            const result = validateCode(`
                for (var item of [1, 2, 3]) {
                    print(item);
                }
            `);
            expect(result.valid).toBe(true);
        });

        it('should allow exponentiation operator', () => {
            const result = validateCode('var x = 5; var squared = x ** 2;');
            expect(result.valid).toBe(true);
        });
    });

    describe('Error Reporting', () => {
        it('should report line numbers', () => {
            const result = validateCode(`
                var x = 1;
                var y = 2;
                eval("test");
            `);
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.line).toBe(4);
        });

        it('should provide error location information', () => {
            const result = validateCode('eval("test");');
            expect(result.valid).toBe(false);
            // Line number is always available
            expect(result.errors[0]?.line).toBe(1);
            // Column number is available when possible
            expect(result.errors[0]).toHaveProperty('column');
        });

        it('should report syntax errors', () => {
            const result = validateCode('function test() { return');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toBeTruthy();
        });

        it('should report multiple errors', () => {
            const result = validateCode(`
                var n = new Number(1);
                var s = new String("x");
            `);
            expect(result.valid).toBe(false);
            expect(result.errors.length).toBeGreaterThanOrEqual(2);
        });
    });

    describe('Array Boundary Warnings', () => {
        it('should warn about non-sequential array assignments', () => {
            const result = validateCode(`
                var arr = [];
                arr[10] = 1;
            `);
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('sequential');
        });

        it('should allow sequential array assignments', () => {
            const result = validateCode(`
                var arr = [];
                arr[0] = 1;
                arr[1] = 2;
            `);
            expect(result.valid).toBe(true);
        });
    });

    describe('Hoisting', () => {
        it('should accept a top-level call before the function declaration', () => {
            const result = validateCode(`
                blink();
                function blink() { var on = true; }
            `);
            expect(result.errors).toHaveLength(0);
            expect(result.valid).toBe(true);
        });

        it('should accept a call inside a function to a later top-level function', () => {
            const result = validateCode(`
                function start() { return blink(); }
                function blink() { return 1; }
                start();
            `);
            expect(result.errors).toHaveLength(0);
        });

        it('should accept a nested function used before its declaration', () => {
            const result = validateCode(`
                function outer() {
                    var x = inner();
                    return x;
                    function inner() { return 2; }
                }
                outer();
            `);
            expect(result.errors).toHaveLength(0);
        });

        it('should accept a var used before its declaration in the same scope', () => {
            const result = validateCode(`
                function f() {
                    count = 1;
                    if (count) { var count; }
                    return count;
                }
                total = 0;
                var total;
                f();
            `);
            expect(result.errors).toHaveLength(0);
        });

        it('should not leak a nested function or var to the outer scope', () => {
            const result = validateCode(`
                function outer() {
                    var local = 1;
                    function inner() { return local; }
                    return inner();
                }
                inner();
                local;
            `);
            expect(result.valid).toBe(false);
            const messages = result.errors.map(e => e.message).join('\n');
            expect(messages).toContain("'inner' is not defined");
            expect(messages).toContain("'local' is not defined");
        });

        it('should still report an undeclared name', () => {
            const result = validateCode(`
                blink();
                function blink() { return missing; }
            `);
            expect(result.valid).toBe(false);
            expect(result.errors.every(e => e.message.includes("'missing' is not defined"))).toBe(true);
        });

        it('should still report let used before its declaration (TDZ)', () => {
            const result = validateCode(`
                x = 1;
                let x;
            `);
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain("'x' is not defined");
        });
    });

    describe('Stdlib Globals Validation', () => {
        it('should reject undefined function calls', () => {
            const result = validateCode('unknownFunction();');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('not defined');
        });

        it('should accept known global functions', () => {
            const result = validateCode(`
                var x = parseInt("42");
                var y = parseFloat("3.14");
                var z = isNaN(x);
            `);
            expect(result.valid).toBe(true);
        });

        it('should suggest similar globals for typos', () => {
            const result = validateCode('parseIng("42");');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('parseInt');
        });
    });

    describe('MQuickJS Stdlib API Validation', () => {
        it('should accept console.log', () => {
            const result = validateCode('console.log("Hello, World!");');
            expect(result.valid).toBe(true);
        });

        it('should accept console.log with multiple arguments', () => {
            const result = validateCode('console.log("Value:", 42, "test");');
            expect(result.valid).toBe(true);
        });

        it('should reject invalid console methods', () => {
            const result = validateCode('console.error("Error!");');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('not defined');
        });

        it('should accept performance.now', () => {
            const result = validateCode('var t = performance.now();');
            expect(result.valid).toBe(true);
        });

        it('should reject invalid performance methods', () => {
            const result = validateCode('performance.measure();');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('not defined');
        });
    });

    describe('Case Sensitivity', () => {
        it('should accept uppercase Math', () => {
            const result = validateCode('var x = Math.atan2(1, 2);');
            expect(result.valid).toBe(true);
        });

        it('should reject lowercase math as undefined', () => {
            const result = validateCode('var x = math.atan2(1, 2);');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('math');
            expect(result.errors[0]?.message).toContain('not defined');
        });

        it('should accept uppercase Object', () => {
            const result = validateCode('var keys = Object.keys({a: 1});');
            expect(result.valid).toBe(true);
        });

        it('should reject lowercase object as undefined', () => {
            const result = validateCode('var keys = object.keys({a: 1});');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('object');
        });

        it('should accept uppercase String', () => {
            const result = validateCode('var s = String.fromCharCode(65);');
            expect(result.valid).toBe(true);
        });

        it('should reject lowercase string as undefined', () => {
            const result = validateCode('var s = string.fromCharCode(65);');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('string');
        });
    });

    describe('FreeButton API Method Validation', () => {
        it('should reject removed LED global', () => {
            const result = validateCode('led.on(0);');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('not defined');
        });

        it('should reject removed button global', () => {
            const result = validateCode('button.onClick(0, function() {});');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('not defined');
        });

        it('should reject invalid sensor methods', () => {
            const result = validateCode('sensor.invalidMethod();');
            expect(result.valid).toBe(false);
        });

        it('should accept valid sensor API calls', () => {
            const result = validateCode(`
                var temp = sensor.getValue(0);
                var info = sensor.getInfo(0);
            `);
            expect(result.valid).toBe(true);
        });

        it('should reject invalid MQTT methods', () => {
            const result = validateCode('mqtt.invalidMethod();');
            expect(result.valid).toBe(false);
        });

        it('should accept valid MQTT API calls', () => {
            const result = validateCode(`
                mqtt.publish(0, "test/topic", "hello");
                mqtt.subscribe(0, "test/topic", function(topic, msg) {
                    print(msg);
                });
            `);
            expect(result.valid).toBe(true);
        });

        it('should accept fb hierarchy LED access', () => {
            const result = validateCode('fb.main.left.led.setColor(255, 0, 0);');
            expect(result.valid).toBe(true);
        });

        it('should accept fb hierarchy button access', () => {
            const result = validateCode('fb.main.left.button.onClick(function() { print("click"); });');
            expect(result.valid).toBe(true);
        });
    });

    describe('Scope Tracking', () => {
        it('should not flag locally declared variables', () => {
            const result = validateCode(`
                function test() {
                    var localVar = 42;
                    localVar();
                }
            `);
            // localVar is declared, so calling it should only fail if it's not a function
            // But we're checking if it's defined, so this should pass the undefined check
            expect(result.valid).toBe(true);
        });

        it('should track function parameters', () => {
            const result = validateCode(`
                function test(callback) {
                    callback();
                }
            `);
            expect(result.valid).toBe(true);
        });

        it('should track function declarations', () => {
            const result = validateCode(`
                function myFunc() {
                    return 42;
                }
                myFunc();
            `);
            expect(result.valid).toBe(true);
        });

        it('should catch undefined variables used as values', () => {
            const result = validateCode(`
                var temp = sensor.getValue(0);
                var humidity = sensor.getValue(x);
            `);
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain("'x' is not defined");
        });

        it('should allow property access on defined variables', () => {
            const result = validateCode(`
                var obj = {x: 1, y: 2};
                var value = obj.x;
            `);
            expect(result.valid).toBe(true);
        });

        it('should not flag property names as undefined', () => {
            const result = validateCode(`
                var obj = {
                    unknownProp: 42
                };
            `);
            expect(result.valid).toBe(true);
        });

        it('should catch undefined variables in expressions', () => {
            const result = validateCode(`
                var result = undefinedVar + 10;
            `);
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain("'undefinedVar' is not defined");
        });

        it('should catch undefined variables passed as arguments', () => {
            const result = validateCode(`
                print(undefinedVariable);
            `);
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain("'undefinedVariable' is not defined");
        });
    });

    describe('Type Validation', () => {
        it('should reject string literal where number expected', () => {
            const result = validateCode('sensor.getValue("x");');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('expects number');
            expect(result.errors[0]?.message).toContain('got string');
        });

        it('should reject number literal where string expected', () => {
            const result = validateCode('mqtt.subscribe(0, 123, function(){});');
            expect(result.valid).toBe(false);
            expect(result.errors[0]?.message).toContain('expects string');
            expect(result.errors[0]?.message).toContain('got number');
        });

        it('should accept correct literal types', () => {
            const result = validateCode(`
                sensor.getValue(0);
                mqtt.publish(0, "topic", "payload");
            `);
            expect(result.valid).toBe(true);
        });

        it('should accept function expressions for callback parameters', () => {
            const result = validateCode(`
                mqtt.subscribe(0, "topic", function() {
                    print("received");
                });
            `);
            expect(result.valid).toBe(true);
        });

        it('should not validate variable references (unknown type)', () => {
            const result = validateCode(`
                var sensorId = 0;
                sensor.getValue(sensorId);
            `);
            // Should pass because we can't determine type of variables statically
            expect(result.valid).toBe(true);
        });

        it('should catch multiple type errors', () => {
            const result = validateCode(`
                sensor.getValue("0");
                mqtt.publish("0", "topic", "payload");
            `);
            expect(result.valid).toBe(false);
            expect(result.errors.length).toBeGreaterThanOrEqual(2);
        });
    });

    describe('config.json names', () => {
        const names = { keys: ['sceneUp'], displayItems: ['volume'], lists: ['scenes'] };

        it('accepts known button and display item names', () => {
            const result = validateCode([
                'fb.button("sceneUp").button.onClick(function () { print("up"); });',
                'fb.main.display.setValue("volume", "15%");',
                "fb.main.display.setLabel('volume', 'Vol');",
            ].join('\n'), names);
            expect(result.errors).toEqual([]);
        });

        it('reports an unknown button name with line and column', () => {
            const result = validateCode('var x = 1;\n  fb.button("sceneUpp").button.onClick(function () {});', names);
            expect(result.errors).toEqual([{ message: 'unknown button "sceneUpp" in config.json', line: 2, column: 13 }]);
        });

        it('reports an unknown display item name for every name-taking method', () => {
            for (const method of ['remove', 'get', 'setLabel', 'setValue', 'setUnit', 'setPosition', 'setPage', 'setIcon', 'setStyle', 'addStyle', 'removeStyle']) {
                const result = validateCode(`fb.main.display.${method}("vol", 1, 2);`, names);
                expect(result.errors).toEqual([
                    { message: 'unknown display item "vol" in config.json', line: 1, column: method.length + 18 },
                ]);
            }
        });

        it('checks list names for every fb.main.list method', () => {
            for (const method of ['next', 'prev', 'select', 'getSelected', 'count', 'setItems', 'setStyle']) {
                expect(validateCode(`fb.main.list.${method}("scenes", 1);`, names).errors).toEqual([]);
                const result = validateCode(`fb.main.list.${method}("scene", 1);`, names);
                expect(result.errors).toEqual([
                    { message: 'unknown list "scene" in config.json', line: 1, column: method.length + 15 },
                ]);
            }
        });

        it('accepts a list index and keeps lists apart from buttons and display items', () => {
            expect(validateCode('fb.main.list.next(0);\nfb.main.list.setItems(1, ["a"]);', names).errors).toEqual([]);
            const result = validateCode('fb.main.list.next("volume");\nfb.button("scenes");', names);
            expect(result.errors.map(e => e.message)).toEqual([
                'unknown list "volume" in config.json',
                'unknown button "scenes" in config.json',
            ]);
        });

        it('keeps the namespaces apart', () => {
            const result = validateCode('fb.button("volume");\nfb.main.display.setValue("sceneUp", 1);', names);
            expect(result.errors.map(e => e.message)).toEqual([
                'unknown button "volume" in config.json',
                'unknown display item "sceneUp" in config.json',
            ]);
        });

        it('does not check numbers, variables or other methods', () => {
            const result = validateCode([
                'var n = "nope";',
                'fb.button(n);',
                'fb.main.display.setValue(3, "x");',
                'fb.main.display.setValue(n, "x");',
                'fb.main.display.showPage(2);',
                'fb.main.left.display.setLabel("nope");',
            ].join('\n'), names);
            expect(result.errors).toEqual([]);
        });

        it('skips the check without a config.json', () => {
            expect(validateCode('fb.button("anything"); fb.main.display.setValue("x", 1);', null).valid).toBe(true);
            expect(validateCode('fb.button("anything");').valid).toBe(true);
        });
    });

    describe('calls the firmware no longer has', () => {
        const messages = (code: string, n: Parameters<typeof validateCode>[1] = null) => validateCode(code, n).errors.map(e => e.message);
        const FONT = 'give the item a style that sets "font-family" / "font-size" (config.json "styles": "style" in config.json, or setStyle())';

        it('each is reported with what replaces it, with or without a config.json', () => {
            const code = [
                'fb.key("light").display.setLabel("x");',
                'fb.button("light").display.setFont("montserrat", 20);',
                'fb.bars[0].left.display.setTopFont("montserrat", 20);',
                'fb.main.display.setFont("temp", "montserrat", 20);',
                'fb.main.list.setFont("scenes", "montserrat", 3);',
                'fb.main.right.display.setTopStyle("alarm");',
                'fb.main.display.setLabelStyle("temp", "alarm");',
                'fb.main.list.setFocusStyle("scenes", "picked");',
                'fb.main.list.addStyle("scenes", "alarm");',
                'fb.main.list.removeStyle(0, "alarm");',
            ].join('\n');
            const expected = [
                'fb.key() no longer exists: use fb.button("name")',
                `setFont() no longer exists: ${FONT}`,
                'setTopFont() no longer exists: give the button a style whose "title" block sets "font-family" / "font-size"',
                `setFont() no longer exists: ${FONT}`,
                `setFont() no longer exists: ${FONT}`,
                'setTopStyle() no longer exists: use setStyle() with a style that has a "title" block',
                'setLabelStyle() no longer exists: use setStyle() with a style that has a "title" block',
                'setFocusStyle() no longer exists: use setStyle() with a style that has a "selected" block',
                'fb.main.list.addStyle() does not exist: a list has setStyle() only',
                'fb.main.list.removeStyle() does not exist: a list has setStyle() only',
            ];
            expect(messages(code)).toEqual(expected);
            expect(messages(code, { keys: ['light'], displayItems: ['temp'], lists: ['scenes'], styles: { alarm: ['color'], picked: [] } })).toEqual(expected);
        });

        it('says where: the line and column of the call\'s name', () => {
            expect(validateCode('var k =\n  fb.key("a");', null).errors).toEqual([
                { message: 'fb.key() no longer exists: use fb.button("name")', line: 2, column: 6 },
            ]);
        });

        it('with arguments that are no literals too: the call itself is gone', () => {
            expect(messages('var n = "a", f = "x"; fb.key(n); fb.button(n).display.setFont(f, 3);')).toHaveLength(2);
        });

        it('what still exists is not reported; another object\'s method of that name neither', () => {
            expect(messages([
                'fb.button("a").display.setStyle("x"); fb.button("a").display.addStyle("x"); fb.button("a").display.removeStyle("x");',
                'fb.main.display.setStyle(1, "x"); fb.main.display.addStyle(1, "x"); fb.main.display.removeStyle(1, "x");',
                'fb.main.list.setStyle(0, "x");',
                'var my = { key: function () {}, setFont: function () {} }; my.key(); my.setFont("a");',
            ].join('\n'))).toEqual([]);
        });

        it('a "display" member of the script\'s own objects is not a button\'s display: nothing is said', () => {
            const styles = { styles: styleNames({ styles: { alarm: { color: '#ff0000' } } }), keys: ['light'], displayItems: [], lists: [] };
            for (const n of [null, styles]) {
                expect(messages([
                    'var w = { display: { setStyle: function () {}, setFont: function () {}, setTopFont: function () {}, addStyle: function () {} } };',
                    'w.display.setStyle("x"); w.display.setFont("x"); w.display.setTopFont("x"); w.display.addStyle(5); w.display.setTopStyle("y");',
                    // Deeper, computed, a call result, this: none is provably a button.
                    'var app = { panel: w, panels: [w] }; app.panel.display.setFont("x"); app.panels[0].display.setStyle("nosuch");',
                    'function panel() { return w; } panel().display.setFont("x"); panel().display.removeStyle("nosuch");',
                    'function Thing() { this.display = w.display; this.display.setFont("x"); }',
                    // A member of fb.main that is no button, and an fb-like object of the script.
                    'var mine = { main: { left: w, display: w.display }, bars: [{ left: w }], button: function () { return w; } };',
                    'mine.main.left.display.setFont("x"); mine.bars[0].left.display.setStyle("nosuch"); mine.button("a").display.setTopFont("x");',
                    // A variable that holds a button and later something else, a parameter of that name.
                    'var b = fb.button("light"); b = w; b.display.setFont("x"); b.display.setStyle("nosuch");',
                    'function f(k) { k.display.setFont("x"); k.display.setStyle("nosuch"); } var k = fb.button("light"); f(w);',
                ].join('\n'), n), n ? 'with styles' : 'without').toEqual([]);
            }
        });

        it('a button is recognised where the receiver provably is one: fb.button(), fb.main and fb.bars sides, a variable of one', () => {
            const styles = { styles: styleNames({ styles: { alarm: { color: '#ff0000' } } }), keys: ['light'], displayItems: [], lists: [] };
            expect(messages([
                'fb.button("light").display.setFont("x", 3);',
                'fb.main.left.display.setFont("x", 3); fb.main.right.display.setTopFont("x", 3);',
                'fb.bars[0].left.display.setFont("x", 3); fb.bars[1].right.display.setTopStyle("alarm");',
                'var i = 0; fb.bars[i].right.display.setFont("x", 3);',
                // A variable that only ever holds a button: declared with one, or assigned one later.
                'var a = fb.button("light"); a.display.setFont("x", 3); a.display.setStyle("nosuch");',
                'var c; c = fb.main.left; c = fb.bars[0].right; c.display.setTopFont("x", 3); c.display.addStyle("");',
            ].join('\n'), styles)).toEqual([
                `setFont() no longer exists: ${FONT}`,
                `setFont() no longer exists: ${FONT}`,
                'setTopFont() no longer exists: give the button a style whose "title" block sets "font-family" / "font-size"',
                `setFont() no longer exists: ${FONT}`,
                'setTopStyle() no longer exists: use setStyle() with a style that has a "title" block',
                `setFont() no longer exists: ${FONT}`,
                `setFont() no longer exists: ${FONT}`,
                'unknown style "nosuch"',
                'setTopFont() no longer exists: give the button a style whose "title" block sets "font-family" / "font-size"',
                'addStyle() requires a style name (a string)',
            ]);
            // fb.main.display and fb.main.list are not buttons: their own rules apply.
            expect(messages('fb.main.display.setTopFont("x"); fb.main.list.setTopFont("x");', styles)).toEqual([]);
        });
    });

    describe('config.json styles', () => {
        // What the IDE gives the validator for a config (font-config styleNames): per style,
        // the dotted properties fb.style.set can set for it.
        const names = {
            keys: ['light'], displayItems: ['temp'], lists: ['scenes'],
            styles: styleNames({
                styles: {
                    button: { opacity: 1, icon: { color: '#0000ff' } },
                    alarm: { color: '#ff0000', opacity: 1, title: { color: '#ff8080' } },
                    big: { 'font-family': 'montserrat', 'font-size': 48, 'line-height': 1.5 },
                    picked: { selected: { color: '#0000ff', 'background-color': '#00c853' } },
                    spaced: { 'letter-spacing': 3, 'text-decoration': 'underline', 'text-align': 'left' },
                    tint: { icon: { color: '#00ff00' } }, muted: { color: '#808080' }, small: { 'font-size': 20 }, empty: {},
                },
            }),
        };
        const messages = (code: string, n: typeof names | Omit<typeof names, 'styles'> = names) => validateCode(code, n).errors.map(e => e.message);

        it('accepts known styles, lists of them, "" and what cannot be checked', () => {
            expect(messages([
                'fb.main.display.setStyle("temp", "alarm");',
                'fb.main.display.setStyle("temp", "big alarm  muted small");',
                'fb.main.display.setStyle(7, "");',
                'fb.main.display.addStyle("temp", "alarm"); fb.main.display.removeStyle("temp", "alarm");',
                'fb.main.list.setStyle("scenes", "alarm picked");',
                'fb.main.list.setStyle(0, "");',
                'fb.button("light").display.setStyle("big alarm");',
                'fb.button("light").display.setStyle("");',
                'fb.bars[0].left.display.addStyle("alarm"); fb.main.right.display.removeStyle("big");',
                'fb.main.display.create(0, 0, 40, 0, "big alarm", 0, "L", "u", 0, 1); fb.main.display.create(0, 0, 40, 0, "", 0, "L", "u", 0, 1);',
                'var s = "x"; fb.main.display.setStyle("temp", s); fb.button("light").display.setStyle(s); fb.main.display.addStyle("temp", s);',
                'fb.main.display.create(0, 0, 40, 0, s, 0, "L", "u", 0, 1); fb.main.list.setStyle(0, s + " alarm");',
                'fb.style.set("alarm", "color", "#00ff00");',
                'fb.style.set("alarm", "opacity", 0.5);',
                'fb.style.set("alarm", "title.color", "#00ff00");',
                'fb.style.set("picked", "selected.background-color", "#00c85380");',
                'fb.style.set("tint", "icon.color", "#ffffff");',
                'fb.style.set("spaced", "letter-spacing", -3); fb.style.set("spaced", "text-decoration", "line-through underline"); fb.style.set("spaced", "text-align", "right");',
                'fb.style.set("button", "color", "#00ff00"); fb.style.set("displayitem", "unit.color", "#00ff00"); fb.style.set("list", "selected.background-color", "#0000ff");',
                'var p = "color", v = 5; fb.style.set("alarm", p, v); fb.style.set(s, "color", v); fb.style.set("alarm", "color", v); fb.style.set(s, p, v);',
            ].join('\n'))).toEqual([]);
        });

        it('setStyle and display.create: the device\'s errors for a list of styles', () => {
            expect(messages([
                'fb.main.display.setStyle("temp", "alarn");',
                'fb.main.display.setStyle("temp", "alarm Alarm");',
                'fb.main.list.setStyle("scenes", "nosuch");',
                'fb.button("light").display.setStyle("alarm nosuch big");',
                'fb.main.display.create(0, 0, 10, 0, "nosuch", 0, "x", "", 0, 1);',
                // Not a string: TypeError.
                'fb.main.display.setStyle("temp", 3);',
                'fb.button("light").display.setStyle(null);',
                'fb.main.list.setStyle("scenes", true);',
                'fb.main.display.create(0, 0, 10, 0, 3, 0, "x", "", 0, 1);',
                // The style of a kind: TypeError.
                'fb.button("light").display.setStyle("button");',
                'fb.main.display.setStyle("temp", "alarm list");',
                'fb.main.list.setStyle("scenes", "displayitem");',
                'fb.main.display.create(0, 0, 10, 0, "displayitem", 0, "x", "", 0, 1);',
                // A fifth name: RangeError; a name given twice counts once.
                'fb.main.display.setStyle("temp", "alarm muted big small empty");',
                'fb.button("light").display.setStyle("alarm muted big small empty");',
                'fb.main.list.setStyle("scenes", "alarm muted big small empty");',
                'fb.main.display.setStyle("temp", "alarm muted alarm big small alarm");',
            ].join('\n'))).toEqual([
                'unknown style "alarn"', 'unknown style "Alarm"', 'unknown style "nosuch"', 'unknown style "nosuch"', 'unknown style "nosuch"',
                'display.setStyle() requires style names (a string)',
                'setStyle() requires style names (a string)',
                'fb.main.list.setStyle() requires style names (a string)',
                'display.create() requires style names (a string)',
                'setStyle(): "button" is the style of a kind and cannot be named by an item',
                'display.setStyle(): "list" is the style of a kind and cannot be named by an item',
                'fb.main.list.setStyle(): "displayitem" is the style of a kind and cannot be named by an item',
                'display.create(): "displayitem" is the style of a kind and cannot be named by an item',
                'display.setStyle(): an item has at most 4 styles',
                'setStyle(): an item has at most 4 styles',
                'fb.main.list.setStyle(): an item has at most 4 styles',
            ]);
            const result = validateCode('fb.main.display.setStyle("temp",\n  "alarn");', names);
            expect(result.errors[0]).toMatchObject({ line: 2, column: 3 });
            // The item and list names are still checked.
            expect(messages('fb.main.display.setStyle("nope", "alarm"); fb.main.list.setStyle("nolist", "picked");')).toEqual([
                'unknown display item "nope" in config.json', 'unknown list "nolist" in config.json',
            ]);
        });

        it('addStyle and removeStyle: one style name', () => {
            expect(messages([
                'fb.button("light").display.addStyle("nosuch");',
                'fb.button("light").display.removeStyle("nosuch");',
                // Not a list of names.
                'fb.button("light").display.addStyle("alarm muted");',
                'fb.main.display.addStyle("temp", "nosuch");',
                'fb.main.display.removeStyle("temp", "alarm muted");',
                'fb.button("light").display.addStyle("");',
                'fb.button("light").display.removeStyle(5);',
                'fb.main.display.addStyle("temp", "");',
                'fb.main.display.removeStyle("temp", null);',
                'fb.button("light").display.addStyle("button");',
                'fb.main.display.removeStyle("temp", "displayitem");',
            ].join('\n'))).toEqual([
                'unknown style "nosuch"', 'unknown style "nosuch"', 'unknown style "alarm muted"', 'unknown style "nosuch"',
                'unknown style "alarm muted"',
                'addStyle() requires a style name (a string)',
                'removeStyle() requires a style name (a string)',
                'display.addStyle() requires a style name (a string)',
                'display.removeStyle() requires a style name (a string)',
                'addStyle(): "button" is the style of a kind and cannot be named by an item',
                'display.removeStyle(): "displayitem" is the style of a kind and cannot be named by an item',
            ]);
        });

        it('fb.style.set: the style, the property and the block', () => {
            expect(messages([
                'fb.style.set("nosuch", "color", "#ffffff");',
                'fb.style.set("Button", "color", "#ffffff");',
                'fb.style.set("", "color", "#ffffff");',
                'fb.style.set(7, "color", "#00ff00");',
                'fb.style.set("alarm", 5, "#00ff00");',
                'fb.style.set("alarm", "color");',
                // A block the style does not have in config.json cannot be made.
                'fb.style.set("empty", "color", "#ffffff");',
                'fb.style.set("picked", "opacity", 0.5);',
                'fb.style.set("muted", "title.color", "#ffffff");',
                'fb.style.set("alarm", "unit.color", "#ffffff");',
                'fb.style.set("alarm", "selected.background-color", "#ffffff");',
                // Per font: fixed.
                'fb.style.set("big", "font-family", "montserrat");',
                'fb.style.set("big", "font-size", 20);',
                'fb.style.set("big", "line-height", 2);',
                'fb.style.set("alarm", "title.font-size", 30);',
                // Not a property.
                'fb.style.set("alarm", "text_color", "#ffffff");',
                'fb.style.set("alarm", "font-weight", 700);',
                'fb.style.set("alarm", "main.color", "#ffffff");',
                'fb.style.set("alarm", "title.colour", "#ffffff");',
                'fb.style.set("alarm", "constructor", "#ffffff");',
                'var v = 1; fb.style.set("muted", "title.background-color", v);',
            ].join('\n'))).toEqual([
                'unknown style "nosuch"', 'unknown style "Button"',
                'fb.style.set() requires a style name, a property name and a value',
                'fb.style.set() requires a style name, a property name and a value',
                'fb.style.set() requires a style name, a property name and a value',
                'fb.style.set() requires a style name, a property name and a value',
                'fb.style.set(): the style has no main block in config.json',
                'fb.style.set(): the style has no main block in config.json',
                'fb.style.set(): the style has no "title" block in config.json',
                'fb.style.set(): the style has no "unit" block in config.json',
                'fb.style.set(): the style has no "selected" block in config.json',
                'fb.style.set(): "font-family" cannot be changed by a script',
                'fb.style.set(): "font-size" cannot be changed by a script',
                'fb.style.set(): "line-height" cannot be changed by a script',
                'fb.style.set(): "title.font-size" cannot be changed by a script',
                'fb.style.set(): unknown property "text_color"',
                'fb.style.set(): unknown property "font-weight"',
                'fb.style.set(): unknown property "main.color"',
                'fb.style.set(): unknown property "title.colour"',
                'fb.style.set(): unknown property "constructor"',
                'fb.style.set(): the style has no "title" block in config.json',
            ]);
        });

        it('fb.style.set adds a property the block does not set in config.json', () => {
            expect(messages([
                'fb.style.set("alarm", "text-align", "left"); fb.style.set("alarm", "title.opacity", 0.5);',
                'fb.style.set("big", "color", "#ff0000"); fb.style.set("small", "text-decoration", "underline");',
                'fb.style.set("picked", "selected.letter-spacing", 2); fb.style.set("muted", "background-color", "#00000080");',
                // An icon colour: every style of "styles", "button" and "displayitem".
                'fb.style.set("alarm", "icon.color", "#ffffff"); fb.style.set("empty", "icon.color", "#ffffff");',
                'fb.style.set("displayitem", "icon.color", "#ffffff");',
                'fb.style.set("displayitem", "opacity", 0.5); fb.style.set("button", "title.letter-spacing", 1);',
                'fb.style.set("list", "text-decoration", "underline"); fb.style.set("list", "selected.opacity", 0.5);',
            ].join('\n'))).toEqual([]);
        });

        it('fb.style.set: the order of the checks is the device\'s (the name, the fixed three, the block, the value)', () => {
            expect(messages([
                // 14.5 is no font size, but the device says first that the size cannot be changed.
                'fb.style.set("big", "font-size", 14.5);',
                'fb.style.set("big", "line-height", 1.234);',
                'fb.style.set("big", "font-size", "large");',
                'fb.style.set("muted", "title.font-size", 14.5);',
                'fb.style.set("big", "font-weight", 14.5);',
                'fb.style.set("muted", "title.opacity", 0.333);',
                'fb.style.set("muted", "title.color", "red");',
                'fb.style.set("muted", "opacity", 0.333);',
            ].join('\n'))).toEqual([
                'fb.style.set(): "font-size" cannot be changed by a script',
                'fb.style.set(): "line-height" cannot be changed by a script',
                'fb.style.set(): "font-size" cannot be changed by a script',
                'fb.style.set(): "title.font-size" cannot be changed by a script',
                'fb.style.set(): unknown property "font-weight"',
                'fb.style.set(): the style has no "title" block in config.json',
                'fb.style.set(): the style has no "title" block in config.json',
                'fb.style.set(): not a valid value for "opacity" (it takes a number 0..1 with at most two decimals)',
            ]);
        });

        it('a style named like a member of Object.prototype is a style like any other; such a name that is no style is unknown', () => {
            // JSON.parse: "__proto__" as an own key, as a config.json has it.
            const odd = {
                ...names,
                styles: styleNames(JSON.parse('{"styles": {"constructor": {"color": "#ff0000"}, "__proto__": {"font-size": 20, "title": {"color": "#00ff00"}}}}')),
            };
            expect(Object.keys(odd.styles).sort()).toEqual(['__proto__', 'button', 'constructor', 'displayitem', 'list']);
            expect(messages([
                'fb.main.display.setStyle("temp", "constructor __proto__"); fb.button("light").display.addStyle("__proto__");',
                'fb.main.list.setStyle("scenes", "constructor"); fb.main.display.removeStyle("temp", "constructor");',
                'fb.style.set("constructor", "color", "#00ff00"); fb.style.set("__proto__", "title.color", "#00ff00"); fb.style.set("__proto__", "opacity", 0.5);',
            ].join('\n'), odd)).toEqual([]);
            expect(messages([
                'fb.style.set("constructor", "title.color", "#00ff00");',
                'fb.style.set("__proto__", "font-size", 30);',
                'fb.style.set("toString", "color", "#00ff00");',
                'fb.main.display.setStyle("temp", "hasOwnProperty"); fb.button("light").display.addStyle("valueOf");',
            ].join('\n'), odd)).toEqual([
                'fb.style.set(): the style has no "title" block in config.json',
                'fb.style.set(): "font-size" cannot be changed by a script',
                'unknown style "toString"', 'unknown style "hasOwnProperty"', 'unknown style "valueOf"',
            ]);
            // The config of this file has no such styles: every one of the names is unknown.
            expect(messages([
                'fb.style.set("constructor", "color", "#00ff00"); fb.style.set("__proto__", "color", "#00ff00");',
                'fb.main.display.setStyle("temp", "constructor"); fb.main.display.addStyle("temp", "__proto__"); fb.main.list.setStyle("scenes", "toString");',
            ].join('\n'))).toEqual([
                'unknown style "constructor"', 'unknown style "__proto__"', 'unknown style "constructor"', 'unknown style "__proto__"', 'unknown style "toString"',
            ]);
        });

        it('fb.style.set: a value as config.json takes it', () => {
            expect(messages([
                'fb.style.set("alarm", "color", "red");',
                'fb.style.set("alarm", "color", "ff0000");',
                'fb.style.set("alarm", "color", "#ff000080");',
                'fb.style.set("alarm", "color", 5);',
                'fb.style.set("alarm", "opacity", 1.5);',
                'fb.style.set("alarm", "opacity", 0.333);',
                'fb.style.set("alarm", "opacity", "0.5");',
                'fb.style.set("alarm", "opacity", -1);',
                'fb.style.set("spaced", "letter-spacing", 2.5);',
                'fb.style.set("spaced", "letter-spacing", 101);',
                'fb.style.set("spaced", "text-decoration", "blink");',
                'fb.style.set("tint", "icon.color", "green");',
            ].join('\n'))).toEqual([
                'fb.style.set(): not a valid value for "color" (it takes a colour "#rrggbb")',
                'fb.style.set(): not a valid value for "color" (it takes a colour "#rrggbb")',
                'fb.style.set(): not a valid value for "color" (it takes a colour "#rrggbb")',
                'fb.style.set(): not a valid value for "color" (it takes a colour "#rrggbb")',
                'fb.style.set(): not a valid value for "opacity" (it takes a number 0..1 with at most two decimals)',
                'fb.style.set(): not a valid value for "opacity" (it takes a number 0..1 with at most two decimals)',
                'fb.style.set(): not a valid value for "opacity" (it takes a number 0..1 with at most two decimals)',
                'fb.style.set(): not a valid value for "opacity" (it takes a number 0..1 with at most two decimals)',
                'fb.style.set(): not a valid value for "letter-spacing" (it takes an integer -100..100 (px))',
                'fb.style.set(): not a valid value for "letter-spacing" (it takes an integer -100..100 (px))',
                'fb.style.set(): not a valid value for "text-decoration" (it takes "none", "underline", "line-through" or "underline line-through")',
                'fb.style.set(): not a valid value for "icon.color" (it takes a colour "#rrggbb")',
            ]);
        });

        it('fb.style.set on "button", "displayitem" and "list": every part the kind has', () => {
            expect(messages([
                'fb.style.set("button", "unit.color", "#ffffff");',
                'fb.style.set("button", "selected.color", "#ffffff");',
                'fb.style.set("list", "title.color", "#ffffff");',
                'fb.style.set("list", "icon.color", "#ffffff");',
                'fb.style.set("displayitem", "selected.opacity", 0.5);',
                'fb.style.set("list", "font-size", 20);',
                'fb.style.set("button", "colour", "#ffffff");',
                'fb.style.set("displayitem", "color", "white");',
            ].join('\n'))).toEqual([
                'fb.style.set(): the style of this kind has no "unit" part',
                'fb.style.set(): the style of this kind has no "selected" part',
                'fb.style.set(): the style of this kind has no "title" part',
                'fb.style.set(): the style of this kind has no "icon" part',
                'fb.style.set(): the style of this kind has no "selected" part',
                'fb.style.set(): "font-size" cannot be changed by a script',
                'fb.style.set(): unknown property "colour"',
                'fb.style.set(): not a valid value for "color" (it takes a colour "#rrggbb")',
            ]);
        });

        it('fb.style.set: a literal true, false or null value is no string or number', () => {
            expect(messages([
                'fb.style.set("alarm", "color", true);',
                'fb.style.set("alarm", "opacity", false);',
                'fb.style.set("alarm", "color", null);',
                // whatever the property is, as on the device
                'var p = "color"; fb.style.set("alarm", p, null);',
                'fb.style.set("alarm", "text-align", true);',
            ].join('\n'))).toEqual(Array(5).fill('fb.style.set(): the value must be a string or a number'));
            // Not a literal of those: not judged.
            expect(messages('var v = true; fb.style.set("alarm", "color", v); fb.style.set("alarm", "opacity", 1); fb.style.set("alarm", "color", "#ffffff");')).toEqual([]);
        });

        it('with a style that is no literal: what holds for every style is still checked', () => {
            expect(messages([
                'var s = "alarm";',
                'fb.style.set(s, "color", "#ffffff"); fb.style.set(s, "opacity", 0.5); fb.style.set(s, "icon.color", "#ffffff");',
                'fb.style.set(s, "font-size", 20);',
                'fb.style.set(s, "colour", "#ffffff");',
                'fb.style.set(s, "color", "red");',
            ].join('\n'))).toEqual([
                'fb.style.set(): "font-size" cannot be changed by a script',
                'fb.style.set(): unknown property "colour"',
                'fb.style.set(): not a valid value for "color" (it takes a colour "#rrggbb")',
            ]);
        });

        it('the styles of the kinds always exist: a config without "styles" has the calls too', () => {
            const kinds = { ...names, styles: styleNames({}) };
            expect(messages('fb.style.set("button", "color", "#ffffff"); fb.main.display.setStyle("temp", ""); fb.main.list.setStyle(0, "");', kinds)).toEqual([]);
            expect(messages('fb.main.display.setStyle("temp", "alarm");', kinds)).toEqual(['unknown style "alarm"']);
        });

        it('skips the checks without a config.json, and where the styles are not known', () => {
            expect(validateCode('fb.style.set("x", "y", 1); fb.main.display.setStyle("a", "b");', null).valid).toBe(true);
            const { styles: _styles, ...plain } = names;
            expect(messages('fb.style.set("x", "y", 1); fb.main.display.setStyle("temp", "b"); fb.button("light").display.addStyle(5);', plain)).toEqual([]);
        });
    });

    describe('config.json fonts', () => {
        const names = { keys: [], displayItems: ['temp'], lists: ['scenes'], fonts: ['montserrat', 'montserrat_semibold', 'mdi', 'roboto'], fontIds: [2] };

        it('knows the system fonts without a "fonts" table', () => {
            const plain = { keys: [], displayItems: ['temp'], lists: [] };
            const warnings = (text: string) => validateCode(`fb.main.display.setValue("temp", "${text}");`, plain).warnings.map(w => w.message);
            expect(warnings('\\\\f{montserrat}1 \\\\f{montserrat_semibold}2')).toEqual([]);
            expect(warnings('\\\\f{roboto}1')).toEqual([
                'unknown font "roboto" in \\f{roboto} (not a system font or a config.json "fonts" entry; the item\'s font is used)',
            ]);
        });

        it('warns about an unknown \\f{font} in display text (still valid)', () => {
            const result = validateCode([
                'fb.main.display.setValue("temp", "\\\\f{roboto}21\\\\s{20}C");',
                'fb.main.display.setLabel("temp", "\\\\f{2}a \\\\f{mono}b \\\\f{9}c \\\\f{}d");',
                'fb.main.display.setUnit("temp", "\\\\f{nope}x");',
            ].join('\n'), names);
            expect(result.valid).toBe(true);
            expect(result.errors).toEqual([]);
            expect(result.warnings.map(w => [w.message, w.line])).toEqual([
                [expect.stringContaining('unknown font "mono" in \\f{mono}'), 2],
                [expect.stringContaining('unknown font "9" in \\f{9}'), 2],
                [expect.stringContaining('unknown font "nope"'), 3],
            ]);
        });
    });
});
