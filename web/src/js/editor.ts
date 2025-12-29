import { EditorView, basicSetup } from 'codemirror';
import { javascript } from '@codemirror/lang-javascript';
import { oneDark } from '@codemirror/theme-one-dark';
import { freebuttonAutocomplete } from './autocomplete';

let editorView: EditorView | null = null;

// Example scripts for FreeButton
export const examples: Record<string, string> = {
    blink: `// LED Blinker
function blink() {
    led.setColor(0, 255, 0, 0);  // Red
    setTimeout(function() {
        led.off(0);
        setTimeout(blink, 500);
    }, 500);
}

blink();`,
    sensor: `// Read sensor values
var temp = sensor.getValue(0);
var humidity = sensor.getValue(1);

print("Temperature:", temp, "°C");
print("Humidity:", humidity, "%");`,
    button: `// Button click handler
button.onClick(0, function() {
    print("Button 0 clicked!");
    led.setColor(0, 0, 255, 0);  // Green
});

button.onLongPress(0, function() {
    print("Button 0 long pressed!");
    led.setColor(0, 255, 0, 0);  // Red
});`,
    mqtt: `// Publish to MQTT
var temp = sensor.getValue(0);
var humidity = sensor.getValue(1);

mqtt.publish("home/sensors/temperature", temp.toString());
mqtt.publish("home/sensors/humidity", humidity.toString());

print("Published sensor data to MQTT");`
};

/**
 * Initialize the CodeMirror editor
 * @param parent - The parent element to attach the editor to
 * @returns The initialized editor instance
 */
export function initEditor(parent: HTMLElement): EditorView {
    editorView = new EditorView({
        extensions: [
            basicSetup,
            javascript(),
            oneDark,
            EditorView.lineWrapping,
            freebuttonAutocomplete
        ],
        parent: parent
    });

    return editorView;
}

/**
 * Get the current code from the editor
 * @returns The editor content
 */
export function getCode(): string {
    if (!editorView) {
        throw new Error('Editor not initialized');
    }
    return editorView.state.doc.toString();
}

/**
 * Set the editor content
 * @param newContent - The new content to set
 */
export function setCode(newContent: string): void {
    if (!editorView) {
        throw new Error('Editor not initialized');
    }
    editorView.dispatch({
        changes: {
            from: 0,
            to: editorView.state.doc.length,
            insert: newContent
        }
    });
}

/**
 * Clear the editor content
 */
export function clearEditor(): void {
    setCode('');
}

/**
 * Load an example script
 * @param name - The name of the example to load
 * @returns True if example was loaded, false otherwise
 */
export function loadExample(name: string): boolean {
    if (examples[name]) {
        setCode(examples[name]);
        return true;
    }
    return false;
}

/**
 * Get the editor instance
 * @returns The editor instance
 */
export function getEditorView(): EditorView | null {
    return editorView;
}
