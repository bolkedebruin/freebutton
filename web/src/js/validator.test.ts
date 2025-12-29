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

        it('should accept LED blink example', () => {
            const result = validateCode(`
                function blink() {
                    led.setColor(0, 255, 0, 0);
                    setTimeout(function() {
                        led.off(0);
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
            const result = validateCode('var squared = x ** 2;');
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

    describe('FreeButton API Integration', () => {
        it('should accept LED API calls', () => {
            const result = validateCode(`
                led.on(0);
                led.off(1);
                led.setColor(0, 255, 0, 0);
                var count = led.count();
            `);
            expect(result.valid).toBe(true);
        });

        it('should accept button API calls', () => {
            const result = validateCode(`
                button.setLabel(0, "Click Me");
                button.onClick(0, function() {
                    print("Clicked!");
                });
            `);
            expect(result.valid).toBe(true);
        });

        it('should accept sensor API calls', () => {
            const result = validateCode(`
                var temp = sensor.getValue(0);
                var info = sensor.getInfo(0);
            `);
            expect(result.valid).toBe(true);
        });

        it('should accept MQTT API calls', () => {
            const result = validateCode(`
                mqtt.publish(0, "test/topic", "hello");
                mqtt.subscribe(0, "test/topic", function(topic, msg) {
                    print(msg);
                });
            `);
            expect(result.valid).toBe(true);
        });
    });
});
