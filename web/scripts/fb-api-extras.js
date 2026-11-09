/**
 * fb API details that stdlib.json (tools/js/generate_stdlib_json.cpp) does not
 * carry yet: config.json names for keys and display items.
 *
 *   - fb.key(name): the key's existing side object (FbSide / FbMainSide)
 *   - fb.main.display.remove/get/setLabel/setValue/setUnit/setPosition/setPage
 *     take an item id or its config.json name
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

    if (fb.root) {
        fb.root.methods = fb.root.methods || [];
        if (!fb.root.methods.some(m => m.name === FB_KEY.name)) fb.root.methods.unshift(FB_KEY);
    }
    return stdlib;
}
