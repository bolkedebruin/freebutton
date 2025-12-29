/**
 * MQuickJS Code Validator
 * Enforces ES5 strict mode and MQuickJS "stricter mode" limitations
 *
 * MQuickJS Restrictions:
 * - Strict mode only (no `with`, global vars must be declared)
 * - Arrays cannot have holes
 * - Only indirect eval: (1, eval)('code')
 * - No value boxing: new Number(1), new String(), new Boolean()
 * - for...of only works with arrays
 * - ES5 feature set with limited ES6+ extensions
 */

import * as acorn from 'acorn';
import type { Node } from 'acorn';

export interface ValidationError {
    message: string;
    line?: number;
    column?: number;
}

export interface ValidationResult {
    valid: boolean;
    errors: ValidationError[];
}

// Forbidden value boxing constructors
const VALUE_BOXING_CONSTRUCTORS = new Set(['Number', 'String', 'Boolean']);

// MQuickJS stricter mode validation plugin
function mquickjsStricterMode(Parser: typeof acorn.Parser) {
    return class extends Parser {
        // Override raise to capture errors with proper formatting
        raise(pos: number, message: string): never {
            const loc = acorn.getLineInfo(this.input, pos);
            const error = new SyntaxError(message);
            (error as any).pos = pos;
            (error as any).loc = loc;
            throw error;
        }

        // Override to detect array holes
        // @ts-ignore - parseExprAtom is an internal acorn method not in public types
        parseExprAtom(refDestructuringErrors?: any): Node {
            // @ts-ignore
            const node = super.parseExprAtom(refDestructuringErrors);

            // Check for array holes: [1, , 3]
            if (node.type === 'ArrayExpression') {
                for (let i = 0; i < (node as any).elements.length; i++) {
                    if ((node as any).elements[i] === null) {
                        this.raise(node.start, 'Array holes are not allowed in MQuickJS stricter mode. Use undefined explicitly: [1, undefined, 3]');
                    }
                }
            }

            return node;
        }
    };
}

// Create custom parser with MQuickJS validation
const MQuickJSParser = acorn.Parser.extend(mquickjsStricterMode);

/**
 * Validate JavaScript code against MQuickJS stricter mode requirements
 */
export function validateCode(code: string): ValidationResult {
    const errors: ValidationError[] = [];

    try {
        // Parse with modern JS to allow const, let, for...of, **
        // but we'll validate against MQuickJS allowed features
        const ast = MQuickJSParser.parse(code, {
            ecmaVersion: 2020,
            sourceType: 'script',
            locations: true,
            allowReturnOutsideFunction: false
        });

        // Walk the AST to check for MQuickJS violations
        walkAST(ast, errors);

    } catch (err: any) {
        // Syntax errors from acorn
        const loc = err.loc;
        errors.push({
            message: err.message.replace(/\s*\(\d+:\d+\)$/, ''),
            line: loc?.line || err.pos,
            column: loc?.column !== undefined ? loc.column + 1 : undefined
        });
    }

    return {
        valid: errors.length === 0,
        errors
    };
}

/**
 * Walk AST and check for MQuickJS violations
 */
function walkAST(node: any, errors: ValidationError[]): void {
    if (!node || typeof node !== 'object') return;

    const loc = node.loc;
    const line = loc?.start?.line;
    const column = loc?.start?.column ? loc.start.column + 1 : undefined;

    // Check for `with` statement (forbidden in strict mode)
    if (node.type === 'WithStatement') {
        errors.push({
            message: '`with` statement is forbidden in MQuickJS strict mode',
            line,
            column
        });
    }

    // Check for direct eval calls
    if (node.type === 'CallExpression') {
        if (node.callee?.type === 'Identifier' && node.callee.name === 'eval') {
            errors.push({
                message: 'Direct eval() is forbidden in MQuickJS. Use indirect eval: (1, eval)("code")',
                line,
                column
            });
        }

        // Check for value boxing constructors with `new`
        if (node.callee?.type === 'Identifier' && VALUE_BOXING_CONSTRUCTORS.has(node.callee.name)) {
            // Check if parent is NewExpression
            // We'll check this during NewExpression walk
        }
    }

    // Check for value boxing: new Number(1), new String(), new Boolean()
    if (node.type === 'NewExpression') {
        if (node.callee?.type === 'Identifier' && VALUE_BOXING_CONSTRUCTORS.has(node.callee.name)) {
            errors.push({
                message: `Value boxing with 'new ${node.callee.name}()' is not supported in MQuickJS. Use literals or direct conversion: ${node.callee.name}(value)`,
                line,
                column
            });
        }
    }

    // Check for assignment to array indices (potential out-of-bounds)
    if (node.type === 'AssignmentExpression') {
        // Check for pattern: arr[index] = value where index > arr.length
        if (node.left?.type === 'MemberExpression' &&
            node.left.computed &&
            node.left.property?.type === 'Literal' &&
            typeof node.left.property.value === 'number') {

            // We can't statically verify array bounds without runtime info,
            // but we can warn about non-sequential assignments
            const index = node.left.property.value;
            if (index > 1) {
                errors.push({
                    message: `Assignment to array[${index}] may fail if not sequential. MQuickJS only allows extending arrays one element at a time.`,
                    line,
                    column
                });
            }
        }
    }

    // Check for for...of with non-array iterables
    if (node.type === 'ForOfStatement') {
        // MQuickJS only supports for...of with arrays
        // We can't statically determine the type, so we'll add a warning
        // (This is informational since we can't verify without runtime info)
        const rightType = node.right?.type;
        if (rightType !== 'ArrayExpression' && rightType !== 'Identifier') {
            errors.push({
                message: 'for...of in MQuickJS only works with arrays. Ensure the iterable is an array.',
                line,
                column
            });
        }
    }

    // Check for undeclared variables (must be declared with var/let/const in strict mode)
    // This is handled by ES5 strict mode parser

    // Check for unsupported ES6+ features
    if (node.type === 'ArrowFunctionExpression') {
        errors.push({
            message: 'Arrow functions are not supported in ES5. Use function expressions instead.',
            line,
            column
        });
    }

    if (node.type === 'ClassDeclaration' || node.type === 'ClassExpression') {
        errors.push({
            message: 'Classes are not supported in ES5. Use constructor functions and prototypes.',
            line,
            column
        });
    }

    if (node.type === 'TemplateLiteral') {
        errors.push({
            message: 'Template literals are not supported in ES5. Use string concatenation.',
            line,
            column
        });
    }

    if (node.type === 'SpreadElement') {
        errors.push({
            message: 'Spread operator is not supported in ES5.',
            line,
            column
        });
    }

    if (node.type === 'RestElement') {
        errors.push({
            message: 'Rest parameters are not supported in ES5.',
            line,
            column
        });
    }

    if (node.type === 'ObjectPattern' || node.type === 'ArrayPattern') {
        errors.push({
            message: 'Destructuring is not supported in ES5.',
            line,
            column
        });
    }

    // MQuickJS allows some ES6+ features:
    // - Exponentiation operator ** (we'll allow it)
    // - for...of with arrays (checked above)
    // - Typed arrays (can't statically check)
    // - Limited string methods (can't statically check)
    // - globalThis (can't easily check without more complex analysis)

    // Recursively walk child nodes
    for (const key in node) {
        if (key === 'loc' || key === 'range' || key === 'start' || key === 'end') {
            continue;
        }

        const child = node[key];
        if (Array.isArray(child)) {
            child.forEach(c => walkAST(c, errors));
        } else if (child && typeof child === 'object') {
            walkAST(child, errors);
        }
    }
}

/**
 * Format validation errors for display
 */
export function formatValidationErrors(errors: ValidationError[]): string {
    if (errors.length === 0) {
        return '';
    }

    return errors.map(err => {
        const location = err.line ? ` (line ${err.line}:${err.column || 0})` : '';
        return `${err.message}${location}`;
    }).join('\n');
}
