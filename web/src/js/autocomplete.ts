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
import { SYSTEM_FONTS } from './font-config';

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

// Cursor inside the string literal first argument of fb.key("..., of
// fb.main.display.setValue("... etc. or of fb.main.list.next("... etc.;
// group 1 = the name typed so far.
const KEY_NAME_ARG = /\bfb\s*\.\s*key\s*\(\s*["']([A-Za-z0-9_]*)$/;
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
// key display's setIcon (fb.main.left.display.setIcon("...).
const ICON_NAME_ARG = new RegExp(
    `(?:\\bfb\\s*\\.\\s*main\\s*\\.\\s*(?:display|image)\\s*\\.\\s*setIcon\\s*\\(\\s*(?:"[^"]*"|'[^']*'|[A-Za-z0-9_$.]+)\\s*,`
    + `|(?<!\\bmain\\s*)\\.\\s*display\\s*\\.\\s*setIcon\\s*\\()\\s*["']([A-Za-z0-9_-]*)$`
);

// Font name: second argument of fb.main.display.setFont / fb.main.list.setFont,
// or the first of a key display's setFont / setTopFont.
const FONT_NAME_ARG = new RegExp(
    `(?:\\bfb\\s*\\.\\s*main\\s*\\.\\s*(?:display|list)\\s*\\.\\s*setFont\\s*\\(\\s*(?:"[^"]*"|'[^']*'|[A-Za-z0-9_$.]+)\\s*,`
    + `|(?<!\\bmain\\s*)\\.\\s*display\\s*\\.\\s*set(?:Top)?Font\\s*\\()\\s*["']([A-Za-z0-9_]*)$`
);

// Named style, any: second argument of fb.main.list.setStyle / setFocusStyle
// and the first of fb.style.set.
const STYLE_NAME_ARG = new RegExp(
    `(?:\\bfb\\s*\\.\\s*main\\s*\\.\\s*list\\s*\\.\\s*set(?:Focus)?Style\\s*\\(\\s*(?:"[^"]*"|'[^']*'|[A-Za-z0-9_$.]+)\\s*,`
    + `|\\bfb\\s*\\.\\s*style\\s*\\.\\s*set\\s*\\()\\s*["']([A-Za-z0-9_]*)$`
);

// Named style for a part without a background: second argument of
// fb.main.display.setStyle / setLabelStyle, the first of a key display's
// setStyle / setTopStyle. A style that sets bg_color / bg_opa is for a list
// only (the device throws TypeError), so it is not offered here.
const TEXT_STYLE_NAME_ARG = new RegExp(
    `(?:\\bfb\\s*\\.\\s*main\\s*\\.\\s*display\\s*\\.\\s*set(?:Label)?Style\\s*\\(\\s*(?:"[^"]*"|'[^']*'|[A-Za-z0-9_$.]+)\\s*,`
    + `|(?<!\\bmain\\s*)\\.\\s*display\\s*\\.\\s*set(?:Top)?Style\\s*\\()\\s*["']([A-Za-z0-9_]*)$`
);

const BG_PROPERTIES = ['bg_color', 'bg_opa'];

/** The properties of a named style, [] for a name that is none (own properties only: "constructor" is no style). */
function styleProperties(names: ConfigNames | null, style: string): string[] {
    const styles = names?.styles;
    return styles && Object.prototype.hasOwnProperty.call(styles, style) ? styles[style] ?? [] : [];
}

// Property of a named style: the second argument of fb.style.set("style", "...
// (group 1 = the style, group 2 = the property typed so far).
const STYLE_PROPERTY_ARG = /\bfb\s*\.\s*style\s*\.\s*set\s*\(\s*["']([A-Za-z0-9_]*)["']\s*,\s*["']([a-z_]*)$/;

type NameKind = Exclude<keyof ConfigNames, 'fontIds' | 'styles'>;

const NAME_ARGS: [RegExp, NameKind | 'styles' | 'textStyles', string][] = [
    [STYLE_NAME_ARG, 'styles', 'named style (config.json)'],
    [TEXT_STYLE_NAME_ARG, 'textStyles', 'named style (config.json)'],
    [FONT_NAME_ARG, 'fonts', 'font (config.json)'],
    [ICON_NAME_ARG, 'icons', 'icon (config.json)'],
    [KEY_NAME_ARG, 'keys', 'key (config.json)'],
    [DISPLAY_NAME_ARG, 'displayItems', 'display item (config.json)'],
    [LIST_NAME_ARG, 'lists', 'list (config.json)'],
    [IMAGE_NAME_ARG, 'images', 'image (config.json)'],
];

/**
 * Key / display item / list names from config.json inside the name argument
 * of fb.key("..."), fb.main.display.setValue("...", ...) etc. and
 * fb.main.list.next("...") etc.; font, icon and named style names in the
 * arguments that take one; the properties a named style sets in the second
 * argument of fb.style.set. Returns null when the cursor is not in such an
 * argument; no options without a config. Exported for testing.
 */
export function configNameCompletions(
    context: CompletionContext,
    names: ConfigNames | null = getConfigNames()
): CompletionResult | null {
    const before = context.state.doc.sliceString(Math.max(0, context.pos - 200), context.pos);
    const property = before.match(STYLE_PROPERTY_ARG);
    if (property) {
        // What fb.style.set may change: the style's own properties, but its font and size.
        const settable = styleProperties(names, property[1]!).filter(p => p !== 'text_font' && p !== 'text_size');
        return {
            from: context.pos - property[2]!.length,
            options: settable.map(name => ({ label: name, type: 'property', detail: `property of style "${property[1]}"` })),
            validFor: /^[a-z_]*$/
        };
    }
    for (const [pattern, kind, detail] of NAME_ARGS) {
        const match = before.match(pattern);
        if (!match) continue;
        const known = kind === 'styles' ? Object.keys(names?.styles ?? {})
            : kind === 'textStyles'
                ? Object.keys(names?.styles ?? {}).filter(s => !styleProperties(names, s).some(p => BG_PROPERTIES.includes(p)))
            // Fonts: the system fonts exist without a "style.fonts" table.
            : names?.[kind] ?? (kind === 'fonts' && names ? SYSTEM_FONTS.map(f => f.name) : []);
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
