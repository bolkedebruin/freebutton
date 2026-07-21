/**
 * WASM Compiler Wrapper
 * Loads and initializes the MicroQuickJS bytecode compiler
 */

import type { MQuickJSCompiler, CompileOptions, CompilerVersion } from './types';

let compilerInstance: MQuickJSCompiler | null = null;
let initializationPromise: Promise<void> | null = null;

/**
 * Dynamically load the WASM compiler scripts
 */
async function loadCompilerScripts(): Promise<void> {
    // Load the Emscripten-generated module
    return new Promise((resolve, reject) => {
        const script1 = document.createElement('script');
        script1.src = 'assets/mquickjs.js';
        script1.onload = () => {
            // Load the compiler wrapper
            const script2 = document.createElement('script');
            script2.src = 'assets/mquickjs-compiler.js';
            script2.onload = () => resolve();
            script2.onerror = () => reject(new Error('Failed to load mquickjs-compiler.js'));
            document.head.appendChild(script2);
        };
        script1.onerror = () => reject(new Error('Failed to load mquickjs.js'));
        document.head.appendChild(script1);
    });
}

/**
 * Initialize the WASM compiler
 */
export async function initCompiler(): Promise<void> {
    // Return existing promise if already initializing
    if (initializationPromise) {
        return initializationPromise;
    }

    // Return immediately if already initialized
    if (compilerInstance && compilerInstance.ready) {
        return;
    }

    initializationPromise = (async () => {
        try {
            console.log('Loading WASM compiler scripts...');
            await loadCompilerScripts();

            console.log('Initializing WASM compiler...');
            // mqjsCompiler is now available globally from mquickjs-compiler.js
            if (typeof mqjsCompiler === 'undefined') {
                throw new Error('mqjsCompiler not found - script loading failed');
            }

            compilerInstance = mqjsCompiler;
            await compilerInstance.init();
            console.log('WASM compiler ready');
        } catch (err) {
            console.error('Failed to initialize compiler:', err);
            throw err;
        }
    })();

    return initializationPromise;
}

/**
 * Compile JavaScript source code to bytecode
 * @param sourceCode - The JavaScript source code
 * @param options - Compilation options
 * @returns The compiled bytecode
 */
export async function compile(sourceCode: string, options: CompileOptions = {}): Promise<Uint8Array> {
    if (!compilerInstance) {
        await initCompiler();
    }

    if (!compilerInstance) {
        throw new Error('Compiler not initialized');
    }

    return await compilerInstance.compile(sourceCode, {
        targetAddr: options.targetAddr || 0,
        use32Bit: options.use32Bit !== false
    });
}

/**
 * Validate JavaScript syntax
 * @param sourceCode - The JavaScript source code
 * @returns True if valid
 * @throws Validation error
 */
export async function validate(sourceCode: string): Promise<boolean> {
    if (!compilerInstance) {
        await initCompiler();
    }

    if (!compilerInstance) {
        throw new Error('Compiler not initialized');
    }

    return await compilerInstance.validate(sourceCode);
}

/**
 * Get compiler version information
 * @returns Version info
 */
export function getVersion(): CompilerVersion {
    if (!compilerInstance) {
        return {
            compiler: 'Not initialized',
            features: [],
            platform: 'WebAssembly'
        };
    }

    return compilerInstance.getVersion();
}

/**
 * Check if compiler is ready
 */
export function isReady(): boolean {
    return compilerInstance !== null && compilerInstance.ready;
}

/**
 * Fetch the next available virtual address from the device
 * @param deviceUrl - The device base URL. Defaults to "" (same origin) so the
 *   IDE served from the device talks to that device without CORS.
 * @returns Virtual address information
 */
export async function getDeviceVirtualAddress(deviceUrl: string = ''): Promise<{
    offset: string;
    virtual_addr: string;
    free_space: number;
}> {
    const response = await fetch(`${deviceUrl}/api/bytecode/next_address`);

    if (!response.ok) {
        throw new Error(`Failed to get virtual address: ${response.statusText}`);
    }

    return await response.json();
}

/**
 * Compile JavaScript for a specific device
 * Automatically queries the device for the correct virtual memory address
 * @param sourceCode - The JavaScript source code
 * @param deviceUrl - The device base URL. Defaults to "" (same origin).
 * @returns The compiled bytecode pre-relocated for the device
 */
export async function compileForDevice(sourceCode: string, deviceUrl: string = ''): Promise<Uint8Array> {
    // Get the target virtual address from the device
    const addressInfo = await getDeviceVirtualAddress(deviceUrl);

    // Parse hex address to number
    const targetAddr = parseInt(addressInfo.virtual_addr, 16);

    console.log(`Compiling for device at ${deviceUrl}`);
    console.log(`Target virtual address: ${addressInfo.virtual_addr} (${targetAddr})`);
    console.log(`Free space: ${addressInfo.free_space} bytes`);

    // Compile with the correct target address
    return await compile(sourceCode, {
        targetAddr,
        use32Bit: true
    });
}
