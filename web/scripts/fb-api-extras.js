/**
 * fb API details that stdlib.json (tools/js/generate_stdlib_json.cpp) does not
 * carry yet: config.json names for buttons and display items, the lists
 * (rollers) and images on the main display, and the styles.
 *
 *   - fb.button(name): the button's existing side object (FbSide / FbMainSide)
 *   - fb.main.display.remove/get/setLabel/setValue/setUnit/setPosition/setPage
 *     take an item id or its config.json name
 *   - fb.main.list (FbMainList): next/prev/select/getSelected/count/setItems
 *     on a config.json "lists" entry by name or index
 *   - icons (config.json "icons", rasterized by the IDE):
 *     fb.main.display.setIcon(idOrName, icon), <side>.display.setIcon(icon)
 *     (FbSmallDisplay: a button's display), and fb.main.image (FbMainImage):
 *     setIcon(name, icon) / setSrc(name, key) on a config.json "images" entry
 *   - styles (config.json "styles"; docs/STYLE.md): setStyle(styles),
 *     addStyle(style) and removeStyle(style) on a button's display and, with
 *     the item first, on fb.main.display; fb.main.list.setStyle(nameOrIndex,
 *     styles); fb.style.set(style, property, value) (FbStyle)
 *
 * Applied in memory by generate-types.js and generate-autocomplete.js. Each
 * change is skipped when stdlib.json already has it, so this becomes a no-op
 * once the C generator emits them.
 */

const STYLES_ARG = {
    name: 'styles',
    type: 'string',
    description: 'Up to four style names of config.json "styles", separated by spaces; a later name wins where two set the same property. "" = the styles config.json gives the item. Not a string, or "button" / "displayitem" / "list": TypeError; an unknown style: ReferenceError; a fifth name: RangeError',
};

const STYLE_ARG = {
    name: 'style',
    type: 'string',
    description: 'One style name of config.json "styles". Not a string, "", or "button" / "displayitem" / "list": TypeError; an unknown style: ReferenceError',
};

const ITEM_ID = { name: 'id', type: 'number', description: 'Item id' };

const DISPLAY_NAME_METHODS = {
    remove: 'Remove a display item (by id or config.json name)',
    get: 'Get a display item by id or config.json name: id, x, y, width, boxType, style (its styles now, names joined by a space), alignment, label, unit, round, page; null when there is none',
    setLabel: 'Set item label (item by id or config.json name)',
    setValue: 'Set item value (item by id or config.json name)',
    setUnit: 'Set item unit (item by id or config.json name)',
    setPosition: 'Set item position (item by id or config.json name)',
    setPage: 'Set item page, 0 = every page (item by id or config.json name)',
    setIcon: 'Show an icon from config.json "icons" before the value ("" hides it; item by id or config.json name); false when there is no such item',
    setStyle: 'Give the item a list of styles (item by id or config.json name): the value takes their main blocks, the caption their "title" blocks, the unit their "unit" blocks, the icon their "icon" colour. "" gives it the styles of config.json again. false when there is no such item',
    addStyle: 'Add one style as the last of the item\'s styles, so that it wins (item by id or config.json name); a style the item has moves to the end. A fifth style: RangeError, nothing changes. false when there is no such item',
    removeStyle: 'Take one style out of the item\'s styles, also one config.json gave it (item by id or config.json name); a style the item does not have: nothing changes. false when there is no such item',
};

const DISPLAY_STYLE_METHODS = [
    { name: 'setStyle', description: DISPLAY_NAME_METHODS.setStyle, params: [ITEM_ID, STYLES_ARG], returns: 'boolean' },
    { name: 'addStyle', description: DISPLAY_NAME_METHODS.addStyle, params: [ITEM_ID, STYLE_ARG], returns: 'boolean' },
    { name: 'removeStyle', description: DISPLAY_NAME_METHODS.removeStyle, params: [ITEM_ID, STYLE_ARG], returns: 'boolean' },
];

const SMALL_DISPLAY_STYLE_METHODS = [
    {
        name: 'setStyle',
        description: 'Give the button a list of styles: the label takes their main blocks, the top label their "title" blocks, the icon their "icon" colour. "" gives it the styles of config.json again. A button without an entry in config.json "buttons": InternalError',
        params: [STYLES_ARG],
        returns: 'void',
    },
    {
        name: 'addStyle',
        description: 'Add one style as the last of the button\'s styles, so that it wins; a style the button has moves to the end. A fifth style: RangeError, nothing changes',
        params: [STYLE_ARG],
        returns: 'void',
    },
    {
        name: 'removeStyle',
        description: 'Take one style out of the button\'s styles, also one config.json gave it; a style the button does not have: nothing changes',
        params: [STYLE_ARG],
        returns: 'void',
    },
];

const FB_STYLE_PROP = {
    name: 'style',
    description: 'The styles of config.json "styles", and "button", "displayitem" and "list": the looks of the kinds',
    type: 'FbStyle',
};

const FB_STYLE = {
    methods: [
        {
            name: 'set',
            description: 'Set one value of a style: every button, display item and list that uses the style is drawn again with it. Of "button", "displayitem" and "list" too: every item of the kind. A property the style does not set in config.json is added. Not saved: a script restart brings the values of config.json back and takes added properties away. Each call waits for the display task (up to a whole redraw): change one property per event, or switch styles with setStyle / addStyle. No room in the LVGL memory for a property more: InternalError (out of memory), nothing changes',
            params: [
                { name: 'style', type: 'string', description: 'A style of config.json "styles", or "button" / "displayitem" / "list" (unknown: ReferenceError)' },
                { name: 'property', type: 'string', description: 'A property: "color", "background-color", "text-align", "letter-spacing", "text-decoration" or "opacity", or "title.", "unit." or "selected." and one ("title.color"), or "icon.color". The block must exist: a style of "styles" has the blocks config.json gives it, a kind has its parts. "font-family", "font-size", "line-height", a block the style does not have or an unknown name: TypeError' },
                { name: 'value', type: 'string | number', description: 'The new value, as in config.json: "#rrggbb", 0..1, "left" / "center" / "right", ... (a value that is not valid: TypeError)' },
            ],
            returns: 'void',
        },
    ],
};

const ICON_ARG = {
    name: 'icon',
    type: 'string',
    description: 'Icon name from config.json "icons" ([A-Za-z0-9_-], 1..31; else TypeError); "" hides the icon',
};

const DISPLAY_SET_ICON = {
    name: 'setIcon',
    description: DISPLAY_NAME_METHODS.setIcon,
    params: [ITEM_ID, ICON_ARG],
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

const FB_BUTTON = {
    name: 'button',
    description: 'Button by its "name" in config.json: the same object as fb.main.left/right (FbMainSide) or fb.bars[i].left/right (FbSide). Throws ReferenceError for an unknown name.',
    params: [{ name: 'name', type: 'string', description: 'Button name from config.json' }],
    returns: 'FbSide | FbMainSide',
};

const LIST_ARG = {
    name: 'nameOrIndex',
    type: 'string | number',
    description: 'List "name" in config.json, or its index in "lists" (unknown name: ReferenceError)',
};

const LIST_SET_STYLE = {
    name: 'setStyle',
    description: 'Give the list a list of styles: the rows take their main blocks (color, font, line-height, background-color of the whole roller), the selected row their "selected" blocks. "" gives it the styles of config.json again. A list has no addStyle / removeStyle',
    params: [LIST_ARG, STYLES_ARG],
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
        LIST_SET_STYLE,
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
    for (const method of DISPLAY_STYLE_METHODS) addMethod(display, method);
    const small = fb.types?.FbSmallDisplay;
    addMethod(small, SMALL_DISPLAY_SET_ICON);
    for (const method of SMALL_DISPLAY_STYLE_METHODS) addMethod(small, method);

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
        addMethod(fb.types.FbMainList, LIST_SET_STYLE);
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
        if (!fb.root.methods.some(m => m.name === FB_BUTTON.name)) fb.root.methods.unshift(FB_BUTTON);
    }
    return stdlib;
}
