import blinkRaw from '../../../data/scripts/examples/blink.js?raw';
import sensorRaw from '../../../data/scripts/examples/sensor.js?raw';
import buttonRaw from '../../../data/scripts/examples/button.js?raw';
import displaysRaw from '../../../data/scripts/examples/displays.js?raw';
import mqttSimpleRaw from '../../../data/scripts/examples/mqtt_simple.js?raw';
import mqttDemoRaw from '../../../data/scripts/examples/mqtt_demo.js?raw';

export interface Example {
    id: string;
    title: string;
    description: string;
    code: string;
}

function parseExample(id: string, raw: string): Example {
    const lines = raw.split('\n');
    let title = id;
    let description = '';

    for (let i = 0; i < lines.length; i++) {
        const match = lines[i].match(/^\/\/\s*@(\w+)\s+(.*)/);
        if (match) {
            if (match[1] === 'title') title = match[2].trim();
            if (match[1] === 'description') description = match[2].trim();
        } else {
            break;
        }
    }

    return { id, title, description, code: raw };
}

export const examples: Example[] = [
    parseExample('blink', blinkRaw),
    parseExample('sensor', sensorRaw),
    parseExample('button', buttonRaw),
    parseExample('displays', displaysRaw),
    parseExample('mqtt_simple', mqttSimpleRaw),
    parseExample('mqtt_demo', mqttDemoRaw),
];

export const exampleMap: Record<string, Example> = Object.fromEntries(
    examples.map(e => [e.id, e])
);
