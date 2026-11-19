#!/usr/bin/env node
/**
 * Generate autocomplete definitions from stdlib.json
 * Creates autocomplete-data.ts with API objects and global completions
 */

import { readFileSync, writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
import { applyFbExtras } from './fb-api-extras.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const STDLIB_JSON_PATH = join(__dirname, '../src/js/stdlib.json');
const OUTPUT_PATH = join(__dirname, '../src/js/autocomplete-data.ts');

/**
 * Generate autocomplete snippet with placeholders
 */
function generateSnippet(name, params) {
    if (!params || params.length === 0) {
        return `${name}()`;
    }

    let snippet = `${name}(`;
    snippet += params.map((p, i) => {
        // Special handling for Function type
        if (p.type === 'Function') {
            return `function() {\n\t\${${i + 1}}\n}`;
        }
        return `\${${i + 1}:${p.name}}`;
    }).join(', ');
    snippet += ')';

    return snippet;
}

/**
 * Generate detail string (signature)
 */
function generateDetail(params, returnType) {
    if (!params || params.length === 0) {
        return `() → ${returnType}`;
    }

    const paramsStr = params.map(p => `${p.name}: ${p.type}`).join(', ');
    return `(${paramsStr}) → ${returnType}`;
}

/**
 * Generate autocomplete data from stdlib.json
 */
function generateAutocomplete() {
    console.log('Reading stdlib.json...\n');

    // Read stdlib.json
    let stdlib;
    try {
        const stdlibContent = readFileSync(STDLIB_JSON_PATH, 'utf8');
        stdlib = applyFbExtras(JSON.parse(stdlibContent));
    } catch (err) {
        console.error(`Error reading ${STDLIB_JSON_PATH}:`, err.message);
        process.exit(1);
    }

    console.log('Generating autocomplete data...\n');

    const apiObjects = {};
    const globalCompletions = [];

    // FreeButton APIs (led, button, sensor, mqtt, console, performance)
    console.log('FreeButton APIs:');
    for (const [apiName, api] of Object.entries(stdlib.apis || {})) {
        const methods = [];
        for (const method of api.methods) {
            methods.push({
                label: `${apiName}.${method.name}`,
                type: 'method',
                detail: generateDetail(method.params, method.returns),
                info: method.description || '',
                apply: generateSnippet(`${apiName}.${method.name}`, method.params)
            });
        }
        apiObjects[apiName] = { methods };
        console.log(`  ${apiName}: ${api.methods.length} methods`);
    }

    // Stdlib classes with static methods (Math, Object, String, Number, etc.)
    console.log('\nStdlib Class Static Methods:');
    for (const [className, classInfo] of Object.entries(stdlib.classes || {})) {
        if (classInfo.static && classInfo.static.length > 0) {
            const methods = [];
            for (const method of classInfo.static) {
                methods.push({
                    label: `${className}.${method.name}`,
                    type: 'method',
                    detail: generateDetail(method.params, method.returns),
                    info: method.description || '',
                    apply: generateSnippet(`${className}.${method.name}`, method.params)
                });
            }
            // Add class to API objects for member expression completion
            apiObjects[className] = { methods };
            console.log(`  ${className}: ${classInfo.static.length} static methods`);
        }
    }

    // Global functions
    console.log('\nGlobal Functions:');
    for (const globalFunc of stdlib.globals || []) {
        globalCompletions.push({
            label: globalFunc.name,
            type: 'function',
            detail: generateDetail(globalFunc.params, globalFunc.returns),
            info: globalFunc.description || '',
            apply: generateSnippet(globalFunc.name, globalFunc.params)
        });
        console.log(`  ${globalFunc.name}`);
    }

    // fb hierarchy completions (runtime-constructed device API)
    if (stdlib.fb) {
        const fbTypes = stdlib.fb.types || {};
        const root = stdlib.fb.root;

        // Helper: generate completions for all paths through the hierarchy
        function addFbCompletions(prefix, typeName, depth) {
            if (depth > 4) return; // prevent infinite recursion

            const typeDef = fbTypes[typeName];
            if (!typeDef) return;

            // Add method completions
            if (typeDef.methods) {
                for (const method of typeDef.methods) {
                    const label = `${prefix}.${method.name}`;
                    methods.push({
                        label,
                        type: 'method',
                        detail: generateDetail(method.params, method.returns),
                        info: method.description || '',
                        apply: generateSnippet(label, method.params)
                    });
                }
            }

            // Recurse into properties
            if (typeDef.properties) {
                for (const prop of typeDef.properties) {
                    const propType = prop.type.replace('[]', '');
                    if (fbTypes[propType]) {
                        addFbCompletions(`${prefix}.${prop.name}`, propType, depth + 1);
                    }
                }
            }
        }

        // Build fb completions from root methods (fb.button) and properties
        const methods = [];
        for (const method of (root && root.methods) || []) {
            const label = `fb.${method.name}`;
            methods.push({
                label,
                type: 'method',
                detail: generateDetail(method.params, method.returns),
                info: method.description || '',
                apply: generateSnippet(label, method.params)
            });
        }
        if (root && root.properties) {
            for (const prop of root.properties) {
                const propType = prop.type.replace('[]', '');
                const propPrefix = prop.type.endsWith('[]')
                    ? `fb.${prop.name}[n]`
                    : `fb.${prop.name}`;
                if (fbTypes[propType]) {
                    addFbCompletions(propPrefix, propType, 0);
                }
            }
        }

        if (methods.length > 0) {
            apiObjects['fb'] = { methods };
            console.log(`\nfb hierarchy: ${methods.length} completions`);
        }
    }

    // Test utility functions (from test_language.js and test_builtin.js)
    const testUtilities = [
        {
            name: 'throw_error',
            params: [{ name: 'msg', type: 'string' }],
            returns: 'never',
            description: 'Throws an error with the given message (test utility)'
        },
        {
            name: 'assert',
            params: [
                { name: 'actual', type: 'any' },
                { name: 'expected', type: 'any' },
                { name: 'message', type: 'string' }
            ],
            returns: 'void',
            description: 'Asserts that actual equals expected, throws error if not (test utility)'
        },
        {
            name: 'assert_throws',
            params: [
                { name: 'expected_error', type: 'any' },
                { name: 'func', type: 'Function' }
            ],
            returns: 'void',
            description: 'Asserts that a function throws the expected error type (test utility)'
        }
    ];

    console.log('\nTest Utilities:');
    for (const testFunc of testUtilities) {
        globalCompletions.push({
            label: testFunc.name,
            type: 'function',
            detail: generateDetail(testFunc.params, testFunc.returns),
            info: testFunc.description,
            apply: generateSnippet(testFunc.name, testFunc.params)
        });
        console.log(`  ${testFunc.name}`);
    }

    // Generate TypeScript output
    let output = `/**
 * Autocomplete Data for FreeButton Editor
 * Auto-generated from stdlib.json
 * DO NOT EDIT MANUALLY - Run: npm run generate-autocomplete
 */

export interface CompletionItem {
    label: string;
    type: string;
    detail: string;
    info: string;
    apply: string;
}

export interface APIDefinition {
    methods: CompletionItem[];
}

export interface FreebuttonAPIDefinition {
    [key: string]: APIDefinition;
}

export const freebuttonAPI: FreebuttonAPIDefinition = `;

    output += JSON.stringify(apiObjects, null, 2);
    output += ';\n\n';

    output += 'export const globalCompletions: CompletionItem[] = ';
    output += JSON.stringify(globalCompletions, null, 2);
    output += ';\n';

    // Write output
    writeFileSync(OUTPUT_PATH, output, 'utf8');

    const totalMethods = Object.values(apiObjects).reduce((sum, api) => sum + api.methods.length, 0);
    const totalCompletions = totalMethods + globalCompletions.length;

    console.log(`\n✓ Generated autocomplete data`);
    console.log(`  Output: ${OUTPUT_PATH}`);
    console.log(`  API objects: ${Object.keys(apiObjects).length}`);
    console.log(`  Total methods: ${totalMethods}`);
    console.log(`  Global functions: ${globalCompletions.length}`);
    console.log(`  Total completions: ${totalCompletions}\n`);
}

// Run generator
try {
    generateAutocomplete();
} catch (err) {
    console.error('Error generating autocomplete:', err);
    process.exit(1);
}
