/**
 * UI Controls and Interactions
 * Handles all user interface logic, status messages, and bytecode display
 */

import { getCode, clearEditor as clearEditorContent, loadExample } from './editor';
import { compile, validate } from './compiler';
import { validateCode, formatValidationErrors } from './validator';
import { signedFetch } from './auth';

type StatusType = 'info' | 'success' | 'error';

// Global state
let compiledBytecode: Uint8Array | null = null;

/**
 * Show status message
 * @param message - The message to display
 * @param type - Message type: 'info', 'success', 'error'
 */
export function showStatus(message: string, type: StatusType = 'info'): void {
    const statusDiv = document.getElementById('status');
    if (!statusDiv) return;

    const icon = type === 'success' ? '✓' : type === 'error' ? '✗' : 'ℹ';
    statusDiv.innerHTML = `<span class="status-icon">${icon}</span>${message}`;
    statusDiv.className = 'status ' + type;
    statusDiv.style.display = 'block';

    // Auto-hide success and error messages after 5 seconds
    if (type === 'success' || type === 'error') {
        setTimeout(() => {
            statusDiv.style.display = 'none';
        }, 5000);
    }
}

/**
 * Format bytes as hex dump
 * @param bytes - The byte array to format
 * @param limit - Maximum number of bytes to display (default: 256)
 * @returns Formatted hex dump
 */
function formatHexDump(bytes: Uint8Array, limit: number = 256): string {
    const displayBytes = bytes.slice(0, limit);
    let output = '';

    for (let i = 0; i < displayBytes.length; i += 16) {
        // Offset
        output += i.toString(16).padStart(8, '0') + ':  ';

        // Hex values
        for (let j = 0; j < 16; j++) {
            if (i + j < displayBytes.length) {
                output += displayBytes[i + j]!.toString(16).padStart(2, '0') + ' ';
            } else {
                output += '   ';
            }
            if (j === 7) output += ' ';
        }

        output += ' |';

        // ASCII representation
        for (let j = 0; j < 16 && i + j < displayBytes.length; j++) {
            const byte = displayBytes[i + j]!;
            output += (byte >= 32 && byte < 127) ? String.fromCharCode(byte) : '.';
        }

        output += '|\n';
    }

    if (bytes.length > limit) {
        output += `\n... (${bytes.length - limit} more bytes)`;
    }

    return output;
}

/**
 * Display bytecode information
 * @param bytecode - The compiled bytecode
 */
function displayBytecodeInfo(bytecode: Uint8Array): void {
    const view = new DataView(bytecode.buffer);

    // Read header (little-endian)
    const magic = view.getUint16(0, true);
    const version = view.getUint16(2, true);

    const sizeElem = document.getElementById('bytecodeSize');
    const magicElem = document.getElementById('bytecodeMagic');
    const versionElem = document.getElementById('bytecodeVersion');
    const hexPreviewElem = document.getElementById('hexPreview');
    const bytecodeInfoElem = document.getElementById('bytecodeInfo');

    if (sizeElem) sizeElem.textContent = bytecode.length.toString();
    if (magicElem) magicElem.textContent = '0x' + magic.toString(16).padStart(4, '0');
    if (versionElem) versionElem.textContent = version.toString();

    // Show hex dump
    if (hexPreviewElem) hexPreviewElem.textContent = formatHexDump(bytecode);

    if (bytecodeInfoElem) bytecodeInfoElem.style.display = 'block';
}

/**
 * Compile the current code in the editor
 */
export async function compileCode(): Promise<void> {
    const sourceCode = getCode();

    if (!sourceCode.trim()) {
        showStatus('Please enter some JavaScript code', 'error');
        return;
    }

    // Pre-validate code before compilation
    showStatus('Validating code...', 'info');
    const validationResult = validateCode(sourceCode);

    if (!validationResult.valid) {
        const errorMessages = formatValidationErrors(validationResult.errors);
        showStatus('Validation failed:\n' + errorMessages, 'error');
        return;
    }

    showStatus('Compiling...', 'info');
    const compileBtn = document.getElementById('compileBtn') as HTMLButtonElement | null;
    if (compileBtn) compileBtn.disabled = true;

    try {
        // Always compile as position-independent bytecode (targetAddr: 0)
        // Device firmware will perform relocation on upload using streaming algorithm
        compiledBytecode = await compile(sourceCode, {
            targetAddr: 0,
            use32Bit: true
        });
        showStatus(`✓ Compilation successful! (${compiledBytecode.length} bytes, position-independent)`, 'success');

        // Show bytecode info
        displayBytecodeInfo(compiledBytecode);

        const downloadBtn = document.getElementById('downloadBtn') as HTMLButtonElement | null;
        if (downloadBtn) downloadBtn.disabled = false;

        // Upload targets the serving device by default (same origin), so the
        // compile & upload action is always available after a successful compile.
        const compileUploadBtn = document.getElementById('compileUploadBtn') as HTMLButtonElement | null;
        if (compileUploadBtn) compileUploadBtn.disabled = false;

    } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        showStatus('Compilation failed: ' + errorMessage, 'error');
        compiledBytecode = null;

        const bytecodeInfoElem = document.getElementById('bytecodeInfo');
        if (bytecodeInfoElem) bytecodeInfoElem.style.display = 'none';

        const downloadBtn = document.getElementById('downloadBtn') as HTMLButtonElement | null;
        if (downloadBtn) downloadBtn.disabled = true;

        const compileUploadBtn = document.getElementById('compileUploadBtn') as HTMLButtonElement | null;
        if (compileUploadBtn) compileUploadBtn.disabled = true;
    } finally {
        if (compileBtn) compileBtn.disabled = false;
    }
}

/**
 * Validate the current code syntax
 */
export async function validateCodeUI(): Promise<void> {
    const sourceCode = getCode();

    if (!sourceCode.trim()) {
        showStatus('Please enter some JavaScript code', 'error');
        return;
    }

    showStatus('Validating...', 'info');
    const validateBtn = document.getElementById('validateBtn') as HTMLButtonElement | null;
    if (validateBtn) validateBtn.disabled = true;

    try {
        // First run static validation
        const validationResult = validateCode(sourceCode);

        if (!validationResult.valid) {
            const errorMessages = formatValidationErrors(validationResult.errors);
            showStatus('Validation failed:\n' + errorMessages, 'error');
            return;
        }

        // Then run compiler validation
        await validate(sourceCode);
        showStatus('✓ Code is valid!', 'success');
    } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        showStatus('Validation failed: ' + errorMessage, 'error');
    } finally {
        if (validateBtn) validateBtn.disabled = false;
    }
}

/**
 * Download the compiled bytecode as a .jsc file
 */
export function downloadBytecode(): void {
    if (!compiledBytecode) {
        showStatus('Please compile first', 'error');
        return;
    }

    const blob = new Blob([compiledBytecode as BlobPart], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);

    const a = document.createElement('a');
    a.href = url;
    a.download = 'script.jsc';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);

    showStatus('Downloaded script.jsc', 'success');
}

/**
 * Compile and upload bytecode to device
 */
export async function compileAndUpload(): Promise<void> {
    // Empty deviceUrl means same origin: the IDE served from the device uploads
    // back to that device (fetch('/api/...')). An explicit URL still works for a
    // dev build talking to a remote device.
    const deviceUrlInput = document.getElementById('deviceUrl') as HTMLInputElement | null;
    const deviceUrl = deviceUrlInput?.value.trim() ?? '';

    const sourceCode = getCode();

    if (!sourceCode.trim()) {
        showStatus('Please enter some JavaScript code', 'error');
        return;
    }

    // Pre-validate code
    showStatus('Validating code...', 'info');
    const validationResult = validateCode(sourceCode);

    if (!validationResult.valid) {
        const errorMessages = formatValidationErrors(validationResult.errors);
        showStatus('Validation failed:\n' + errorMessages, 'error');
        return;
    }

    const compileUploadBtn = document.getElementById('compileUploadBtn') as HTMLButtonElement | null;
    if (compileUploadBtn) compileUploadBtn.disabled = true;

    try {
        // Compile as position-independent bytecode (device will relocate on upload)
        showStatus('Compiling...', 'info');

        compiledBytecode = await compile(sourceCode, {
            targetAddr: 0,
            use32Bit: true
        });
        showStatus(`✓ Compiled! (${compiledBytecode.length} bytes) - Uploading...`, 'info');

        // Upload to device
        const formData = new FormData();
        const blob = new Blob([compiledBytecode as BlobPart], { type: 'application/octet-stream' });
        formData.append('file', blob, 'script.jsc');

        const uploadResponse = await signedFetch(`${deviceUrl}/api/bytecode/upload`, {
            method: 'POST',
            body: formData
        });

        if (!uploadResponse.ok) {
            throw new Error(`Upload failed: ${uploadResponse.statusText}`);
        }

        const result = await uploadResponse.json();
        if (result.status !== 'success') {
            throw new Error(result.message || 'Upload failed');
        }

        showStatus(`✓ Upload successful! (${compiledBytecode.length} bytes relocated and written to flash)`, 'success');

        // Show bytecode info
        displayBytecodeInfo(compiledBytecode);

        const downloadBtn = document.getElementById('downloadBtn') as HTMLButtonElement | null;
        if (downloadBtn) downloadBtn.disabled = false;

    } catch (err) {
        const errorMessage = err instanceof Error ? err.message : String(err);
        showStatus('Upload failed: ' + errorMessage, 'error');
        compiledBytecode = null;

        const bytecodeInfoElem = document.getElementById('bytecodeInfo');
        if (bytecodeInfoElem) bytecodeInfoElem.style.display = 'none';

        const downloadBtn = document.getElementById('downloadBtn') as HTMLButtonElement | null;
        if (downloadBtn) downloadBtn.disabled = true;
    } finally {
        if (compileUploadBtn) compileUploadBtn.disabled = false;
    }
}

/**
 * Clear the editor and reset state
 */
export function clearEditor(): void {
    clearEditorContent();

    const bytecodeInfoElem = document.getElementById('bytecodeInfo');
    if (bytecodeInfoElem) bytecodeInfoElem.style.display = 'none';

    compiledBytecode = null;

    const downloadBtn = document.getElementById('downloadBtn') as HTMLButtonElement | null;
    if (downloadBtn) downloadBtn.disabled = true;

    const compileUploadBtn = document.getElementById('compileUploadBtn') as HTMLButtonElement | null;
    if (compileUploadBtn) compileUploadBtn.disabled = true;
}

/**
 * Load an example script
 * @param name - The example name
 */
export function loadExampleScript(name: string): void {
    if (loadExample(name)) {
        showStatus('Loaded ' + name + ' example', 'success');
    } else {
        showStatus('Example not found: ' + name, 'error');
    }
}

/**
 * Initialize all UI event listeners
 */
export function initUI(): void {
    // Compile button
    const compileBtn = document.getElementById('compileBtn');
    if (compileBtn) {
        compileBtn.addEventListener('click', compileCode);
    }

    // Validate button
    const validateBtn = document.getElementById('validateBtn');
    if (validateBtn) {
        validateBtn.addEventListener('click', validateCodeUI);
    }

    // Compile & Upload button
    const compileUploadBtn = document.getElementById('compileUploadBtn');
    if (compileUploadBtn) {
        compileUploadBtn.addEventListener('click', compileAndUpload);
    }

    // Download button
    const downloadBtn = document.getElementById('downloadBtn');
    if (downloadBtn) {
        downloadBtn.addEventListener('click', downloadBytecode);
    }

    // Clear button
    const clearBtn = document.getElementById('clearBtn');
    if (clearBtn) {
        clearBtn.addEventListener('click', clearEditor);
    }

    // Device URL input - enable/disable Compile & Upload button
    const deviceUrlInput = document.getElementById('deviceUrl') as HTMLInputElement | null;
    if (deviceUrlInput) {
        deviceUrlInput.addEventListener('input', () => {
            const compileUploadBtn = document.getElementById('compileUploadBtn') as HTMLButtonElement | null;
            if (compileUploadBtn) {
                const hasDeviceUrl = deviceUrlInput.value.trim().length > 0;
                // Enable button if device URL is set and code is available
                compileUploadBtn.disabled = !hasDeviceUrl;
            }
        });
    }

    // Example buttons
    const exampleBlink = document.getElementById('exampleBlink');
    if (exampleBlink) {
        exampleBlink.addEventListener('click', () => loadExampleScript('blink'));
    }

    const exampleSensor = document.getElementById('exampleSensor');
    if (exampleSensor) {
        exampleSensor.addEventListener('click', () => loadExampleScript('sensor'));
    }

    const exampleButton = document.getElementById('exampleButton');
    if (exampleButton) {
        exampleButton.addEventListener('click', () => loadExampleScript('button'));
    }

    const exampleMqtt = document.getElementById('exampleMqtt');
    if (exampleMqtt) {
        exampleMqtt.addEventListener('click', () => loadExampleScript('mqtt_simple'));
    }
}
