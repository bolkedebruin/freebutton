/**
 * fb API details that stdlib.json (tools/js/generate_stdlib_json.cpp) does not
 * carry yet: config.json names for keys and display items, and the lists
 * (rollers) on the main display.
 *
 *   - fb.key(name): the key's existing side object (FbSide / FbMainSide)
 *   - fb.main.display.remove/get/setLabel/setValue/setUnit/setPosition/setPage
 *     take an item id or its config.json name
 *   - fb.main.list (FbMainList): next/prev/select/getSelected/count/setItems
 *     on a config.json "lists" entry by name or index
 *   - icons (config.json "icons", rasterized by the IDE):
 *     fb.main.display.setIcon(idOrName, icon), <side>.display.setIcon(icon)
 *     (FbSmallDisplay: a key's display), and fb.main.image (FbMainImage):
 *     setIcon(name, icon) / setSrc(name, key) on a config.json "images" entry
 *   - fonts (config.json "style.fonts"): fb.main.display.setFont(idOrName, font,
 *     size), <side>.display.setFont(font, size) / setTopFont(font, size), and
 *     fb.main.list.setFont(nameOrIndex, font, size)
 *   - named styles (config.json "style.styles"; the device has these only when
 *     the config defines named styles): fb.main.display.setStyle / setLabelStyle
 *     (idOrName, style), <side>.display.setStyle / setTopStyle(style),
 *     fb.main.list.setStyle / setFocusStyle(nameOrIndex, style) and
 *     fb.style.set(style, property, value) (FbStyle)
 *
 * Applied in memory by generate-types.js and generate-autocomplete.js. Each
 * change is skipped when stdlib.json already has it, so this becomes a no-op
 * once the C generator emits them.
 */

const NAMED_ONLY = 'Only on a device whose config.json has named styles.';

const DISPLAY_NAME_METHODS = {
    remove: 'Remove a display item (by id or config.json name)',
    get: 'Get a display item by id or config.json name',
    setLabel: 'Set item label (item by id or config.json name)',
    setValue: 'Set item value (item by id or config.json name)',
    setUnit: 'Set item unit (item by id or config.json name)',
    setPosition: 'Set item position (item by id or config.json name)',
    setPage: 'Set item page, 0 = every page (item by id or config.json name)',
    setIcon: 'Show an icon from config.json "icons" before the value ("" hides it; item by id or config.json name); false when there is no such item',
    setFont: 'Set the font of the value and unit (item by id or config.json name; the caption keeps its own; font "" restores config.json); false when there is no such item',
    setStyle: 'Give the value and unit a named style of config.json "style.styles" (item by id or config.json name; "" takes the named style away; the item\'s own fields still win); false when there is no such item. ' + NAMED_ONLY,
    setLabelStyle: 'Give the caption a named style of config.json "style.styles" (item by id or config.json name; "" takes the named style away); false when there is no such item. ' + NAMED_ONLY,
};

const TEXT_STYLE_ARG = {
    name: 'style',
    type: 'string',
    description: 'Name of a style in config.json "style.styles" (unknown: ReferenceError; not a string: TypeError); a style that sets bg_color or bg_opa is for lists only (TypeError); "" = no named style (to get the configured one back, name it)',
};

const LIST_STYLE_ARG = {
    name: 'style',
    type: 'string',
    description: 'Name of a style in config.json "style.styles" (unknown: ReferenceError; not a string: TypeError); "" = no named style (to get the configured one back, name it)',
};

const DISPLAY_SET_STYLE = {
    name: 'setStyle',
    description: DISPLAY_NAME_METHODS.setStyle,
    params: [{ name: 'id', type: 'number', description: 'Item id' }, TEXT_STYLE_ARG],
    returns: 'boolean',
};

const DISPLAY_SET_LABEL_STYLE = {
    name: 'setLabelStyle',
    description: DISPLAY_NAME_METHODS.setLabelStyle,
    params: [{ name: 'id', type: 'number', description: 'Item id' }, TEXT_STYLE_ARG],
    returns: 'boolean',
};

const SMALL_DISPLAY_SET_STYLE = {
    name: 'setStyle',
    description: 'Give the bottom label a named style of config.json "style.styles" ("" takes the named style away; the key\'s own "font" / "fontsize" still win). ' + NAMED_ONLY,
    params: [TEXT_STYLE_ARG],
    returns: 'void',
};

const SMALL_DISPLAY_SET_TOP_STYLE = {
    name: 'setTopStyle',
    description: 'Give the top label a named style of config.json "style.styles" ("" takes the named style away; the key\'s own "topfont" / "topfontsize" still win). ' + NAMED_ONLY,
    params: [TEXT_STYLE_ARG],
    returns: 'void',
};

const FB_STYLE_PROP = {
    name: 'style',
    description: 'The named styles of config.json "style.styles" (present only when config.json has named styles)',
    type: 'FbStyle',
};

const FB_STYLE = {
    methods: [
        {
            name: 'set',
            description: 'Change one value of a named style: every key, display item and list that uses the style is redrawn with it. Not saved: a script restart brings the values of config.json back.',
            params: [
                { name: 'style', type: 'string', description: 'Name of a style in config.json "style.styles" (unknown: ReferenceError)' },
                { name: 'property', type: 'string', description: 'A property the style sets in config.json, e.g. "text_color" (one it does not set, "text_font", "text_size" or an unknown name: TypeError)' },
                { name: 'value', type: 'string | number', description: 'The new value, as in config.json: "#rrggbb", 0..255, "left" / "center" / "right", ... (a bad value: TypeError)' },
            ],
            returns: 'void',
        },
    ],
};

const FONT_ARG = {
    name: 'font',
    type: 'string | number',
    description: 'Font "name" or "id" from config.json "style.fonts", or a system font: "montserrat", "montserrat_semibold" (unknown: ReferenceError); "" = back to the font and size of config.json (the item\'s fields, else its "style")',
};

const FONT_SIZE_ARG = {
    name: 'size',
    type: 'number',
    description: '1..5 = the classic sizes (14/24/32/48/72 px), 6..72 = pixels (else TypeError); ignored, and may be left out, for font ""',
};

const DISPLAY_SET_FONT = {
    name: 'setFont',
    description: DISPLAY_NAME_METHODS.setFont,
    params: [{ name: 'id', type: 'number', description: 'Item id' }, FONT_ARG, FONT_SIZE_ARG],
    returns: 'boolean',
};

const SMALL_DISPLAY_SET_FONT = {
    name: 'setFont',
    description: 'Set the font of the bottom label (default: the style\'s key label font, config.json "style.key.label"; font "" restores config.json)',
    params: [FONT_ARG, FONT_SIZE_ARG],
    returns: 'void',
};

const SMALL_DISPLAY_SET_TOP_FONT = {
    name: 'setTopFont',
    description: 'Set the font of the top label (default: the style\'s key top label font, config.json "style.key.toplabel"; font "" restores config.json)',
    params: [FONT_ARG, FONT_SIZE_ARG],
    returns: 'void',
};

const ICON_ARG = {
    name: 'icon',
    type: 'string',
    description: 'Icon name from config.json "icons" ([A-Za-z0-9_-], 1..31; else TypeError); "" hides the icon',
};

const DISPLAY_SET_ICON = {
    name: 'setIcon',
    description: DISPLAY_NAME_METHODS.setIcon,
    params: [{ name: 'id', type: 'number', description: 'Item id' }, ICON_ARG],
    returns: 'boolean',
};

const SMALL_DISPLAY_SET_ICON = {
    name: 'setIcon',
    description: 'Show an icon from config.json "icons" instead of the bottom label ("" shows the label again)',
    params: [ICON_ARG],
    returns: 'void',
};

const IMAGE_ARG = {
    name: 'nameOrIndex',
    type: 'string | number',
    description: 'Image "name" in config.json "images" (unknown name: ReferenceError), or its index there (out of range: RangeError)',
};

const FB_MAIN_IMAGE_PROP = {
    name: 'image',
    description: 'Images on the main display, from config.json "images" (present only when config.json has images)',
    type: 'FbMainImage',
};

const FB_MAIN_IMAGE = {
    methods: [
        {
            name: 'setIcon',
            description: 'Show an icon from config.json "icons" at the image\'s width x height ("" clears the image)',
            params: [IMAGE_ARG, ICON_ARG],
            returns: 'void',
        },
        {
            name: 'setSrc',
            description: 'Show any key of the device\'s images bucket (an LVGL .bin raster, e.g. "radar.bin"; "" clears the image)',
            params: [IMAGE_ARG, { name: 'key', type: 'string', description: 'Object key in bucket "images" (at most 96 bytes, else TypeError)' }],
            returns: 'void',
        },
    ],
};

const FB_KEY = {
    name: 'key',
    description: 'Key by its "name" in config.json: the same object as fb.main.left/right (FbMainSide) or fb.bars[i].left/right (FbSide). Throws ReferenceError for an unknown name.',
    params: [{ name: 'name', type: 'string', description: 'Key name from config.json' }],
    returns: 'FbSide | FbMainSide',
};

const LIST_ARG = {
    name: 'nameOrIndex',
    type: 'string | number',
    description: 'List "name" in config.json, or its index in "lists" (unknown name: ReferenceError)',
};

const LIST_SET_STYLE = {
    name: 'setStyle',
    description: 'Give the list a named style of config.json "style.styles" ("" takes the named style away; the list\'s own "font" / "fontsize" / "color" still win). ' + NAMED_ONLY,
    params: [LIST_ARG, LIST_STYLE_ARG],
    returns: 'void',
};

const LIST_SET_FOCUS_STYLE = {
    name: 'setFocusStyle',
    description: 'Give the selected row a named style of config.json "style.styles", which may set bg_color / bg_opa ("" takes the named style away; the list\'s own "focuscolor" / "focustextcolor" still win). ' + NAMED_ONLY,
    params: [LIST_ARG, LIST_STYLE_ARG],
    returns: 'void',
};

const FB_MAIN_LIST_PROP = {
    name: 'list',
    description: 'Lists (rollers) on the main display, from config.json "lists"',
    type: 'FbMainList',
};

const FB_MAIN_LIST = {
    methods: [
        {
            name: 'next',
            description: 'Select the next item (one down; stops at the last, no wrap); returns the new index',
            params: [LIST_ARG],
            returns: 'number',
        },
        {
            name: 'prev',
            description: 'Select the previous item (one up; stops at the first, no wrap); returns the new index',
            params: [LIST_ARG],
            returns: 'number',
        },
        {
            name: 'select',
            description: 'Select an item by index; false when the index is out of range',
            params: [LIST_ARG, { name: 'index', type: 'number', description: '0-based item index' }],
            returns: 'boolean',
        },
        {
            name: 'getSelected',
            description: 'Index of the selected item; -1 when the list is empty',
            params: [LIST_ARG],
            returns: 'number',
        },
        {
            name: 'count',
            description: 'Number of items in the list',
            params: [LIST_ARG],
            returns: 'number',
        },
        {
            name: 'setItems',
            description: 'Replace the items (no newlines in an item); the selection is clamped to the new items',
            params: [LIST_ARG, { name: 'items', type: 'string[]', description: 'New item texts' }],
            returns: 'boolean',
        },
        {
            name: 'setFont',
            description: 'Set the font of the items (default: the style\'s list font, config.json "style.list", at the list\'s "fontsize"; font "" restores config.json)',
            params: [LIST_ARG, FONT_ARG, FONT_SIZE_ARG],
            returns: 'void',
        },
        LIST_SET_STYLE,
        LIST_SET_FOCUS_STYLE,
    ],
};

// Add a method unless the type has one of that name.
function addMethod(type, method) {
    if (!type) return;
    type.methods = type.methods || [];
    if (!type.methods.some(m => m.name === method.name)) type.methods.push(structuredClone(method));
}

export function applyFbExtras(stdlib) {
    const fb = stdlib.fb;
    if (!fb) return stdlib;

    const display = fb.types?.FbMainDisplay;
    addMethod(display, DISPLAY_SET_ICON);
    addMethod(display, DISPLAY_SET_FONT);
    addMethod(display, DISPLAY_SET_STYLE);
    addMethod(display, DISPLAY_SET_LABEL_STYLE);
    const small = fb.types?.FbSmallDisplay;
    addMethod(small, SMALL_DISPLAY_SET_ICON);
    addMethod(small, SMALL_DISPLAY_SET_FONT);
    addMethod(small, SMALL_DISPLAY_SET_TOP_FONT);
    addMethod(small, SMALL_DISPLAY_SET_STYLE);
    addMethod(small, SMALL_DISPLAY_SET_TOP_STYLE);

    for (const method of fb.types?.FbMainDisplay?.methods || []) {
        const description = DISPLAY_NAME_METHODS[method.name];
        const first = method.params?.[0];
        if (!description || !first || first.name !== 'id' || first.type !== 'number') continue;
        method.description = description;
        method.params[0] = {
            name: 'idOrName',
            type: 'number | string',
            description: 'Item id, or its "name" in config.json (unknown name: ReferenceError)',
        };
    }

    fb.types = fb.types || {};
    const main = fb.types.FbMain;
    if (main && !fb.types.FbMainList) {
        fb.types.FbMainList = FB_MAIN_LIST;
        main.properties = main.properties || [];
        if (!main.properties.some(p => p.name === FB_MAIN_LIST_PROP.name)) main.properties.push(FB_MAIN_LIST_PROP);
    } else {
        for (const name of ['setFont', 'setStyle', 'setFocusStyle']) {
            addMethod(fb.types.FbMainList, FB_MAIN_LIST.methods.find(m => m.name === name));
        }
    }
    if (main && !fb.types.FbMainImage) {
        fb.types.FbMainImage = FB_MAIN_IMAGE;
        main.properties = main.properties || [];
        if (!main.properties.some(p => p.name === FB_MAIN_IMAGE_PROP.name)) main.properties.push(FB_MAIN_IMAGE_PROP);
    }

    if (fb.root && !fb.types.FbStyle) {
        fb.types.FbStyle = FB_STYLE;
        fb.root.properties = fb.root.properties || [];
        if (!fb.root.properties.some(p => p.name === FB_STYLE_PROP.name)) fb.root.properties.push(FB_STYLE_PROP);
    }

    if (fb.root) {
        fb.root.methods = fb.root.methods || [];
        if (!fb.root.methods.some(m => m.name === FB_KEY.name)) fb.root.methods.unshift(FB_KEY);
    }
    return stdlib;
}
