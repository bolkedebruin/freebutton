/**
 * FreeButton API Autocomplete
 * Provides IntelliSense-style autocomplete for FreeButton stdlib APIs
 */

import { autocompletion, snippetCompletion, CompletionContext, CompletionResult } from '@codemirror/autocomplete';
import { syntaxTree } from '@codemirror/language';
import type { FreebuttonAPIDefinition } from './types';
import { freebuttonAPI, globalCompletions } from './autocomplete-data';
import { DISPLAY_NAME_METHODS, type ConfigNames } from './config-model';
import { getConfigNames } from './device-config';

/**
 * FreeButton API definitions and global functions
 * Auto-generated from stdlib sources - see autocomplete-data.ts
 * Run: npm run generate-autocomplete to regenerate
 */

/**
 * Custom completion source for FreeButton API
 * Exported for testing
 */
export function freebuttonCompletions(context: CompletionContext): CompletionResult | null {
    const word = context.matchBefore(/\w*/);

    // Check if we just typed a dot - allow completions even with empty word
    const charBefore = context.state.doc.sliceString(context.pos - 1, context.pos);
    const isDotCompletion = charBefore === '.';

    if (!word || (word.from === word.to && !context.explicit && !isDotCompletion)) {
        return null;
    }

    // Check if we're after a dot by examining the text first (most reliable)
    let objectName: string | null = null;

    if (isDotCompletion) {
        // Just typed a dot - parse backwards to find the object name
        const textBefore = context.state.doc.sliceString(Math.max(0, context.pos - 20), context.pos);
        const match = textBefore.match(/(\w+)\.$/);
        if (match && match[1]) {
            objectName = match[1];
        }
    } else {
        // Try to get object name from syntax tree
        const tree = syntaxTree(context.state);
        const nodeBefore = tree.resolveInner(context.pos, -1);

        if (nodeBefore.name === 'PropertyName') {
            const parent = nodeBefore.parent;
            if (parent && parent.name === 'MemberExpression') {
                objectName = context.state.doc.sliceString(parent.from, nodeBefore.from - 1);
            }
        }
    }

    // Return completions for the specific API if we found an object name
    if (objectName) {
        // If we found an object name but it's not a known API, return empty
        if (!(objectName in freebuttonAPI)) {
            return { from: word.from, options: [], validFor: /^\w*$/ };
        }
        const apiKey = objectName as keyof FreebuttonAPIDefinition;
        const api = freebuttonAPI[apiKey];
        if (!api) return null;

        return {
            from: word.from,
            options: api.methods.map(m => {
                // Strip object prefix from both label and apply
                // e.g., "mqtt.publish(...)" -> "publish(...)"
                const methodName = m.label.split('.')[1] || m.label;
                const applyText = m.apply.replace(/^\w+\./, ''); // Remove "mqtt." prefix

                return snippetCompletion(applyText, {
                    label: methodName,
                    type: m.type,
                    detail: m.detail,
                    info: m.info
                });
            }),
            validFor: /^\w*$/
        };
    }

    // Global completions (object names + global functions)
    const objectCompletions = Object.keys(freebuttonAPI).map(name => ({
        label: name,
        type: 'namespace',
        detail: 'FreeButton API',
        info: `${name.charAt(0).toUpperCase() + name.slice(1)} control and monitoring`
    }));

    // Get the typed text for case-sensitive filtering
    const typedText = context.state.doc.sliceString(word.from, word.to);

    // Filter object completions BEFORE creating the options
    // Only include objects that start with the exact case of typed text
    const filteredObjectCompletions = typedText
        ? objectCompletions.filter(obj => obj.label.startsWith(typedText))
        : objectCompletions;

    // Filter global completions the same way
    const filteredGlobalCompletions = typedText
        ? globalCompletions.filter(g => g.label.startsWith(typedText))
        : globalCompletions;

    // Build final options list from filtered results
    const allOptions = [
        ...filteredObjectCompletions,
        ...filteredGlobalCompletions.map(g =>
            snippetCompletion(g.apply, {
                label: g.label,
                type: g.type,
                detail: g.detail,
                info: g.info
            })
        )
    ];

    return {
        from: word.from,
        options: allOptions,
        validFor: /^\w*$/
    };
}

// Cursor inside the string literal first argument of fb.key("... or of
// fb.main.display.setValue("... etc.; group 1 = the name typed so far.
const KEY_NAME_ARG = /\bfb\s*\.\s*key\s*\(\s*["']([A-Za-z0-9_]*)$/;
const DISPLAY_NAME_ARG = new RegExp(
    `\\bfb\\s*\\.\\s*main\\s*\\.\\s*display\\s*\\.\\s*(?:${DISPLAY_NAME_METHODS.join('|')})\\s*\\(\\s*["']([A-Za-z0-9_]*)$`
);

/**
 * Key / display item names from config.json inside the name argument of
 * fb.key("...") and fb.main.display.setValue("...", ...) etc. Returns null
 * when the cursor is not in such an argument; no options without a config.
 * Exported for testing.
 */
export function configNameCompletions(
    context: CompletionContext,
    names: ConfigNames | null = getConfigNames()
): CompletionResult | null {
    const before = context.state.doc.sliceString(Math.max(0, context.pos - 200), context.pos);
    let match = before.match(KEY_NAME_ARG);
    let list = names?.keys;
    let detail = 'key (config.json)';
    if (!match) {
        match = before.match(DISPLAY_NAME_ARG);
        list = names?.displayItems;
        detail = 'display item (config.json)';
    }
    if (!match) return null;
    return {
        from: context.pos - match[1]!.length,
        options: (list ?? []).map(name => ({ label: name, type: 'constant', detail })),
        validFor: /^[A-Za-z0-9_]*$/
    };
}

/** Config names inside a name argument, else the FreeButton API. */
export function editorCompletions(context: CompletionContext): CompletionResult | null {
    return configNameCompletions(context) ?? freebuttonCompletions(context);
}

/**
 * Create and export the autocomplete extension
 * Using closeOnBlur: false to prevent competing with other autocomplete sources
 */
export const freebuttonAutocomplete = autocompletion({
    override: [editorCompletions],
    activateOnTyping: true,
    maxRenderedOptions: 20,
    defaultKeymap: true,  // Enable Tab to accept completions
    closeOnBlur: false,
    // Set high priority to override JavaScript language autocomplete
    interactionDelay: 0
});
