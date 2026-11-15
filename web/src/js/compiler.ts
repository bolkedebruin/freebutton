/**
 * WASM Compiler Wrapper
 * Loads and initializes the MicroQuickJS bytecode compiler
 */

import type { MQuickJSCompiler, CompileOptions } from './types';
import { signedFetch } from './auth';

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
        use32Bit: options.use32Bit !== false,
        romBase: options.romBase || 0,
    });
}

/**
 * Where the device's built-in (stdlib) table is, when the device says so and
 * it is the table this compiler was built with; else 0. With it the compiler
 * resolves the built-in names (Date, Math, ...) itself. Without it the device
 * looks them up while the upload arrives, and misses a name whose text comes
 * in a later part of the upload than the code that uses it.
 */
export function romBaseFor(info: { rom_base?: string; atoms_id?: string }, compilerStdlibId: number): number {
    if (!info.rom_base || !info.atoms_id || !compilerStdlibId) return 0;
    const device = parseInt(info.atoms_id, 16) >>> 0;
    const rom = parseInt(info.rom_base, 16) >>> 0;
    return device === (compilerStdlibId >>> 0) && rom !== 0 ? rom : 0;
}

/**
 * Ask the device for its built-in (stdlib) table: where it is and which one
 * it is (see romBaseFor()).
 * @param deviceUrl - The device base URL. Defaults to "" (same origin) so the
 *   IDE served from the device talks to that device without CORS.
 */
export async function getDeviceStdlibInfo(deviceUrl: string = ''): Promise<{
    rom_base?: string;
    atoms_id?: string;
}> {
    const response = await signedFetch(`${deviceUrl}/api/bytecode/next_address`);

    if (!response.ok) {
        throw new Error(`Failed to get the device's script library info: ${response.statusText}`);
    }

    return await response.json();
}

/**
 * Compile JavaScript for the device at `deviceUrl` ("" = same origin). The
 * bytecode is position-independent; the device is asked for its built-in
 * table so that the compiler can resolve the built-in names for it.
 * `atomsId` is the device's "atoms_id" when the compiler did that, else "".
 * The upload passes it on ("?atoms=<id>") so that the device needs no lookup
 * memory.
 */
export async function compileForDeviceEx(sourceCode: string, deviceUrl: string = ''): Promise<{ bytecode: Uint8Array; atomsId: string }> {
    const stdlibInfo = await getDeviceStdlibInfo(deviceUrl);

    console.log(`Compiling for device at ${deviceUrl}`);

    // Compile for the device's own built-in table when it is this compiler's.
    if (!compilerInstance) await initCompiler();
    const stdlibId = compilerInstance?.stdlibId?.() ?? 0;
    const romBase = romBaseFor(stdlibInfo, stdlibId);
    if (!romBase && stdlibInfo.atoms_id) {
        console.warn('The device firmware and this IDE were built from different script libraries: deploy may not run correctly. Update the device\'s IDE (filesystem) or firmware.');
    }
    const bytecode = await compile(sourceCode, { use32Bit: true, romBase });
    return { bytecode, atomsId: romBase ? (stdlibInfo.atoms_id ?? '') : '' };
}
