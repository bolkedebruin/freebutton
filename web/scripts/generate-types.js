#!/usr/bin/env node
/**
 * Generate TypeScript definitions from FreeButton C stdlib
 * Parses the JavaScript API comments in C source files
 */

import { readFileSync, writeFileSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Paths to C source files
const LIB_PATH = join(__dirname, '../../lib/mquickjs');
const OUTPUT_PATH = join(__dirname, '../src/js/freebutton.d.ts');

const sourceFiles = [
    join(LIB_PATH, 'freebutton_led.c'),
    join(LIB_PATH, 'freebutton_button.c'),
    join(LIB_PATH, 'freebutton_sensor.c'),
    join(LIB_PATH, 'freebutton_mqtt.c')
];

/**
 * Parse JavaScript API from C file comments
 */
function parseJavaScriptAPI(content) {
    const apiRegex = /\/\*[\s\S]*?JavaScript API:([\s\S]*?)\*\//;
    const match = content.match(apiRegex);

    if (!match) return null;

    const apiBlock = match[1];
    const lines = apiBlock.split('\n')
        .map(line => line.trim())
        .filter(line => line.startsWith('*'))
        .map(line => line.substring(1).trim())
        .filter(line => line.length > 0);

    const methods = [];

    for (const line of lines) {
        // Parse: "object.method(params) - Description"
        const methodMatch = line.match(/^(\w+)\.(\w+)\((.*?)\)\s*-\s*(.+)$/);
        if (methodMatch) {
            const [, object, method, params, description] = methodMatch;
            methods.push({
                object,
                method,
                params: params.split(',').map(p => p.trim()).filter(p => p),
                description
            });
        }
    }

    return methods;
}

/**
 * Convert parameter to TypeScript type
 */
function parseParamType(param) {
    // Remove optional markers
    const isOptional = param.includes('?') || param.includes('[');
    const cleanParam = param.replace(/[\[\]?]/g, '').trim();

    // Common patterns
    if (cleanParam.includes('position') || cleanParam.includes('Id') || cleanParam.endsWith('id')) {
        return { name: cleanParam, type: 'number', optional: isOptional };
    }
    if (cleanParam.includes('text') || cleanParam.includes('topic') || cleanParam.includes('payload') || cleanParam.includes('name')) {
        return { name: cleanParam, type: 'string', optional: isOptional };
    }
    if (cleanParam.includes('callback')) {
        return { name: cleanParam, type: 'Function', optional: isOptional };
    }
    if (cleanParam === 'r' || cleanParam === 'g' || cleanParam === 'b' || cleanParam === 'qos' || cleanParam === 'delay' || cleanParam === 'interval') {
        return { name: cleanParam, type: 'number', optional: isOptional };
    }
    if (cleanParam === 'retain') {
        return { name: cleanParam, type: 'boolean', optional: isOptional };
    }

    // Default to any
    return { name: cleanParam, type: 'any', optional: isOptional };
}

/**
 * Infer return type from method name
 */
function inferReturnType(method, description) {
    if (method === 'count' || method.startsWith('get')) {
        if (description.toLowerCase().includes('number')) return 'number';
        if (description.toLowerCase().includes('array')) return 'any[]';
        if (description.toLowerCase().includes('object')) return 'any';
        if (description.toLowerCase().includes('string') || description.toLowerCase().includes('name') || description.toLowerCase().includes('type')) return 'string';
        if (description.toLowerCase().includes('boolean') || method.startsWith('is')) return 'boolean';
        return 'any';
    }
    return 'void';
}

/**
 * Generate TypeScript interface for an API object
 */
function generateInterface(objectName, methods) {
    const interfaceName = objectName.charAt(0).toUpperCase() + objectName.slice(1) + 'API';
    let output = `interface ${interfaceName} {\n`;

    for (const method of methods) {
        const params = method.params
            .map(p => parseParamType(p))
            .map(p => `${p.name}${p.optional ? '?' : ''}: ${p.type}`)
            .join(', ');

        const returnType = inferReturnType(method.method, method.description);

        output += `    /** ${method.description} */\n`;
        output += `    ${method.method}(${params}): ${returnType};\n\n`;
    }

    output += '}\n';
    return { interfaceName, output };
}

/**
 * Main generation function
 */
function generateTypes() {
    console.log('🔍 Parsing FreeButton stdlib C files...\n');

    const apisByObject = {};

    // Parse all source files
    for (const file of sourceFiles) {
        try {
            const content = readFileSync(file, 'utf8');
            const methods = parseJavaScriptAPI(content);

            if (methods && methods.length > 0) {
                const objectName = methods[0].object;
                apisByObject[objectName] = methods;
                console.log(`✓ Found ${methods.length} methods for ${objectName}`);
            }
        } catch (err) {
            console.error(`✗ Error reading ${file}:`, err.message);
        }
    }

    console.log('\n📝 Generating TypeScript definitions...\n');

    // Generate header
    let output = `/**
 * FreeButton Global Type Declarations
 * Auto-generated from C stdlib source files
 * DO NOT EDIT MANUALLY - Run: npm run generate-types
 */

`;

    // Generate interfaces
    const interfaces = [];
    for (const [objectName, methods] of Object.entries(apisByObject)) {
        const { interfaceName, output: interfaceCode } = generateInterface(objectName, methods);
        interfaces.push({ objectName, interfaceName });
        output += interfaceCode + '\n';
    }

    // Generate global declarations
    output += '// ============================================================================\n';
    output += '// Global Variables\n';
    output += '// ============================================================================\n\n';

    for (const { objectName, interfaceName } of interfaces) {
        output += `declare const ${objectName}: ${interfaceName};\n`;
    }

    // Add custom global functions
    output += `
// ============================================================================
// Global Functions
// ============================================================================

/** Print to console */
declare function print(...args: any[]): void;

/** Execute callback after delay in milliseconds */
declare function setTimeout(callback: () => void, delay: number): number;

/** Execute callback repeatedly at interval in milliseconds */
declare function setInterval(callback: () => void, interval: number): number;

/** Cancel a timeout */
declare function clearTimeout(id: number): void;

/** Cancel an interval */
declare function clearInterval(id: number): void;

// ============================================================================
// Compiler API (loaded via WASM)
// ============================================================================

interface CompileOptions {
    targetAddr?: number;
    use32Bit?: boolean;
}

interface CompilerVersion {
    compiler: string;
    features: string[];
    platform: string;
}

interface MQuickJSCompiler {
    ready: boolean;
    init(): Promise<void>;
    compile(sourceCode: string, options?: CompileOptions): Promise<Uint8Array>;
    validate(sourceCode: string): Promise<boolean>;
    getVersion(): CompilerVersion;
}

declare const mqjsCompiler: MQuickJSCompiler;
`;

    // Write output
    writeFileSync(OUTPUT_PATH, output, 'utf8');

    console.log(`✓ Generated TypeScript definitions`);
    console.log(`📄 Output: ${OUTPUT_PATH}\n`);

    // Print summary
    console.log('Summary:');
    for (const { objectName, interfaceName } of interfaces) {
        const methodCount = apisByObject[objectName].length;
        console.log(`  ${objectName}: ${methodCount} methods (${interfaceName})`);
    }
}

// Run generator
try {
    generateTypes();
    console.log('Type generation complete');
} catch (err) {
    console.error('Error generating types:', err);
    process.exit(1);
}
