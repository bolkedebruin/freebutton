/**
 * FreeButton API Autocomplete
 * Provides IntelliSense-style autocomplete for FreeButton stdlib APIs
 */

import { autocompletion, snippetCompletion, CompletionContext, CompletionResult } from '@codemirror/autocomplete';
import { syntaxTree } from '@codemirror/language';
import type { FreebuttonAPIDefinition } from './types';
import { freebuttonAPI, globalCompletions } from './autocomplete-data';
import { DISPLAY_NAME_METHODS, IMAGE_NAME_METHODS, LIST_NAME_METHODS, type ConfigNames } from './config-model';
import { getConfigNames } from './device-config';
import { styleKind, styleListNames } from './style-config';

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
        // If we found an object name but it's not a known API, return empty.
        // An own property only: "constructor" and "toString" are members of
        // every object, and no API.
        if (!Object.prototype.hasOwnProperty.call(freebuttonAPI, objectName)) {
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

// Cursor inside the string literal first argument of fb.button("..., of
// fb.main.display.setValue("... etc. or of fb.main.list.next("... etc.;
// group 1 = the name typed so far.
const BUTTON_NAME_ARG = /\bfb\s*\.\s*button\s*\(\s*["']([A-Za-z0-9_]*)$/;
const DISPLAY_NAME_ARG = new RegExp(
    `\\bfb\\s*\\.\\s*main\\s*\\.\\s*display\\s*\\.\\s*(?:${DISPLAY_NAME_METHODS.join('|')})\\s*\\(\\s*["']([A-Za-z0-9_]*)$`
);

const LIST_NAME_ARG = new RegExp(
    `\\bfb\\s*\\.\\s*main\\s*\\.\\s*list\\s*\\.\\s*(?:${LIST_NAME_METHODS.join('|')})\\s*\\(\\s*["']([A-Za-z0-9_]*)$`
);

const IMAGE_NAME_ARG = new RegExp(
    `\\bfb\\s*\\.\\s*main\\s*\\.\\s*image\\s*\\.\\s*(?:${IMAGE_NAME_METHODS.join('|')})\\s*\\(\\s*["']([A-Za-z0-9_]*)$`
);

// Icon name: second argument of fb.main.display.setIcon / fb.main.image.setIcon
// (first argument a simple literal or identifier), or the only argument of a
// button display's setIcon (fb.main.left.display.setIcon("...).
const ICON_NAME_ARG = new RegExp(
    `(?:\\bfb\\s*\\.\\s*main\\s*\\.\\s*(?:display|image)\\s*\\.\\s*setIcon\\s*\\(\\s*(?:"[^"]*"|'[^']*'|[A-Za-z0-9_$.]+)\\s*,`
    + `|(?<!\\bmain\\s*)\\.\\s*display\\s*\\.\\s*setIcon\\s*\\()\\s*["']([A-Za-z0-9_-]*)$`
);

// The styles of an item, a list of names separated by spaces: the second
// argument of fb.main.display.setStyle and fb.main.list.setStyle, the first
// of a button display's setStyle. Group 1 = the names before the one being
// typed, group 2 = the name typed so far.
const ITEM_STYLES_ARG = new RegExp(
    `(?:\\bfb\\s*\\.\\s*main\\s*\\.\\s*(?:display|list)\\s*\\.\\s*setStyle\\s*\\(\\s*(?:"[^"]*"|'[^']*'|[A-Za-z0-9_$.]+)\\s*,`
    + `|(?<!\\bmain\\s*)\\.\\s*display\\s*\\.\\s*setStyle\\s*\\()\\s*["']((?:[A-Za-z0-9_]+ +)*)([A-Za-z0-9_]*)$`
);

// The same in the fifth argument of fb.main.display.create (after x, y, width, boxType).
const CREATE_STYLES_ARG = /\bfb\s*\.\s*main\s*\.\s*display\s*\.\s*create\s*\(\s*(?:[^,()"']+,\s*){4}["']((?:[A-Za-z0-9_]+ +)*)([A-Za-z0-9_]*)$/;

// One style: the second argument of fb.main.display.addStyle / removeStyle,
// the first of a button display's addStyle / removeStyle. These take one
// name, so nothing is offered after a first name and a space. Group 1 = the
// name typed so far.
const ITEM_STYLE_ARG = new RegExp(
    `(?:\\bfb\\s*\\.\\s*main\\s*\\.\\s*display\\s*\\.\\s*(?:add|remove)Style\\s*\\(\\s*(?:"[^"]*"|'[^']*'|[A-Za-z0-9_$.]+)\\s*,`
    + `|(?<!\\bmain\\s*)\\.\\s*display\\s*\\.\\s*(?:add|remove)Style\\s*\\()\\s*["']([A-Za-z0-9_]*)$`
);

// Any style, "button", "displayitem" and "list" included: the first argument of fb.style.set.
const STYLE_NAME_ARG = /\bfb\s*\.\s*style\s*\.\s*set\s*\(\s*["']([A-Za-z0-9_]*)$/;

/** The dotted properties fb.style.set can set for a style, [] for a name that is none (own properties only: "constructor" is no style). */
function styleProperties(names: ConfigNames | null, style: string): string[] {
    const styles = names?.styles;
    return styles && Object.prototype.hasOwnProperty.call(styles, style) ? styles[style] ?? [] : [];
}

// Property of a style: the second argument of fb.style.set("style", "...
// (group 1 = the style, group 2 = the property typed so far).
const STYLE_PROPERTY_ARG = /\bfb\s*\.\s*style\s*\.\s*set\s*\(\s*["']([A-Za-z0-9_]*)["']\s*,\s*["']([a-z.-]*)$/;

type NameKind = Exclude<keyof ConfigNames, 'fontIds' | 'styles' | 'fonts'>;

const NAME_ARGS: [RegExp, NameKind | 'styles' | 'itemStyles', string][] = [
    [STYLE_NAME_ARG, 'styles', 'style (config.json)'],
    [ITEM_STYLE_ARG, 'itemStyles', 'style (config.json)'],
    [ICON_NAME_ARG, 'icons', 'icon (config.json)'],
    [BUTTON_NAME_ARG, 'keys', 'button (config.json)'],
    [DISPLAY_NAME_ARG, 'displayItems', 'display item (config.json)'],
    [LIST_NAME_ARG, 'lists', 'list (config.json)'],
    [IMAGE_NAME_ARG, 'images', 'image (config.json)'],
];

/**
 * Button / display item / list names from config.json inside the name
 * argument of fb.button("..."), fb.main.display.setValue("...", ...) etc.
 * and fb.main.list.next("...") etc.; icon and style names in the arguments
 * that take one (an item cannot name "button", "displayitem" or "list";
 * fb.style.set can; setStyle and display.create take a list and are not
 * offered a name the list has; addStyle and removeStyle take one name); the
 * properties fb.style.set can set in its second argument. Returns null when the cursor is not in such an argument; no
 * options without a config. Exported for testing.
 */
export function configNameCompletions(
    context: CompletionContext,
    names: ConfigNames | null = getConfigNames()
): CompletionResult | null {
    const before = context.state.doc.sliceString(Math.max(0, context.pos - 200), context.pos);
    const property = before.match(STYLE_PROPERTY_ARG);
    if (property) {
        // What fb.style.set may set: every property but the font, the size and the line
        // height, in each block the style has (config-model ConfigNames.styles).
        const settable = styleProperties(names, property[1]!);
        return {
            from: context.pos - property[2]!.length,
            options: settable.map(name => ({ label: name, type: 'property', detail: `property of style "${property[1]}"` })),
            validFor: /^[a-z.-]*$/
        };
    }
    // A list of styles: the styles an item can name, without those the list has already
    // (a name given twice counts once).
    const list = before.match(ITEM_STYLES_ARG) ?? before.match(CREATE_STYLES_ARG);
    if (list) {
        const has = styleListNames(list[1]!);
        const known = Object.keys(names?.styles ?? {}).filter(s => !styleKind(s) && !has.includes(s));
        return {
            from: context.pos - list[2]!.length,
            options: known.map(name => ({ label: name, type: 'constant', detail: 'style (config.json)' })),
            validFor: /^[A-Za-z0-9_-]*$/
        };
    }
    for (const [pattern, kind, detail] of NAME_ARGS) {
        const match = before.match(pattern);
        if (!match) continue;
        const known = kind === 'styles' ? Object.keys(names?.styles ?? {})
            : kind === 'itemStyles' ? Object.keys(names?.styles ?? {}).filter(s => !styleKind(s))
            : names?.[kind] ?? [];
        return {
            from: context.pos - match[1]!.length,
            options: known.map(name => ({ label: name, type: 'constant', detail })),
            validFor: /^[A-Za-z0-9_-]*$/
        };
    }
    return null;
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
