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
 *
 * Validates against actual MQuickJS and FreeButton stdlib from generated sources
 */

import * as acorn from 'acorn';
import type { Node } from 'acorn';
import stdlib from './stdlib.json';

export interface ValidationError {
    message: string;
    line?: number;
    column?: number;
}

export interface ValidationResult {
    valid: boolean;
    errors: ValidationError[];
}

// Load known globals from stdlib.json
const KNOWN_GLOBALS = new Set<string>();

// Add global functions
for (const globalFunc of stdlib.globals) {
    KNOWN_GLOBALS.add(globalFunc.name);
}

// Add FreeButton APIs
for (const apiName of Object.keys(stdlib.apis)) {
    KNOWN_GLOBALS.add(apiName);
}

// Add stdlib classes
for (const className of Object.keys(stdlib.classes)) {
    KNOWN_GLOBALS.add(className);
}

// Add constants
for (const constant of stdlib.constants) {
    KNOWN_GLOBALS.add(constant.name);
}

// Add test utility functions (from test_language.js and test_builtin.js)
// These are JavaScript helper functions for test files, not C bindings
KNOWN_GLOBALS.add('throw_error');
KNOWN_GLOBALS.add('assert');
KNOWN_GLOBALS.add('assert_throws');

// Build API methods map
const API_METHODS: Record<string, Set<string>> = {};
for (const [apiName, api] of Object.entries(stdlib.apis)) {
    API_METHODS[apiName] = new Set(api.methods.map(m => m.name));
}

// API method signatures for type checking - generated from stdlib.json
// Format: { api: { method: [param types] } }
const API_SIGNATURES: Record<string, Record<string, string[]>> = {};

// Build API signatures from stdlib.json
for (const [apiName, api] of Object.entries(stdlib.apis)) {
    API_SIGNATURES[apiName] = {};
    for (const method of api.methods) {
        // Map types to lowercase for compatibility (Function -> function)
        API_SIGNATURES[apiName][method.name] = method.params.map(p => p.type.toLowerCase());
    }
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
 * Track variable declarations in scope
 */
interface Scope {
    variables: Set<string>;
    parent?: Scope;
}

function createScope(parent?: Scope): Scope {
    return { variables: new Set(), parent };
}

function isDeclared(scope: Scope | undefined, name: string): boolean {
    if (!scope) return false;
    if (scope.variables.has(name)) return true;
    return isDeclared(scope.parent, name);
}

/**
 * Walk AST and check for MQuickJS violations
 */
function walkAST(node: any, errors: ValidationError[], scope?: Scope, parent?: any): void {
    if (!node || typeof node !== 'object') return;

    // Attach parent reference for context checking
    node.parent = parent;

    const loc = node.loc;
    const line = loc?.start?.line;
    const column = loc?.start?.column ? loc.start.column + 1 : undefined;

    // Create new scope for functions and blocks
    let currentScope = scope;
    if (!currentScope) {
        currentScope = createScope();
    }

    // Track variable declarations
    if (node.type === 'VariableDeclaration') {
        for (const decl of node.declarations || []) {
            if (decl.id?.type === 'Identifier') {
                currentScope.variables.add(decl.id.name);
            }
        }
    }

    // Track function declarations
    if (node.type === 'FunctionDeclaration' && node.id?.type === 'Identifier') {
        currentScope.variables.add(node.id.name);
    }

    // Track function parameters
    if (node.type === 'FunctionDeclaration' || node.type === 'FunctionExpression') {
        const funcScope = createScope(currentScope);
        for (const param of node.params || []) {
            if (param.type === 'Identifier') {
                funcScope.variables.add(param.name);
            }
        }
        currentScope = funcScope;
    }

    // Track catch clause parameters
    if (node.type === 'CatchClause') {
        const catchScope = createScope(currentScope);
        if (node.param && node.param.type === 'Identifier') {
            catchScope.variables.add(node.param.name);
        }
        currentScope = catchScope;
    }

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

    // Check for undefined variable references
    if (node.type === 'Identifier') {
        const name = node.name;

        // Get parent to determine context
        const parent = (node as any).parent;

        // Skip if this is a declaration (not a reference)
        if (parent) {
            // Skip property names in member expressions (e.g., the "x" in obj.x)
            if (parent.type === 'MemberExpression' && parent.property === node && !parent.computed) {
                return; // This is a property access, not a variable reference
            }

            // Skip labels in object properties (e.g., {x: 1})
            if (parent.type === 'Property' && parent.key === node && !parent.computed) {
                return;
            }

            // Skip function names in declarations
            if ((parent.type === 'FunctionDeclaration' || parent.type === 'FunctionExpression') && parent.id === node) {
                return;
            }

            // Skip variable declarator names
            if (parent.type === 'VariableDeclarator' && parent.id === node) {
                return;
            }
        }

        // Check if variable is declared or is a known global
        if (!isDeclared(currentScope, name) && !KNOWN_GLOBALS.has(name)) {
            const suggestions = findSimilarGlobals(name);
            const suggestionText = suggestions.length > 0
                ? ` Did you mean one of: ${suggestions.join(', ')}?`
                : '';

            errors.push({
                message: `'${name}' is not defined.${suggestionText}`,
                line,
                column
            });
        }
    }

    // Check for member expressions (e.g., led.setColor, mqtt.publish)
    if (node.type === 'MemberExpression' && !node.computed) {
        const object = node.object;
        const property = node.property;

        if (object?.type === 'Identifier' && property?.type === 'Identifier') {
            const apiName = object.name;
            const methodName = property.name;

            // Check if API exists in our known APIs
            if (API_METHODS[apiName]) {
                // Check if method exists on this API
                if (!API_METHODS[apiName].has(methodName)) {
                    errors.push({
                        message: `'${apiName}.${methodName}' is not defined. Available methods: ${Array.from(API_METHODS[apiName]).join(', ')}`,
                        line,
                        column
                    });
                }
            }
        }
    }

    // Check for undefined function calls and validate API method arguments
    if (node.type === 'CallExpression') {
        const callee = node.callee;

        // Check for API method calls to validate argument types
        if (callee?.type === 'MemberExpression' && !callee.computed) {
            const object = callee.object;
            const property = callee.property;

            if (object?.type === 'Identifier' && property?.type === 'Identifier') {
                const apiName = object.name;
                const methodName = property.name;

                // Validate argument types for known API methods
                if (API_SIGNATURES[apiName]?.[methodName]) {
                    const expectedTypes = API_SIGNATURES[apiName][methodName];
                    const args = node.arguments || [];

                    // Check each argument
                    for (let i = 0; i < args.length && i < expectedTypes.length; i++) {
                        const arg = args[i];
                        const expectedType = expectedTypes[i];
                        const actualType = inferArgumentType(arg);

                        // Skip type check if expected type is 'any' or actual type is 'unknown'
                        if (actualType && actualType !== expectedType && actualType !== 'unknown' && expectedType !== 'any') {
                            errors.push({
                                message: `${apiName}.${methodName}() argument ${i + 1} expects ${expectedType}, but got ${actualType}`,
                                line: arg.loc?.start?.line,
                                column: arg.loc?.start?.column ? arg.loc.start.column + 1 : undefined
                            });
                        }
                    }
                }
            }
        }

        // Check regular function calls
        if (callee?.type === 'Identifier') {
            const name = callee.name;

            // Check if function is declared or is a known global
            if (!isDeclared(currentScope, name) && !KNOWN_GLOBALS.has(name)) {
                errors.push({
                    message: `'${name}' is not defined. Did you mean one of: ${findSimilarGlobals(name).join(', ')}?`,
                    line,
                    column
                });
            }
        }
    }

    // MQuickJS allows some ES6+ features:
    // - Exponentiation operator ** (we'll allow it)
    // - for...of with arrays (checked above)
    // - Typed arrays (can't statically check)
    // - Limited string methods (can't statically check)
    // - globalThis (can't easily check without more complex analysis)

    // Recursively walk child nodes
    for (const key in node) {
        if (key === 'loc' || key === 'range' || key === 'start' || key === 'end' || key === 'parent') {
            continue;
        }

        const child = node[key];
        if (Array.isArray(child)) {
            child.forEach(c => walkAST(c, errors, currentScope, node));
        } else if (child && typeof child === 'object') {
            walkAST(child, errors, currentScope, node);
        }
    }
}

/**
 * Infer the type of an argument from AST node
 */
function inferArgumentType(node: any): string {
    if (!node) return 'unknown';

    // Literal values
    if (node.type === 'Literal') {
        if (typeof node.value === 'number') return 'number';
        if (typeof node.value === 'string') return 'string';
        if (typeof node.value === 'boolean') return 'boolean';
        if (node.value === null) return 'null';
    }

    // Function expressions
    if (node.type === 'FunctionExpression' || node.type === 'ArrowFunctionExpression') {
        return 'function';
    }

    // Identifiers and expressions - we can't determine type statically
    // Return 'unknown' to skip validation
    return 'unknown';
}

/**
 * Find similar global identifiers using simple string matching
 */
function findSimilarGlobals(input: string): string[] {
    const similar: string[] = [];
    const inputLower = input.toLowerCase();

    for (const global of KNOWN_GLOBALS) {
        const globalLower = global.toLowerCase();

        // Check for substring match or Levenshtein distance
        if (globalLower.includes(inputLower) || inputLower.includes(globalLower)) {
            similar.push(global);
        } else if (levenshteinDistance(inputLower, globalLower) <= 2) {
            similar.push(global);
        }
    }

    return similar.slice(0, 3); // Return top 3 matches
}

/**
 * Calculate Levenshtein distance
 */
function levenshteinDistance(a: string, b: string): number {
    if (a.length === 0) return b.length;
    if (b.length === 0) return a.length;

    const matrix: number[][] = [];

    for (let i = 0; i <= b.length; i++) {
        matrix[i] = [i];
    }

    for (let j = 0; j <= a.length; j++) {
        matrix[0]![j] = j;
    }

    for (let i = 1; i <= b.length; i++) {
        for (let j = 1; j <= a.length; j++) {
            if (b.charAt(i - 1) === a.charAt(j - 1)) {
                matrix[i]![j] = matrix[i - 1]![j - 1]!;
            } else {
                matrix[i]![j] = Math.min(
                    matrix[i - 1]![j - 1]! + 1, // substitution
                    matrix[i]![j - 1]! + 1,     // insertion
                    matrix[i - 1]![j]! + 1      // deletion
                );
            }
        }
    }

    return matrix[b.length]![a.length]!;
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
