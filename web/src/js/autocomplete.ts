/**
 * FreeButton API Autocomplete
 * Provides IntelliSense-style autocomplete for FreeButton stdlib APIs
 */

import { autocompletion, snippetCompletion, CompletionContext, CompletionResult } from '@codemirror/autocomplete';
import { syntaxTree } from '@codemirror/language';
import type { CompletionItem, FreebuttonAPIDefinition } from './types';

/**
 * FreeButton API definitions with documentation
 */
const freebuttonAPI: FreebuttonAPIDefinition = {
    led: {
        methods: [
            {
                label: 'led.on',
                type: 'function',
                detail: '(position: number)',
                info: 'Turn LED white at position',
                apply: 'led.on(${position})'
            },
            {
                label: 'led.off',
                type: 'function',
                detail: '(position: number)',
                info: 'Turn LED off at position',
                apply: 'led.off(${position})'
            },
            {
                label: 'led.setColor',
                type: 'function',
                detail: '(position: number, r: number, g: number, b: number)',
                info: 'Set LED RGB color (0-255 for each component)',
                apply: 'led.setColor(${position}, ${r}, ${g}, ${b})'
            },
            {
                label: 'led.count',
                type: 'function',
                detail: '(): number',
                info: 'Get number of available LEDs',
                apply: 'led.count()'
            }
        ]
    },
    button: {
        methods: [
            {
                label: 'button.setLabel',
                type: 'function',
                detail: '(position: number, text: string)',
                info: 'Update button label text',
                apply: 'button.setLabel(${position}, "${text}")'
            },
            {
                label: 'button.setTopLabel',
                type: 'function',
                detail: '(position: number, text: string)',
                info: 'Update button top label text',
                apply: 'button.setTopLabel(${position}, "${text}")'
            },
            {
                label: 'button.onClick',
                type: 'function',
                detail: '(position: number, callback: function)',
                info: 'Register click event handler',
                apply: 'button.onClick(${position}, function() {\n\t${}\n})'
            },
            {
                label: 'button.onLongPress',
                type: 'function',
                detail: '(position: number, callback: function)',
                info: 'Register long press event handler',
                apply: 'button.onLongPress(${position}, function() {\n\t${}\n})'
            },
            {
                label: 'button.onRelease',
                type: 'function',
                detail: '(position: number, callback: function)',
                info: 'Register release event handler',
                apply: 'button.onRelease(${position}, function() {\n\t${}\n})'
            },
            {
                label: 'button.count',
                type: 'function',
                detail: '(): number',
                info: 'Get number of available buttons',
                apply: 'button.count()'
            }
        ]
    },
    sensor: {
        methods: [
            {
                label: 'sensor.getValue',
                type: 'function',
                detail: '(sensorId: number): number',
                info: 'Read current sensor value',
                apply: 'sensor.getValue(${sensorId})'
            },
            {
                label: 'sensor.getType',
                type: 'function',
                detail: '(sensorId: number): string',
                info: 'Get sensor type ("temperature", "humidity", etc.)',
                apply: 'sensor.getType(${sensorId})'
            },
            {
                label: 'sensor.getInfo',
                type: 'function',
                detail: '(sensorId: number): object',
                info: 'Get sensor information object',
                apply: 'sensor.getInfo(${sensorId})'
            },
            {
                label: 'sensor.getAll',
                type: 'function',
                detail: '(): array',
                info: 'Get array of all sensor information',
                apply: 'sensor.getAll()'
            },
            {
                label: 'sensor.count',
                type: 'function',
                detail: '(): number',
                info: 'Get number of available sensors',
                apply: 'sensor.count()'
            }
        ]
    },
    mqtt: {
        methods: [
            {
                label: 'mqtt.publish',
                type: 'function',
                detail: '(brokerId: number, topic: string, payload: string, qos?: number, retain?: boolean)',
                info: 'Publish message to MQTT topic',
                apply: 'mqtt.publish(${brokerId}, "${topic}", "${payload}")'
            },
            {
                label: 'mqtt.subscribe',
                type: 'function',
                detail: '(brokerId: number, topic: string, callback: function, qos?: number)',
                info: 'Subscribe to MQTT topic',
                apply: 'mqtt.subscribe(${brokerId}, "${topic}", function(topic, message) {\n\t${}\n})'
            },
            {
                label: 'mqtt.unsubscribe',
                type: 'function',
                detail: '(brokerId: number, topic: string)',
                info: 'Unsubscribe from MQTT topic',
                apply: 'mqtt.unsubscribe(${brokerId}, "${topic}")'
            },
            {
                label: 'mqtt.onConnect',
                type: 'function',
                detail: '(brokerId: number, callback: function)',
                info: 'Register broker connect callback',
                apply: 'mqtt.onConnect(${brokerId}, function() {\n\t${}\n})'
            },
            {
                label: 'mqtt.onDisconnect',
                type: 'function',
                detail: '(brokerId: number, callback: function)',
                info: 'Register broker disconnect callback',
                apply: 'mqtt.onDisconnect(${brokerId}, function() {\n\t${}\n})'
            },
            {
                label: 'mqtt.isConnected',
                type: 'function',
                detail: '(brokerId: number): boolean',
                info: 'Check broker connection status',
                apply: 'mqtt.isConnected(${brokerId})'
            },
            {
                label: 'mqtt.getBrokerName',
                type: 'function',
                detail: '(brokerId: number): string',
                info: 'Get broker name',
                apply: 'mqtt.getBrokerName(${brokerId})'
            },
            {
                label: 'mqtt.getBrokerCount',
                type: 'function',
                detail: '(): number',
                info: 'Get number of configured brokers',
                apply: 'mqtt.getBrokerCount()'
            }
        ]
    }
};

/**
 * Global functions and keywords
 */
const globalCompletions: CompletionItem[] = [
    {
        label: 'print',
        type: 'function',
        detail: '(...args: any)',
        info: 'Print to console',
        apply: 'print(${message})'
    },
    {
        label: 'setTimeout',
        type: 'function',
        detail: '(callback: function, delay: number)',
        info: 'Execute callback after delay in milliseconds',
        apply: 'setTimeout(function() {\n\t${}\n}, ${delay})'
    },
    {
        label: 'setInterval',
        type: 'function',
        detail: '(callback: function, interval: number)',
        info: 'Execute callback repeatedly at interval in milliseconds',
        apply: 'setInterval(function() {\n\t${}\n}, ${interval})'
    },
    {
        label: 'clearTimeout',
        type: 'function',
        detail: '(id: number)',
        info: 'Cancel a timeout',
        apply: 'clearTimeout(${id})'
    },
    {
        label: 'clearInterval',
        type: 'function',
        detail: '(id: number)',
        info: 'Cancel an interval',
        apply: 'clearInterval(${id})'
    }
];

/**
 * Custom completion source for FreeButton API
 */
function freebuttonCompletions(context: CompletionContext): CompletionResult | null {
    const word = context.matchBefore(/\w*/);
    if (!word || (word.from === word.to && !context.explicit)) {
        return null;
    }

    const tree = syntaxTree(context.state);
    const nodeBefore = tree.resolveInner(context.pos, -1);

    // Check if we're after a dot (member access)
    if (nodeBefore.name === 'PropertyName') {
        const parent = nodeBefore.parent;
        if (parent && parent.name === 'MemberExpression') {
            // Get the object name
            const object = context.state.doc.sliceString(parent.from, nodeBefore.from - 1);

            // Return completions for the specific API
            if (object in freebuttonAPI) {
                const apiKey = object as keyof FreebuttonAPIDefinition;
                return {
                    from: word.from,
                    options: freebuttonAPI[apiKey].methods.map(m =>
                        snippetCompletion(m.apply, {
                            label: m.label.split('.')[1] || m.label, // Remove object prefix
                            type: m.type,
                            detail: m.detail,
                            info: m.info
                        })
                    ),
                    validFor: /^\w*$/
                };
            }
        }
    }

    // Global completions (object names + global functions)
    const objectCompletions = Object.keys(freebuttonAPI).map(name => ({
        label: name,
        type: 'namespace',
        detail: 'FreeButton API',
        info: `${name.charAt(0).toUpperCase() + name.slice(1)} control and monitoring`
    }));

    return {
        from: word.from,
        options: [
            ...objectCompletions,
            ...globalCompletions.map(g =>
                snippetCompletion(g.apply, {
                    label: g.label,
                    type: g.type,
                    detail: g.detail,
                    info: g.info
                })
            )
        ],
        validFor: /^\w*$/
    };
}

/**
 * Create and export the autocomplete extension
 */
export const freebuttonAutocomplete = autocompletion({
    override: [freebuttonCompletions],
    activateOnTyping: true,
    maxRenderedOptions: 20
});
