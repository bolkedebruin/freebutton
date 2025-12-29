/**
 * FreeButton JavaScript Editor - Main Entry Point
 * Initializes the editor, compiler, and UI
 */

import { initEditor, loadExample } from './editor';
import { initCompiler } from './compiler';
import { initUI, showStatus } from './ui';

/**
 * Initialize the application
 */
async function init(): Promise<void> {
    try {
        console.log('Starting FreeButton JavaScript Editor...');

        // Create CodeMirror editor
        console.log('Creating CodeMirror editor...');
        const editorContainer = document.getElementById('codeEditor');
        if (!editorContainer) {
            throw new Error('Editor container not found');
        }
        initEditor(editorContainer);
        console.log('Editor created successfully');

        // Initialize UI event listeners
        console.log('Initializing UI...');
        initUI();

        // Initialize WASM compiler
        console.log('Initializing WASM compiler...');
        await initCompiler();
        console.log('WASM compiler initialized');

        // Hide loading indicator, show editor
        const loadingIndicator = document.getElementById('loadingIndicator');
        const editorSection = document.getElementById('editorSection');

        if (loadingIndicator) loadingIndicator.style.display = 'none';
        if (editorSection) editorSection.style.display = 'block';

        // Load default example
        loadExample('blink');

        showStatus('✓ Compiler ready! Try compiling the example code.', 'success');

        console.log('FreeButton JavaScript Editor ready!');
    } catch (err) {
        console.error('Initialization error:', err);
        const loadingDiv = document.getElementById('loadingIndicator');
        if (loadingDiv) {
            const errorMessage = err instanceof Error ? err.message : String(err);
            loadingDiv.innerHTML = `
                <h2 style="color: #721c24;">Failed to load</h2>
                <p>${errorMessage}</p>
                <p>Check browser console for details</p>
                <p>Make sure you're running this from a web server (not file://)</p>
            `;
        }
    }
}

// Start initialization when DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
} else {
    init();
}
