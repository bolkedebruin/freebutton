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
 *
 * Applied in memory by generate-types.js and generate-autocomplete.js. Each
 * change is skipped when stdlib.json already has it, so this becomes a no-op
 * once the C generator emits them.
 */

const DISPLAY_NAME_METHODS = {
    remove: 'Remove a display item (by id or config.json name)',
    get: 'Get a display item by id or config.json name',
    setLabel: 'Set item label (item by id or config.json name)',
    setValue: 'Set item value (item by id or config.json name)',
    setUnit: 'Set item unit (item by id or config.json name)',
    setPosition: 'Set item position (item by id or config.json name)',
    setPage: 'Set item page, 0 = every page (item by id or config.json name)',
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
    ],
};

export function applyFbExtras(stdlib) {
    const fb = stdlib.fb;
    if (!fb) return stdlib;

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
    }

    if (fb.root) {
        fb.root.methods = fb.root.methods || [];
        if (!fb.root.methods.some(m => m.name === FB_KEY.name)) fb.root.methods.unshift(FB_KEY);
    }
    return stdlib;
}
