# Names in config.json

A display item, a button or a list in `config.json` can have a `"name"`. A
script can then use that name instead of the number:

```json
{
  "displayitems": [ { "displayitemid": "8", "name": "volume", "x": 200, "y": 150, "page": 1 } ],
  "buttons":      [ { "position": 3, "name": "sceneUp", "label": "▲" } ]
}
```

```js
fb.main.display.setValue("volume", "Vol: 15%");     // same as setValue(8, ...)
fb.button("sceneUp").button.onClick(function () { ... });
fb.button("sceneUp").display.setLabel("Up");
```

Styles and fonts have names too. They follow the same rule
([Style names and font names](#style-names-and-font-names)).

## Rules

- A name has 1 to 24 characters: letters, digits and `_`. The first
  character is not a digit. As a pattern: `[A-Za-z_][A-Za-z0-9_]{0,23}`.
- Names are case sensitive.
- Display item names are unique among the display items. Button names are
  unique among the buttons. A button and a display item can have the same
  name.
- On the device, a name that does not obey these rules is ignored, and the
  log shows a warning. If two items have the same name, the first one keeps
  it. The config still loads. The IDE shows these errors when you edit
  `config.json`.
- The name of a button refers to its `position`. A button without
  `position` is not shown.

## Style names and font names

- **Styles.** The keys of the top-level `styles` object are style names
  (`docs/STYLE.md`). They follow the rule above. A config defines at most
  32 styles.
- **Reserved style names.** `button`, `displayitem` and `list` are the
  styles of the three kinds. A config can define them. An item cannot name
  them in its `"style"` field, and a script cannot give them to an item.
- **Fonts.** `fonts` is a top-level array. The `name` of each entry
  follows the rule above. `montserrat` and `montserrat_semibold` are the
  names of the two system fonts.
- Styles, fonts, display items, buttons and lists each have their own
  names. A style and a display item can have the same name.

## Script API

| Call | Result |
| --- | --- |
| `fb.main.display.setValue(idOrName, value)` | Also `remove`, `get`, `setLabel`, `setUnit`, `setIcon`, `setStyle`, `addStyle`, `removeStyle`, `setPosition`, `setPage`. The first argument is a display item id (number) or a name (string). |
| `fb.button(name)` | The button's object: the same object as `fb.main.left` / `fb.main.right`, or `fb.bars[i].left` / `.right` (`button`, `display`, and on a bar also `led` and `backLed`). |

- An unknown name causes a `ReferenceError` (`unknown display item "x"`,
  `unknown button "x"`). An unknown number id still gives `false`, as
  before.
- `fb.button()` with an argument that is not a string causes a `TypeError`
  (`fb.button() requires a button name (string)`). If the button's
  connector has nothing connected, `fb.button()` causes a `ReferenceError`.
- Numbers continue to work. Use numbers where you calculate the id, for
  example `setValue(firstRow + i, ...)`.

## Memory

- The names stay in the firmware's general heap, in one packed table
  (`src/components/NameTable.h`). Each name uses 6 bytes plus its length.
  An element without a name uses no memory.
- Style names are in the table of the styles: 1 byte plus its length for
  each name (`docs/STYLE.md`, Memory).
- The names are not in the JS heap. The string in the script stays in the
  bytecode, which the device reads from flash. The binding looks up the name
  in the table and copies nothing.
- `fb.button` itself uses one function object in the JS heap (about 16 B).
- Each `fb.button()` call reads the `fb` tree with short-lived
  property-name strings. If you use a button often, call `fb.button()` once
  and keep the result in a local `var`.
- `GET /config` writes the names back, so a config saved from the device
  keeps them.

The device reads the table only after `config.json` loads at boot, and the
table does not change after that. If a script removes a display item, the
item's name stays in the table. The name then refers to an id that does not
exist, and `setValue` returns `false`. A config item removed this way is kept
in memory and comes back when the script engine restarts
(`docs/SCRIPT-RESTART.md`).
