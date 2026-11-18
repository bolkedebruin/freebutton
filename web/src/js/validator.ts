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
 * Validates against actual MQuickJS and FreeButton stdlib from generated sources.
 * With a config.json loaded, a string literal passed to fb.key() or as the
 * first argument of fb.main.display.setValue() etc. must be a name in it, a
 * font given to setFont / setTopFont must be a font of it (built-ins
 * included), and a \f{font} escape in a string passed to
 * fb.main.display.setValue / setLabel / setUnit should name one (warning).
 * The named-style calls (setStyle / setTopStyle / setLabelStyle /
 * setFocusStyle, fb.style.set) need named styles in config.json
 * ("style.styles"): the device has them only then; a style given to them
 * must be one of those.
 */

import * as acorn from 'acorn';
import type { Node } from 'acorn';
import stdlib from './stdlib.json';
import { DISPLAY_NAME_METHODS, IMAGE_NAME_METHODS, LIST_NAME_METHODS, STYLE_METHODS, type ConfigNames } from './config-model';
import { getConfigNames } from './device-config';
import { SYSTEM_FONTS, FONTSIZE_MAX } from './font-config';
import { namedSetStatus, styleValueRule } from './style-config';
import { parseTextSpans, TEXT_FONT_UNKNOWN } from './text-spans';

export interface ValidationError {
    message: string;
    line?: number;
    column?: number;
    /** 'warning': reported in `warnings`, does not make the code invalid. */
    severity?: 'warning';
}

export interface ValidationResult {
    valid: boolean;
    errors: ValidationError[];
    /** Problems that do not stop the script (e.g. an unknown \f{font} in a string). */
    warnings: ValidationError[];
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

// Add fb hierarchy global
if ((stdlib as any).fb?.root?.name) {
    KNOWN_GLOBALS.add((stdlib as any).fb.root.name);
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

// Supplemental parameter signatures for flat C-bound APIs.
// The generated stdlib.json currently emits empty `params` for these methods
// (the C introspection does not capture argument types for sensor/mqtt), so we
// declare the known signatures here to enable literal argument type-checking.
// Only applied where stdlib.json has no signature of its own.
const SUPPLEMENTAL_API_SIGNATURES: Record<string, Record<string, string[]>> = {
    sensor: {
        getValue: ['number'],
        getType: ['number'],
        getInfo: ['number'],
        onChange: ['number', 'function'],
    },
    mqtt: {
        getBrokerName: ['number'],
        isConnected: ['number'],
        publish: ['number', 'string', 'string'],
        subscribe: ['number', 'string', 'function'],
        unsubscribe: ['number', 'string'],
    },
};

for (const [apiName, methods] of Object.entries(SUPPLEMENTAL_API_SIGNATURES)) {
    API_SIGNATURES[apiName] = API_SIGNATURES[apiName] || {};
    for (const [methodName, params] of Object.entries(methods)) {
        // Do not override a non-empty signature already sourced from stdlib.json
        const existing = API_SIGNATURES[apiName][methodName];
        if (!existing || existing.length === 0) {
            API_SIGNATURES[apiName][methodName] = params;
        }
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
 * Validate JavaScript code against MQuickJS stricter mode requirements.
 * `names`: key / display item names to check fb.key("x") and
 * fb.main.display.setValue("x", ...) against; defaults to the loaded
 * config.json, null skips the check.
 */
export function validateCode(code: string, names: ConfigNames | null = getConfigNames()): ValidationResult {
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
        walkAST(ast, errors, undefined, undefined, names);

    } catch (err: any) {
        // Syntax errors from acorn
        const loc = err.loc;
        errors.push({
            message: err.message.replace(/\s*\(\d+:\d+\)$/, ''),
            line: loc?.line || err.pos,
            column: loc?.column !== undefined ? loc.column + 1 : undefined
        });
    }

    const warnings = errors.filter(e => e.severity === 'warning');
    const hard = errors.filter(e => e.severity !== 'warning');
    return {
        valid: hard.length === 0,
        errors: hard,
        warnings
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
 * Pre-declare the names that JavaScript hoists to the top of a function or
 * program scope, so references before the declaration are not reported:
 * - function declarations that are direct statements of the body
 * - `var` declarations anywhere in the body (blocks, loops, catch bodies),
 *   but not inside nested functions (those get their own scope)
 * `let`/`const` are not hoisted here: they are declared when the walk reaches
 * them, so use before declaration (TDZ) is still reported.
 */
function hoistDeclarations(body: any[], scope: Scope): void {
    for (const stmt of body) {
        if (stmt?.type === 'FunctionDeclaration' && stmt.id?.type === 'Identifier') {
            scope.variables.add(stmt.id.name);
        }
    }
    const collectVars = (node: any): void => {
        if (!node || typeof node !== 'object') return;
        if (Array.isArray(node)) {
            node.forEach(collectVars);
            return;
        }
        if (node.type === 'FunctionDeclaration' || node.type === 'FunctionExpression' ||
            node.type === 'ArrowFunctionExpression') {
            return;
        }
        if (node.type === 'VariableDeclaration' && node.kind === 'var') {
            for (const decl of node.declarations || []) {
                if (decl.id?.type === 'Identifier') {
                    scope.variables.add(decl.id.name);
                }
            }
        }
        for (const key in node) {
            if (key === 'loc' || key === 'parent') continue;
            const child = node[key];
            if (child && typeof child === 'object') collectVars(child);
        }
    };
    collectVars(body);
}

/**
 * Walk AST and check for MQuickJS violations
 */
function walkAST(node: any, errors: ValidationError[], scope?: Scope, parent?: any, names: ConfigNames | null = null): void {
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
    if (node.type === 'Program') {
        hoistDeclarations(node.body || [], currentScope);
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
        if (node.body?.type === 'BlockStatement') {
            hoistDeclarations(node.body.body || [], funcScope);
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

    // Check for member expressions (e.g., sensor.getValue, mqtt.publish)
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

        if (names) checkConfigName(node, names, errors);

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
            child.forEach(c => walkAST(c, errors, currentScope, node, names));
        } else if (child && typeof child === 'object') {
            walkAST(child, errors, currentScope, node, names);
        }
    }
}

function isMember(node: any, objectName: string | null, property: string): boolean {
    return node?.type === 'MemberExpression' && !node.computed
        && node.property?.type === 'Identifier' && node.property.name === property
        && (objectName === null || (node.object?.type === 'Identifier' && node.object.name === objectName));
}

/** callee is fb.main.<object>.<one of methods> */
function isMainMethod(callee: any, object: string, methods: string[]): boolean {
    return callee?.type === 'MemberExpression' && !callee.computed
        && callee.property?.type === 'Identifier' && methods.includes(callee.property.name)
        && isMember(callee.object, null, object) && isMember(callee.object.object, 'fb', 'main');
}

/** A string literal argument that is not in `known` -> "unknown <what>" error. */
function checkNameArg(arg: any, what: string, known: string[], errors: ValidationError[]): void {
    if (arg?.type !== 'Literal' || typeof arg.value !== 'string' || known.includes(arg.value)) return;
    errors.push({
        message: `unknown ${what} "${arg.value}" in config.json`,
        line: arg.loc?.start?.line,
        column: arg.loc ? arg.loc.start.column + 1 : undefined
    });
}

/**
 * Icon names: fb.main.display.setIcon(item, "icon"), fb.main.image.setIcon(image,
 * "icon") and <side>.display.setIcon("icon") (a key's display). "" hides the
 * icon. Only checked when config.json has an "icons" table.
 */
function checkIconName(node: any, names: ConfigNames, errors: ValidationError[]): void {
    const callee = node.callee;
    if (!names.icons || callee?.type !== 'MemberExpression' || callee.computed
        || callee.property?.type !== 'Identifier' || callee.property.name !== 'setIcon') return;
    let arg: any;
    if (isMainMethod(callee, 'display', ['setIcon']) || isMainMethod(callee, 'image', ['setIcon'])) {
        arg = node.arguments?.[1];
    } else if (isMember(callee.object, null, 'display') && node.arguments?.length === 1) {
        arg = node.arguments[0];
    } else {
        return;
    }
    if (arg?.type === 'Literal' && arg.value === '') return;
    checkNameArg(arg, 'icon', names.icons, errors);
}

const SYSTEM_FONT_NAMES: string[] = SYSTEM_FONTS.map(f => f.name);
/** fb.main.display methods whose second argument is display text (\f{} escapes). */
const DISPLAY_TEXT_METHODS = ['setValue', 'setLabel', 'setUnit'];

/** A numeric literal (also -n); null for anything else. */
function literalNumber(node: any): number | null {
    if (node?.type === 'Literal' && typeof node.value === 'number') return node.value;
    if (node?.type === 'UnaryExpression' && node.operator === '-' && node.argument?.type === 'Literal'
        && typeof node.argument.value === 'number') return -node.argument.value;
    return null;
}

function locOf(node: any): { line?: number; column?: number } {
    return { line: node?.loc?.start?.line, column: node?.loc ? node.loc.start.column + 1 : undefined };
}

/**
 * Fonts: fb.main.display.setFont(item, font, size), fb.main.list.setFont(list,
 * font, size) and <side>.display.setFont / setTopFont(font, size) (a key's
 * display): a literal font name or id must be a font of config.json ("" =
 * restore the configured font and size)
 * (the system fonts montserrat / montserrat_semibold always are, and every
 * "style.fonts" entry); the device throws ReferenceError
 * otherwise. \f{font} in a string literal given to fb.main.display.setValue /
 * setLabel / setUnit: a warning when it names no font (the device keeps the
 * item's font).
 */
function checkFontArgs(node: any, names: ConfigNames, errors: ValidationError[]): void {
    const callee = node.callee;
    if (callee?.type !== 'MemberExpression' || callee.computed || callee.property?.type !== 'Identifier') return;
    const fonts = names.fonts ?? SYSTEM_FONT_NAMES;
    const ids = names.fontIds ?? [];
    const method = callee.property.name;

    if (isMainMethod(callee, 'display', DISPLAY_TEXT_METHODS)) {
        const arg = node.arguments?.[1];
        if (arg?.type !== 'Literal' || typeof arg.value !== 'string' || !arg.value.includes('\\f')) return;
        const unknown = new Set<string>();
        parseTextSpans(arg.value, (name, isId, id) => {
            if (isId ? ids.includes(id) : fonts.includes(name)) return 0;
            unknown.add(name);
            return TEXT_FONT_UNKNOWN;
        });
        for (const name of unknown) {
            errors.push({
                message: `unknown font "${name}" in \\f{${name}} (not a system font or a config.json "style.fonts" entry; the item's font is used)`,
                ...locOf(arg),
                severity: 'warning'
            });
        }
        return;
    }

    let arg: any;
    let sizeArg: any;
    if (method === 'setFont' && (isMainMethod(callee, 'display', ['setFont']) || isMainMethod(callee, 'list', ['setFont']))) {
        arg = node.arguments?.[1];
        sizeArg = node.arguments?.[2];
    } else if ((method === 'setFont' || method === 'setTopFont') && isMember(callee.object, null, 'display')
        && !isMember(callee.object.object, 'fb', 'main')) {
        arg = node.arguments?.[0];
        sizeArg = node.arguments?.[1];
    } else {
        return;
    }
    // "" = back to the configured font and size (the size is not used then).
    if (arg?.type === 'Literal' && arg.value === '') return;
    // A literal size must be an integer 1..72 (1-5: the legacy steps); the device throws TypeError.
    const size = literalNumber(sizeArg);
    if (size !== null && !(Number.isInteger(size) && size >= 1 && size <= FONTSIZE_MAX)) {
        errors.push({ message: `font size ${size} must be an integer 1..${FONTSIZE_MAX} (1-5: the sizes 14/24/32/48/72 px, else px)`, ...locOf(sizeArg) });
    }
    if (arg?.type !== 'Literal') return;
    if (typeof arg.value === 'string' && !fonts.includes(arg.value)) {
        errors.push({ message: `unknown font "${arg.value}" in config.json`, ...locOf(arg) });
    } else if (typeof arg.value === 'number' && !ids.includes(arg.value)) {
        errors.push({ message: `unknown font id ${arg.value} in config.json`, ...locOf(arg) });
    }
}

/**
 * Named styles (config.json "style.styles"): fb.main.display.setStyle /
 * setLabelStyle(item, style), fb.main.list.setStyle / setFocusStyle(list,
 * style), <side>.display.setStyle / setTopStyle(style) (a key's display)
 * and fb.style.set(style, property, value).
 *  - Without named styles in config.json the device has none of them (the
 *    engine's "not a function" error): every such call is an error.
 *  - A literal style must be a named style ("" = none; the device throws
 *    ReferenceError), a string (TypeError), and on a key or a display item
 *    not one that sets bg_color / bg_opa (TypeError).
 *  - fb.style.set: a literal property must be one the style sets in
 *    config.json, not text_font / text_size, and a literal value one that
 *    config.json takes for it: a string or a number, never true / false /
 *    null (TypeError).
 * A key's display is recognised as checkFontArgs recognises it for setFont /
 * setTopFont: any `<expr>.display.setStyle(...)` that is not fb.main's. The
 * receiver is not traced back to fb.key() / fb.bars, so a user object with a
 * `display.setStyle` of its own is checked as a key too.
 */
function checkStyleArgs(node: any, names: ConfigNames, errors: ValidationError[]): void {
    const callee = node.callee;
    if (callee?.type !== 'MemberExpression' || callee.computed || callee.property?.type !== 'Identifier') return;
    const method: string = callee.property.name;
    const isSet = method === 'set' && isMember(callee.object, 'fb', 'style');
    let call: string;
    let arg: any;
    /** A style with bg_* is refused here (a key, a display item). */
    let noBackground = false;
    if (isSet) {
        call = 'fb.style.set';
        arg = node.arguments?.[0];
    } else if (isMainMethod(callee, 'display', [...STYLE_METHODS.display])) {
        call = `fb.main.display.${method}`;
        arg = node.arguments?.[1];
        noBackground = true;
    } else if (isMainMethod(callee, 'list', [...STYLE_METHODS.list])) {
        call = `fb.main.list.${method}`;
        arg = node.arguments?.[1];
    } else if ((STYLE_METHODS.key as readonly string[]).includes(method) && isMember(callee.object, null, 'display')
        && !isMember(callee.object.object, 'fb', 'main')) {
        call = `display.${method}`;
        arg = node.arguments?.[0];
        noBackground = true;
    } else {
        return;
    }
    const styles = names.styles;
    if (!styles) {
        errors.push({
            message: `${call}() needs named styles: config.json has no "style.styles", so the device does not have this call`,
            ...locOf(callee.property)
        });
        return;
    }
    if (arg?.type !== 'Literal') return;
    if (typeof arg.value !== 'string') {
        errors.push({ message: `${call}(): the style must be a string (the name of a "style.styles" entry)`, ...locOf(arg) });
        return;
    }
    // "" = no named style on the part; fb.style.set has no such style.
    if (arg.value === '' && !isSet) return;
    const has = Object.prototype.hasOwnProperty.call(styles, arg.value) ? styles[arg.value]! : null;
    if (!has) {
        errors.push({ message: `unknown style "${arg.value}" in config.json`, ...locOf(arg) });
        return;
    }
    if (noBackground && has.some(p => p.startsWith('bg_'))) {
        errors.push({
            message: `style "${arg.value}" sets ${has.filter(p => p.startsWith('bg_')).join(' and ')}: it is for lists only (a key or display item has no background)`,
            ...locOf(arg)
        });
    }
    if (!isSet) return;
    // A literal true / false / null as the value: the device takes a string or a
    // number only and throws TypeError, whatever the property is.
    const literalValue = node.arguments?.[2];
    if (literalValue?.type === 'Literal' && !literalValue.regex
        && (typeof literalValue.value === 'boolean' || literalValue.value === null)) {
        errors.push({ message: 'fb.style.set(): the value must be a string or a number', ...locOf(literalValue) });
        return;
    }
    const prop = node.arguments?.[1];
    if (prop?.type !== 'Literal') return;
    if (typeof prop.value !== 'string') {
        errors.push({ message: 'fb.style.set(): the property must be a string ("text_color", ...)', ...locOf(prop) });
        return;
    }
    const valueNode = node.arguments?.[2];
    const number = literalNumber(valueNode);
    const value: string | number | null = number !== null ? number
        : valueNode?.type === 'Literal' && typeof valueNode.value === 'string' ? valueNode.value : null;
    // Without a literal value: the property alone (any valid value stands in).
    const settable = has.filter(p => p !== 'text_font' && p !== 'text_size');
    switch (namedSetStatus(has, prop.value, value ?? (prop.value.endsWith('_color') ? '#000000' : 0))) {
        case 'unknown':
            errors.push({ message: `"${prop.value}" is not a style property`, ...locOf(prop) });
            break;
        case 'fixed':
            errors.push({ message: `fb.style.set() cannot change ${prop.value}: use another named style for another font or size`, ...locOf(prop) });
            break;
        case 'not-in-style':
            errors.push({
                message: `style "${arg.value}" does not set ${prop.value} in config.json: fb.style.set() changes only what the style sets`
                    + (settable.length ? ` (${settable.join(', ')})` : ''),
                ...locOf(prop)
            });
            break;
        case 'ok':
            break;
        default:
            if (value !== null) errors.push({ message: `${prop.value} ${styleValueRule(prop.value)}`, ...locOf(valueNode) });
    }
}

/**
 * fb.key("x") / fb.main.display.<DISPLAY_NAME_METHODS>("x", ...) /
 * fb.main.list.<LIST_NAME_METHODS>("x", ...) / fb.main.image.<IMAGE_NAME_METHODS>("x", ...):
 * a string literal name must be in config.json, and so must an icon name
 * given to setIcon (checkIconName). Other arguments (and numeric indexes)
 * are not checked.
 */
function checkConfigName(node: any, names: ConfigNames, errors: ValidationError[]): void {
    const callee = node.callee;
    let what: string;
    let known: string[];
    checkIconName(node, names, errors);
    checkFontArgs(node, names, errors);
    checkStyleArgs(node, names, errors);
    if (isMember(callee, 'fb', 'key')) {
        what = 'key';
        known = names.keys;
    } else if (isMainMethod(callee, 'display', DISPLAY_NAME_METHODS)) {
        what = 'display item';
        known = names.displayItems;
    } else if (isMainMethod(callee, 'list', LIST_NAME_METHODS)) {
        what = 'list';
        known = names.lists ?? [];
    } else if (isMainMethod(callee, 'image', IMAGE_NAME_METHODS)) {
        what = 'image';
        known = names.images ?? [];
    } else {
        return;
    }
    checkNameArg(node.arguments?.[0], what, known, errors);
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
