#!/usr/bin/env node
/**
 * Extract JavaScript Globals from MQuickJS and FreeButton Stdlib
 * Parses C source files to generate a list of all available global identifiers
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const MQUICKJS_STDLIB = path.join(__dirname, '../../lib/mquickjs/mqjs_stdlib.c');
const FREEBUTTON_STDLIB = path.join(__dirname, '../../lib/mquickjs/freebutton_stdlib.c');
const FREEBUTTON_TYPES = path.join(__dirname, '../src/js/freebutton.d.ts');
const OUTPUT_FILE = path.join(__dirname, '../src/js/stdlib-globals.json');

/**
 * Extract global class names from mqjs_stdlib.c
 */
function extractMQuickJSGlobals(content) {
    const globals = new Set();

    // Extract class definitions: JS_CLASS_DEF("ClassName", ...)
    const classDefRegex = /JS_CLASS_(?:MAGIC_)?DEF\s*\(\s*"(\w+)"/g;
    let match;
    while ((match = classDefRegex.exec(content)) !== null) {
        globals.add(match[1]);
    }

    // Extract object definitions: JS_OBJECT_DEF("ObjectName", ...)
    const objectDefRegex = /JS_OBJECT_DEF\s*\(\s*"(\w+)"/g;
    while ((match = objectDefRegex.exec(content)) !== null) {
        globals.add(match[1]);
    }

    // Extract JS_PROP_CLASS_DEF registrations for actual global names
    // Example: JS_PROP_CLASS_DEF("console", &js_console_obj)
    const propClassDefRegex = /JS_PROP_CLASS_DEF\s*\(\s*"(\w+)"\s*,/g;
    while ((match = propClassDefRegex.exec(content)) !== null) {
        globals.add(match[1]);
    }

    return Array.from(globals);
}

/**
 * Extract MQuickJS stdlib object APIs (console, performance, etc.)
 */
function extractMQuickJSAPIs(content) {
    const apis = {};

    // Find JS_PROP_CLASS_DEF registrations: JS_PROP_CLASS_DEF("console", &js_console_obj)
    const propClassDefRegex = /JS_PROP_CLASS_DEF\s*\(\s*"(\w+)"\s*,\s*&(\w+)\s*\)/g;
    const registrations = Array.from(content.matchAll(propClassDefRegex));

    for (const [, objectName, objVarName] of registrations) {
        // Find the object definition: static const JSClassDef js_console_obj = JS_OBJECT_DEF("Console", js_console);
        const objDefRegex = new RegExp(`static const JSClassDef ${objVarName}\\s*=\\s*JS_OBJECT_DEF\\s*\\(\\s*"\\w+"\\s*,\\s*(\\w+)\\s*\\)`, 'g');
        const objDefMatch = objDefRegex.exec(content);

        if (objDefMatch) {
            const methodsArrayName = objDefMatch[1]; // e.g., js_console

            // Find the methods array: static const JSPropDef js_console[] = { ... };
            const methodsRegex = new RegExp(`static const JSPropDef ${methodsArrayName}\\[\\]\\s*=\\s*\\{([^}]+)\\}`, 's');
            const methodsMatch = methodsRegex.exec(content);

            if (methodsMatch) {
                const methodsBody = methodsMatch[1];

                // Extract method names: JS_CFUNC_DEF("log", ...)
                const methodNameRegex = /JS_CFUNC_DEF\s*\(\s*"(\w+)"/g;
                const methods = Array.from(methodsBody.matchAll(methodNameRegex)).map(m => m[1]);

                if (methods.length > 0) {
                    apis[objectName] = methods;
                }
            }
        }
    }

    return apis;
}

/**
 * Extract FreeButton API object names
 */
function extractFreeButtonGlobals(content) {
    const globals = new Set();

    // Look for CONFIG_FREEBUTTON_* defines and corresponding object names
    // From freebutton_stdlib.c comments we know: led, button, sensor, mqtt
    const objectDefRegex = /JS_OBJECT_DEF\s*\(\s*"(\w+)"/g;
    let match;
    while ((match = objectDefRegex.exec(content)) !== null) {
        const objectName = match[1];
        // Convert class name to lowercase instance name
        // LED -> led, Button -> button, etc.
        globals.add(objectName.toLowerCase());
    }

    return Array.from(globals);
}

/**
 * Extract global functions from TypeScript declarations
 */
function extractGlobalFunctions(content) {
    const functions = new Set();

    // Extract: declare function functionName(...)
    const functionRegex = /declare\s+function\s+(\w+)\s*\(/g;
    let match;
    while ((match = functionRegex.exec(content)) !== null) {
        functions.add(match[1]);
    }

    // Extract: declare const varName: ...
    const constRegex = /declare\s+const\s+(\w+)\s*:/g;
    while ((match = constRegex.exec(content)) !== null) {
        functions.add(match[1]);
    }

    // Extract functions inside declare global {} blocks
    // Match: declare global { ... function functionName(...) ... }
    const globalBlockRegex = /declare\s+global\s*\{([^}]*(?:\{[^}]*\}[^}]*)*)\}/gs;
    const globalMatch = globalBlockRegex.exec(content);
    if (globalMatch) {
        const globalBlock = globalMatch[1];

        // Extract: function functionName(...)
        const globalFunctionRegex = /\bfunction\s+(\w+)\s*\(/g;
        while ((match = globalFunctionRegex.exec(globalBlock)) !== null) {
            functions.add(match[1]);
        }
    }

    return Array.from(functions);
}

/**
 * Extract methods from TypeScript interfaces
 */
function extractAPIMethods(content) {
    const methods = {};

    // Extract interface definitions and their methods
    const interfaceRegex = /interface\s+(\w+API)\s*{([^}]+)}/g;
    let match;

    while ((match = interfaceRegex.exec(content)) !== null) {
        const interfaceName = match[1]; // e.g., "LedAPI"
        const interfaceBody = match[2];
        const objectName = interfaceName.replace('API', '').toLowerCase(); // "led"

        // Extract method names
        const methodRegex = /^\s*(?:\/\*\*[^*]*\*\/\s*)?(\w+)\s*\(/gm;
        const methodMatches = Array.from(interfaceBody.matchAll(methodRegex));

        methods[objectName] = methodMatches.map(m => m[1]);
    }

    return methods;
}

/**
 * Get standard ES5 built-ins
 */
function getES5Builtins() {
    return [
        // Constructors/Types
        'Object', 'Array', 'String', 'Number', 'Boolean', 'Function',
        'Date', 'RegExp', 'Error',

        // Error types
        'EvalError', 'RangeError', 'ReferenceError', 'SyntaxError',
        'TypeError', 'URIError', 'InternalError',

        // Global objects
        'Math', 'JSON',

        // Global functions
        'parseInt', 'parseFloat', 'isNaN', 'isFinite',
        'encodeURI', 'decodeURI', 'encodeURIComponent', 'decodeURIComponent',
        'eval',

        // Global values
        'undefined', 'null', 'NaN', 'Infinity',
        'globalThis', // MQuickJS extension

        // Special
        'arguments', 'this'
    ];
}

/**
 * Get MQuickJS-specific extensions
 */
function getMQuickJSExtensions() {
    return [
        // Typed Arrays (MQuickJS extension)
        'Int8Array', 'Uint8Array', 'Uint8ClampedArray',
        'Int16Array', 'Uint16Array',
        'Int32Array', 'Uint32Array',
        'Float32Array', 'Float64Array',
        'ArrayBuffer', 'DataView'
    ];
}

/**
 * Main extraction process
 */
function main() {
    console.log('Extracting stdlib globals from MQuickJS and FreeButton sources...\n');

    // Read source files
    const mqjsContent = fs.readFileSync(MQUICKJS_STDLIB, 'utf8');
    const freeButtonContent = fs.readFileSync(FREEBUTTON_STDLIB, 'utf8');
    const typesContent = fs.readFileSync(FREEBUTTON_TYPES, 'utf8');

    // Extract globals
    const mqjsGlobals = extractMQuickJSGlobals(mqjsContent);
    const freeButtonGlobals = extractFreeButtonGlobals(freeButtonContent);
    const globalFunctions = extractGlobalFunctions(typesContent);
    const mqjsAPIs = extractMQuickJSAPIs(mqjsContent);
    const freeButtonAPIs = extractAPIMethods(typesContent);
    const es5Builtins = getES5Builtins();
    const mqjsExtensions = getMQuickJSExtensions();

    // Combine all globals
    const allGlobals = new Set([
        ...es5Builtins,
        ...mqjsExtensions,
        ...mqjsGlobals,
        ...freeButtonGlobals,
        ...globalFunctions
    ]);

    // Merge MQuickJS and FreeButton APIs
    const allAPIs = { ...mqjsAPIs, ...freeButtonAPIs };

    // Create output structure
    const output = {
        generated: new Date().toISOString(),
        sources: {
            mqjs_stdlib: path.relative(path.dirname(OUTPUT_FILE), MQUICKJS_STDLIB),
            freebutton_stdlib: path.relative(path.dirname(OUTPUT_FILE), FREEBUTTON_STDLIB),
            freebutton_types: path.relative(path.dirname(OUTPUT_FILE), FREEBUTTON_TYPES)
        },
        globals: Array.from(allGlobals).sort(),
        apis: allAPIs,
        counts: {
            total: allGlobals.size,
            es5_builtins: es5Builtins.length,
            mqjs_extensions: mqjsExtensions.length,
            mqjs_globals: mqjsGlobals.length,
            freebutton_apis: freeButtonGlobals.length,
            global_functions: globalFunctions.length
        }
    };

    // Write output
    fs.writeFileSync(OUTPUT_FILE, JSON.stringify(output, null, 2));

    // Report
    console.log('✓ Found globals:');
    console.log(`  - ES5 built-ins: ${output.counts.es5_builtins}`);
    console.log(`  - MQuickJS extensions: ${output.counts.mqjs_extensions}`);
    console.log(`  - MQuickJS stdlib: ${output.counts.mqjs_globals}`);
    console.log(`  - FreeButton APIs: ${output.counts.freebutton_apis}`);
    console.log(`  - Global functions: ${output.counts.global_functions}`);
    console.log(`  - Total unique globals: ${output.counts.total}\n`);

    console.log('✓ All APIs with methods:');
    for (const [api, methods] of Object.entries(allAPIs)) {
        console.log(`  - ${api}: ${methods.join(', ')}`);
    }

    console.log(`\n✓ Wrote globals to: ${path.relative(process.cwd(), OUTPUT_FILE)}`);
}

main();
