/**
 * MQuickJS Validator Tests
 * Tests ES5 strict mode and MQuickJS stricter mode enforcement
 */

import { describe, it, expect } from 'vitest';
import { validateCode } from './validator';

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

        it('accepts known key and display item names', () => {
            const result = validateCode([
                'fb.key("sceneUp").button.onClick(function () { print("up"); });',
                'fb.main.display.setValue("volume", "15%");',
                "fb.main.display.setLabel('volume', 'Vol');",
            ].join('\n'), names);
            expect(result.errors).toEqual([]);
        });

        it('reports an unknown key name with line and column', () => {
            const result = validateCode('var x = 1;\n  fb.key("sceneUpp").button.onClick(function () {});', names);
            expect(result.errors).toEqual([{ message: 'unknown key "sceneUpp" in config.json', line: 2, column: 10 }]);
        });

        it('reports an unknown display item name for every name-taking method', () => {
            for (const method of ['remove', 'get', 'setLabel', 'setValue', 'setUnit', 'setPosition', 'setPage']) {
                const result = validateCode(`fb.main.display.${method}("vol", 1, 2);`, names);
                expect(result.errors).toEqual([
                    { message: 'unknown display item "vol" in config.json', line: 1, column: method.length + 18 },
                ]);
            }
        });

        it('checks list names for every fb.main.list method', () => {
            for (const method of ['next', 'prev', 'select', 'getSelected', 'count', 'setItems']) {
                expect(validateCode(`fb.main.list.${method}("scenes", 1);`, names).errors).toEqual([]);
                const result = validateCode(`fb.main.list.${method}("scene", 1);`, names);
                expect(result.errors).toEqual([
                    { message: 'unknown list "scene" in config.json', line: 1, column: method.length + 15 },
                ]);
            }
        });

        it('accepts a list index and keeps lists apart from keys and display items', () => {
            expect(validateCode('fb.main.list.next(0);\nfb.main.list.setItems(1, ["a"]);', names).errors).toEqual([]);
            const result = validateCode('fb.main.list.next("volume");\nfb.key("scenes");', names);
            expect(result.errors.map(e => e.message)).toEqual([
                'unknown list "volume" in config.json',
                'unknown key "scenes" in config.json',
            ]);
        });

        it('keeps the namespaces apart', () => {
            const result = validateCode('fb.key("volume");\nfb.main.display.setValue("sceneUp", 1);', names);
            expect(result.errors.map(e => e.message)).toEqual([
                'unknown key "volume" in config.json',
                'unknown display item "sceneUp" in config.json',
            ]);
        });

        it('does not check numbers, variables or other methods', () => {
            const result = validateCode([
                'var n = "nope";',
                'fb.key(n);',
                'fb.main.display.setValue(3, "x");',
                'fb.main.display.setValue(n, "x");',
                'fb.main.display.showPage(2);',
                'fb.main.left.display.setLabel("nope");',
            ].join('\n'), names);
            expect(result.errors).toEqual([]);
        });

        it('skips the check without a config.json', () => {
            expect(validateCode('fb.key("anything"); fb.main.display.setValue("x", 1);', null).valid).toBe(true);
            expect(validateCode('fb.key("anything");').valid).toBe(true);
        });
    });

    describe('config.json named styles', () => {
        const names = {
            keys: ['light'], displayItems: ['temp'], lists: ['scenes'],
            styles: { alarm: ['text_color', 'text_opa'], big: ['text_font', 'text_size'], picked: ['text_color', 'bg_color'], empty: [] as string[] },
        };
        const messages = (code: string, n: typeof names | Omit<typeof names, 'styles'> = names) => validateCode(code, n).errors.map(e => e.message);

        it('accepts known styles, "" and what cannot be checked', () => {
            expect(messages([
                'fb.main.display.setStyle("temp", "alarm");',
                'fb.main.display.setLabelStyle("temp", "big");',
                'fb.main.display.setStyle(7, "");',
                'fb.main.list.setStyle("scenes", "alarm");',
                'fb.main.list.setFocusStyle("scenes", "picked");',
                'fb.main.list.setStyle(0, "picked");',
                'fb.key("light").display.setStyle("big");',
                'fb.key("light").display.setTopStyle("");',
                'fb.bars[0].left.display.setTopStyle("alarm");',
                'var s = "x"; fb.main.display.setStyle("temp", s); fb.key("light").display.setStyle(s);',
                'fb.style.set("alarm", "text_color", "#00ff00");',
                'fb.style.set("alarm", "text_opa", 128);',
                'fb.style.set("picked", "bg_color", "00c853");',
                'var p = "text_color", v = 5; fb.style.set("alarm", p, v); fb.style.set(s, "anything", v); fb.style.set("alarm", "text_color", v);',
            ].join('\n'))).toEqual([]);
        });

        it('reports an unknown style where the device throws ReferenceError', () => {
            expect(messages([
                'fb.main.display.setStyle("temp", "alarn");',
                'fb.main.display.setLabelStyle("temp", "Alarm");',
                'fb.main.list.setStyle("scenes", "nosuch");',
                'fb.main.list.setFocusStyle("scenes", "nosuch");',
                'fb.key("light").display.setStyle("nosuch");',
                'fb.key("light").display.setTopStyle("nosuch");',
                'fb.style.set("nosuch", "text_color", "#ffffff");',
                'fb.style.set("", "text_color", "#ffffff");',
            ].join('\n'))).toEqual([
                'unknown style "alarn" in config.json', 'unknown style "Alarm" in config.json', 'unknown style "nosuch" in config.json',
                'unknown style "nosuch" in config.json', 'unknown style "nosuch" in config.json', 'unknown style "nosuch" in config.json',
                'unknown style "nosuch" in config.json', 'unknown style "" in config.json',
            ]);
            const result = validateCode('fb.main.display.setStyle("temp",\n  "alarn");', names);
            expect(result.errors[0]).toMatchObject({ line: 2, column: 3 });
            // The item and list names are still checked.
            expect(messages('fb.main.display.setStyle("nope", "alarm"); fb.main.list.setFocusStyle("nolist", "picked");')).toEqual([
                'unknown display item "nope" in config.json', 'unknown list "nolist" in config.json',
            ]);
        });

        it('reports a style that is not a string', () => {
            expect(messages('fb.main.display.setStyle("temp", 3); fb.key("light").display.setStyle(null); fb.style.set(1, "text_color", "#fff");')).toEqual([
                'fb.main.display.setStyle(): the style must be a string (the name of a "style.styles" entry)',
                'display.setStyle(): the style must be a string (the name of a "style.styles" entry)',
                'fb.style.set(): the style must be a string (the name of a "style.styles" entry)',
            ]);
        });

        it('reports a style with bg_* on a key or a display item; a list takes it', () => {
            expect(messages([
                'fb.main.display.setStyle("temp", "picked");',
                'fb.main.display.setLabelStyle("temp", "picked");',
                'fb.key("light").display.setStyle("picked");',
                'fb.main.right.display.setTopStyle("picked");',
                'fb.main.list.setStyle("scenes", "picked");',
                'fb.main.list.setFocusStyle("scenes", "picked");',
            ].join('\n'))).toEqual(Array(4).fill('style "picked" sets bg_color: it is for lists only (a key or display item has no background)'));
        });

        it('fb.style.set: only a property the style sets, not its font or size, a value as in config.json', () => {
            expect(messages([
                'fb.style.set("alarm", "text_align", "left");',
                'fb.style.set("empty", "text_color", "#ffffff");',
                'fb.style.set("big", "text_font", "montserrat");',
                'fb.style.set("big", "text_size", 20);',
                'fb.style.set("alarm", "text_size", 20);',
                'fb.style.set("alarm", "colour", "#ffffff");',
                'fb.style.set("alarm", "text_color", "red");',
                'fb.style.set("alarm", "text_color", 5);',
                'fb.style.set("alarm", "text_opa", 256);',
                'fb.style.set("alarm", "text_opa", -1);',
                'fb.style.set("alarm", "text_opa", "128");',
                'fb.style.set("alarm", 3, 1);',
                'var v = 1; fb.style.set("alarm", "bg_opa", v);',
            ].join('\n'))).toEqual([
                'style "alarm" does not set text_align in config.json: fb.style.set() changes only what the style sets (text_color, text_opa)',
                'style "empty" does not set text_color in config.json: fb.style.set() changes only what the style sets',
                'fb.style.set() cannot change text_font: use another named style for another font or size',
                'fb.style.set() cannot change text_size: use another named style for another font or size',
                'fb.style.set() cannot change text_size: use another named style for another font or size',
                '"colour" is not a style property',
                'text_color must be a colour "#rrggbb"',
                'text_color must be a colour "#rrggbb"',
                'text_opa must be an integer 0..255',
                'text_opa must be an integer 0..255',
                'text_opa must be an integer 0..255',
                'fb.style.set(): the property must be a string ("text_color", ...)',
                'style "alarm" does not set bg_opa in config.json: fb.style.set() changes only what the style sets (text_color, text_opa)',
            ]);
        });

        it('fb.style.set: a literal true, false or null value is no string or number', () => {
            expect(messages([
                'fb.style.set("alarm", "text_color", true);',
                'fb.style.set("alarm", "text_opa", false);',
                'fb.style.set("alarm", "text_color", null);',
                // whatever the property is, as on the device
                'var p = "text_color"; fb.style.set("alarm", p, null);',
                'fb.style.set("alarm", "text_align", true);',
            ].join('\n'))).toEqual(Array(5).fill('fb.style.set(): the value must be a string or a number'));
            // Not a literal of those: not judged.
            expect(messages('var v = true; fb.style.set("alarm", "text_color", v); fb.style.set("alarm", "text_opa", 1); fb.style.set("alarm", "text_color", "#ffffff");')).toEqual([]);
        });

        it('without named styles in config.json the calls do not exist on the device', () => {
            const { styles: _styles, ...plain } = names;
            const expected = (call: string) => `${call}() needs named styles: config.json has no "style.styles", so the device does not have this call`;
            expect(messages([
                'fb.main.display.setStyle("temp", "alarm");',
                'fb.main.display.setLabelStyle("temp", "");',
                'fb.main.list.setStyle("scenes", "alarm");',
                'fb.main.list.setFocusStyle(0, x);',
                'fb.key("light").display.setStyle("alarm");',
                'fb.main.left.display.setTopStyle("alarm");',
                'fb.style.set("alarm", "text_color", "#ffffff");',
            ].join('\n').replace('x)', '"a")'), plain)).toEqual([
                expected('fb.main.display.setStyle'), expected('fb.main.display.setLabelStyle'), expected('fb.main.list.setStyle'),
                expected('fb.main.list.setFocusStyle'), expected('display.setStyle'), expected('display.setTopStyle'), expected('fb.style.set'),
            ]);
            // The font calls and everything else are as before.
            expect(messages('fb.key("light").display.setFont("montserrat", 2); fb.main.display.setValue("temp", "1");', plain)).toEqual([]);
        });

        it('skips the checks without a config.json', () => {
            expect(validateCode('fb.style.set("x", "y", 1); fb.main.display.setStyle("a", "b");', null).valid).toBe(true);
        });
    });

    describe('config.json fonts', () => {
        const names = { keys: [], displayItems: ['temp'], lists: ['scenes'], fonts: ['montserrat', 'montserrat_semibold', 'mdi', 'roboto'], fontIds: [2] };

        it('accepts known font names and ids in setFont / setTopFont', () => {
            const result = validateCode([
                'fb.main.display.setFont("temp", "roboto", 40);',
                'fb.main.display.setFont("temp", 2, 40);',
                'fb.main.list.setFont("scenes", "mdi", 3);',
                'fb.key(1).display.setFont("montserrat", 28);',
                'fb.key(1).display.setTopFont(2, 16);',
                'var f = "x"; fb.key(1).display.setFont(f, 1);',
                'fb.main.display.setFont("temp", "", 0);',
                'fb.key(1).display.setFont("");',
                'fb.key(1).display.setTopFont("");',
                'fb.main.list.setFont("scenes", "");',
            ].join('\n'), names);
            expect(result.errors).toEqual([]);
            expect(result.warnings).toEqual([]);
        });

        it('reports unknown fonts as errors (the device throws ReferenceError)', () => {
            const result = validateCode([
                'fb.main.display.setFont("temp", "robot", 40);',
                'fb.main.list.setFont(0, 7, 3);',
                'fb.key(1).display.setTopFont("mono", 16);',
            ].join('\n'), names);
            expect(result.errors.map(e => [e.message, e.line])).toEqual([
                ['unknown font "robot" in config.json', 1],
                ['unknown font id 7 in config.json', 2],
                ['unknown font "mono" in config.json', 3],
            ]);
        });

        it('reports a literal size outside 1..72 or not an integer (TypeError on the device)', () => {
            const result = validateCode([
                'fb.main.display.setFont("temp", "roboto", 0);',
                'fb.main.list.setFont("scenes", "mdi", 73);',
                'fb.key(1).display.setFont("montserrat", 2.5);',
                'fb.key(1).display.setTopFont("montserrat", -4);',
                'var s = 99; fb.key(1).display.setFont("montserrat", s);',
                'fb.key(1).display.setFont("montserrat", 72); fb.key(1).display.setTopFont(2, 1);',
                'fb.key(1).display.setFont("", 0);',
            ].join('\n'), names);
            expect(result.errors.map(e => [e.message.replace(/ must.*/, ''), e.line])).toEqual([
                ['font size 0', 1], ['font size 73', 2], ['font size 2.5', 3], ['font size -4', 4],
            ]);
        });

        it('knows the system fonts without a "style.fonts" table', () => {
            const plain = { keys: [], displayItems: ['temp'], lists: [] };
            expect(validateCode('fb.key(1).display.setFont("montserrat_semibold", 18);', plain).errors).toEqual([]);
            expect(validateCode('fb.key(1).display.setFont("montserrat", 18);', plain).errors).toEqual([]);
            expect(validateCode('fb.key(1).display.setFont("semibold", 18);', plain).errors).toHaveLength(1);
            expect(validateCode('fb.key(1).display.setFont("icons", 18);', plain).errors).toHaveLength(1);
            expect(validateCode('fb.key(1).display.setFont("roboto", 18);', plain).errors).toHaveLength(1);
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
