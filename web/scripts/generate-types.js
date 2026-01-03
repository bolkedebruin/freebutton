#!/usr/bin/env node
/**
 * Generate TypeScript definitions from stdlib.json
 * This replaces the old brittle C file parsing approach
 */

import { readFileSync, writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Paths
const STDLIB_JSON_PATH = join(__dirname, '../src/js/stdlib.json');
const OUTPUT_PATH = join(__dirname, '../src/js/freebutton.d.ts');

/**
 * Generate TypeScript interface for an API object
 */
function generateAPIInterface(apiName, api) {
    const interfaceName = apiName.charAt(0).toUpperCase() + apiName.slice(1) + 'API';
    let output = `export interface ${interfaceName} {\n`;

    for (const method of api.methods) {
        // Format parameters
        const params = method.params
            .map(p => `${p.name}: ${p.type}`)
            .join(', ');

        // Add JSDoc comment if description exists
        if (method.description) {
            output += `    /** ${method.description} */\n`;
        }

        output += `    ${method.name}(${params}): ${method.returns};\n\n`;
    }

    output += '}\n';
    return { interfaceName, output };
}

/**
 * Generate TypeScript interface for a stdlib class
 */
function generateClassInterface(className, classInfo) {
    let output = '';

    // Generate static methods interface if any
    if (classInfo.static && classInfo.static.length > 0) {
        output += `export interface ${className}Constructor {\n`;
        for (const method of classInfo.static) {
            const params = (method.params || [])
                .map(p => `${p.name}: ${p.type}`)
                .join(', ');

            if (method.description) {
                output += `    /** ${method.description} */\n`;
            }
            output += `    ${method.name}(${params}): ${method.returns};\n`;
        }
        output += '}\n\n';
    }

    // Generate prototype methods interface
    if (classInfo.prototype && classInfo.prototype.length > 0) {
        output += `export interface ${className} {\n`;
        for (const method of classInfo.prototype) {
            const params = (method.params || [])
                .map(p => `${p.name}: ${p.type}`)
                .join(', ');

            if (method.description) {
                output += `    /** ${method.description} */\n`;
            }
            output += `    ${method.name}(${params}): ${method.returns};\n`;
        }
        output += '}\n\n';
    }

    return output;
}

/**
 * Main generation function
 */
function generateTypes() {
    console.log('Reading stdlib.json...\n');

    // Read stdlib.json
    let stdlib;
    try {
        const stdlibContent = readFileSync(STDLIB_JSON_PATH, 'utf8');
        stdlib = JSON.parse(stdlibContent);
    } catch (err) {
        console.error(`Error reading ${STDLIB_JSON_PATH}:`, err.message);
        process.exit(1);
    }

    console.log('Generating TypeScript definitions...\n');

    // Generate header
    let output = `/**
 * FreeButton Global Type Declarations
 * Auto-generated from stdlib.json
 * DO NOT EDIT MANUALLY - Run: npm run generate-types
 */

`;

    // Generate FreeButton API interfaces
    const apiInterfaces = [];
    console.log('FreeButton APIs:');
    for (const [apiName, api] of Object.entries(stdlib.apis || {})) {
        const { interfaceName, output: interfaceCode } = generateAPIInterface(apiName, api);
        apiInterfaces.push({ apiName, interfaceName });
        output += interfaceCode + '\n';
        console.log(`  ${apiName}: ${api.methods.length} methods`);
    }

    // Generate stdlib class interfaces
    console.log('\nStdlib Classes:');
    for (const [className, classInfo] of Object.entries(stdlib.classes || {})) {
        output += generateClassInterface(className, classInfo);
        const staticCount = classInfo.static ? classInfo.static.length : 0;
        const protoCount = classInfo.prototype ? classInfo.prototype.length : 0;
        console.log(`  ${className}: ${staticCount} static, ${protoCount} prototype methods`);
    }

    // Generate global declarations
    output += '// ============================================================================\n';
    output += '// Global Variables and Functions\n';
    output += '// ============================================================================\n\n';
    output += 'declare global {\n';

    // FreeButton API globals
    for (const { apiName, interfaceName } of apiInterfaces) {
        output += `    const ${apiName}: ${interfaceName};\n`;
    }

    output += '\n';

    // Stdlib classes
    for (const className of Object.keys(stdlib.classes || {})) {
        // Check if class has static methods
        if (stdlib.classes[className].static && stdlib.classes[className].static.length > 0) {
            output += `    const ${className}: ${className}Constructor;\n`;
        }
    }

    output += '\n    // Global constants\n';

    // Global constants
    console.log('\nGlobal Constants:');
    for (const constant of stdlib.constants || []) {
        if (constant.description) {
            output += `    /** ${constant.description} */\n`;
        }
        output += `    const ${constant.name}: ${constant.type};\n`;
        console.log(`  ${constant.name}: ${constant.type}`);
    }

    output += '\n    // Global functions\n';

    // Global functions
    console.log('\nGlobal Functions:');
    for (const globalFunc of stdlib.globals || []) {
        const params = globalFunc.params
            .map(p => `${p.name}: ${p.type}`)
            .join(', ');

        if (globalFunc.description) {
            output += `    /** ${globalFunc.description} */\n`;
        }
        output += `    function ${globalFunc.name}(${params}): ${globalFunc.returns};\n`;
        console.log(`  ${globalFunc.name}`);
    }

    // Add test utilities (from test_language.js and test_builtin.js)
    output += '\n    // Test utility functions\n';
    output += '    /** Throws an error with the given message (test utility) */\n';
    output += '    function throw_error(msg: string): never;\n';
    output += '    /** Asserts that actual equals expected, throws error if not (test utility) */\n';
    output += '    function assert(actual: any, expected?: any, message?: string): void;\n';
    output += '    /** Asserts that a function throws the expected error type (test utility) */\n';
    output += '    function assert_throws(expected_error: any, func: Function): void;\n';

    output += '}\n';

    // Add custom compiler API (not in stdlib.json, specific to editor)
    output += `
// ============================================================================
// Compiler API (loaded via WASM)
// ============================================================================

export interface CompileOptions {
    targetAddr?: number;
    use32Bit?: boolean;
}

export interface CompilerVersion {
    compiler: string;
    features: string[];
    platform: string;
}

export interface MQuickJSCompiler {
    ready: boolean;
    init(): Promise<void>;
    compile(sourceCode: string, options?: CompileOptions): Promise<Uint8Array>;
    validate(sourceCode: string): Promise<boolean>;
    getVersion(): CompilerVersion;
}

declare global {
    const mqjsCompiler: MQuickJSCompiler;
}
`;

    // Write output
    writeFileSync(OUTPUT_PATH, output, 'utf8');

    console.log(`\n✓ Generated TypeScript definitions`);
    console.log(`  Output: ${OUTPUT_PATH}\n`);
}

// Run generator
try {
    generateTypes();
} catch (err) {
    console.error('Error generating types:', err);
    process.exit(1);
}
