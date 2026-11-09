# Names for display items and keys

A display item or a key in `config.json` can have a `"name"`. A script can
then use that name instead of the number:

```json
"mqttdisplays": [ { "displayitemid": "8", "name": "volume", "x": 200, "y": 150, "page": 1 } ],
"buttons":      [ { "id": 2, "position": 3, "name": "sceneUp", "label": "▲" } ]
```

```js
fb.main.display.setValue("volume", "Vol: 15%");     // same as setValue(8, ...)
fb.key("sceneUp").button.onClick(function () { ... });
fb.key("sceneUp").display.setLabel("Up");
```

## Rules

- A name has 1 to 24 characters: letters, digits and `_`. The first
  character is not a digit.
- Display item names are unique among the display items. Key names are
  unique among the keys. A key and a display item can have the same name.
- On the device, a name that does not obey these rules is ignored, and the
  log shows a warning. If two items have the same name, the first one keeps
  it. The config still loads. The IDE shows these errors when you edit
  `config.json`.
- The name of a key refers to its `position` (if there is no `position`,
  its `id`).

## Script API

| Call | Result |
| --- | --- |
| `fb.main.display.setValue(idOrName, value)` | Also `remove`, `get`, `setLabel`, `setUnit`, `setPosition`, `setPage`. The first argument is a display item id (number) or a name (string). |
| `fb.key(name)` | The key object: the same object as `fb.main.left` / `fb.main.right`, or `fb.bars[i].left` / `.right` (`button`, `display`, and on a bar also `led` and `backLed`). |

- An unknown name causes a `ReferenceError` (`unknown display item "x"`,
  `unknown key "x"`). An unknown number id still gives `false`, as before.
- `fb.key()` with an argument that is not a string causes a `TypeError`.
  If the key's connector has nothing connected, `fb.key()` causes a
  `ReferenceError`.
- Numbers continue to work. Use numbers where you calculate the id, for
  example `setValue(firstRow + i, ...)`.

## Memory

- The names stay in the firmware's general heap, in one packed table
  (`src/components/NameTable.h`). Each name uses 6 bytes plus its length.
  An element without a name uses no memory.
- The names are not in the JS heap. The string in the script stays in the
  bytecode, which the device reads from flash. The binding looks up the name
  in the table and copies nothing.
- `fb.key` itself uses one function object in the JS heap (about 16 B).
- Each `fb.key()` call reads the `fb` tree with short-lived property-name
  strings. If you use a key often, call `fb.key()` once and keep the result
  in a local `var`.
- `GET /config` writes the names back, so a config saved from the device
  keeps them.

The device reads the table only after `config.json` loads at boot, and the
table does not change after that. If a script removes a display item, the
item's name stays in the table. The name then refers to an id that does not
exist, and `setValue` returns `false`.
