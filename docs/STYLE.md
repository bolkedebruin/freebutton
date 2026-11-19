# Styles: config.json `"styles"` and `"fonts"`

`styles` is a set of named looks. An item (a button, a display item or a
list) lists the styles it wants in one field: `"style": "big night"`. What
none of them sets comes from the style named after the item's kind:
`button`, `displayitem` or `list`.

A style says only what differs. A property that the kind's style does not
set either has the firmware's built-in value
([The reserved styles](#the-reserved-styles)).

```json
{
  "fonts": [
    {"id": 1, "name": "mdi", "src": "icons.ttf"},
    {"name": "montserrat",          "fallback": "mdi"},
    {"name": "montserrat_semibold", "fallback": "mdi"}
  ],
  "styles": {
    "button":      {"font-size": 32, "title": {"font-size": 18}},
    "displayitem": {"font-size": 14},
    "list":        {"font-size": 32, "selected": {"background-color": "#ffffff"}},

    "big":    {"font-size": 48},
    "alarm":  {"color": "#ff0000", "title": {"color": "#ff8080"}, "icon": {"color": "#ff0000"}},
    "night":  {"color": "#606060", "unit": {"color": "#404040"}},
    "picked": {"selected": {"color": "#000000", "background-color": "#00c853"}}
  },
  "buttons": [
    {"position": 1, "name": "windowOpen",  "label": "Open"},
    {"position": 2, "name": "windowClose", "label": "Close", "toplabel": "Window", "style": "alarm"}
  ],
  "displayitems": [
    {"name": "time", "x": 0,  "y": 0,  "width": 50, "page": 2, "style": "big night"},
    {"name": "soc",  "x": 50, "y": 0,  "width": 50, "page": 2, "label": "Battery", "unit": "%", "style": "night"},
    {"name": "co2",  "x": 0,  "y": 40, "width": 50, "page": 1, "label": "CO2", "unit": "ppm", "style": "big alarm"}
  ],
  "lists": [
    {"name": "scenes", "page": 3, "x": 4, "y": 18, "width": 92, "rows": 3,
     "items": ["Bed Time", "Evening"], "style": "picked"}
  ]
}
```

- `windowClose` and `co2` are red. `co2` is also 48 px.
- `time` is 48 px and grey. `soc` is grey, with a darker unit.
- `windowOpen` names no style: it has the look of `button`.
- The selected row of `scenes` is black on green.

There are no styles for a page, no properties on an item itself, and no
style above the three kinds.

## Styles and parts

A style is a block of properties for the main text of an item. It can hold
up to four sub-blocks. Each sub-block is for one other part of an item.

| Block | Button | Display item | List |
|---|---|---|---|
| main (the style itself) | the label | the value | the rows |
| `title` | the top label | the caption (`label`) | not used |
| `unit` | not used | the unit | not used |
| `icon` | the icon | the icon | not used |
| `selected` | not used | not used | the selected row |

- `title`, `unit` and `selected` take the same properties as the main
  block. `icon` takes `color` only: the tint of the icon.
- Any item can use any style. A sub-block that the item's kind does not
  have is ignored.
- A sub-block that is an empty object (`{}`) is the same as no sub-block.
  A style that is an empty object is a style, and counts as one.
- The parts do not inherit from each other. A style without `title` leaves
  the title as the kind's style has it. A main `color` does not colour the
  title, the unit or the selected row.

There are four exceptions:

- **The unit's font.** Where neither the item's styles nor `displayitem`
  set `font-family` or `font-size` in `unit`, the unit has the value's.
  Face and size are taken separately.
- **The selected row's font.** Where no `selected` block sets `font-family`
  or `font-size`, the selected row has the rows'. Face and size are taken
  separately.
- **The selected row's `opacity`, `letter-spacing` and `text-decoration`.**
  Where no `selected` block sets one of these three, the selected row has
  the rows' value. Each of the three is taken separately. A `selected`
  block of the list's styles or of `list` that sets the property wins:
  `"selected": {"text-decoration": "none"}` gives a selected row without
  the underline of the rows. The `color` and the `background-color` of the
  selected row never come from the rows.
- **The icon's colour.** Where no `icon` block sets `color`, the tint is the
  colour of the main text. The tint applies to a one-colour icon raster. A
  colour raster is not tinted. An icon glyph in a text is text: it has the
  colour of its part.

### The `"style"` field of an item

- The value is a string of style names, separated by one or more spaces.
- An item lists at most four styles.
- A later name wins where two styles set the same property of the same
  part.
- A name given twice counts once, at its last place:
  `"alarm night alarm"` is `"night alarm"`.
- An item may not name `button`, `displayitem` or `list`. It has the style
  of its kind in any case.
- No field, or `""`: the item has the look of its kind.

## Properties

The names are CSS names. A value is a JSON string or a JSON number, as the
table gives it. A string holds no unit (`"48px"` is a bad value).

| Property | Value | Range |
|---|---|---|
| `color` | string | `"#rrggbb"` |
| `background-color` | string | `"#rrggbb"` or `"#rrggbbaa"` |
| `font-family` | string or integer | a font name, or the `id` of a `fonts` entry |
| `font-size` | integer | 6..72 (px) |
| `text-align` | string | `"left"`, `"center"`, `"right"` |
| `letter-spacing` | integer | -100..100 (px) |
| `text-decoration` | string | `"none"`, `"underline"`, `"line-through"`, `"underline line-through"` |
| `opacity` | number | 0..1, at most two decimals |
| `line-height` | number | 0.8..3.0, at most two decimals |

- **`color`.** The colour of the text. The `#` is necessary: `"ff0000"` is
  a bad value. A name such as `"red"` is a bad value.
- **`background-color`.** `"#rrggbb"` is opaque. In `"#rrggbbaa"` the last
  two digits are the opacity of the background, `00` to `ff`. See
  [Properties that have no effect yet](#properties-that-have-no-effect-yet).
- **`font-family`.** A font name, or the `id` of a `fonts` entry
  ([Fonts](#fonts)). An id is an integer of 1 or more.
- **`font-size`.** The size in px. In LVGL the size is part of the font, so
  face and size are looked up separately and then make one font instance.
- **`text-align`.** The place of the whole text of the part in its item,
  exactly as the markup `\al`, `\ac` or `\ar` in a text does.
  - A `\a` in the text wins over the style.
  - In `unit` and in `selected` the property is ignored.
  - A button label in several formats (a text with markup that changes
    font, size or colour) stays centred.
  - Without `text-align` and without `\a`: a part of a display item is as
    wide as its text, a button label is centred, a list is left aligned.
  - The `align` field of a display item places the item's box. It is not
    changed by `text-align`.
- **`letter-spacing`.** Extra space between characters, in px.
- **`text-decoration`.** `"line-through underline"` is the same as
  `"underline line-through"`.
- **`opacity`.** The opacity of the text only. 0 is invisible, 1 is solid.
  `0.333` is a bad value (three decimals). See
  [Numbers with decimals](#numbers-with-decimals).
- **`line-height`.** A multiple of the line height of the part's font. 1 is
  the font's own line. The built-in value of a list is 1.25: a quarter of a
  line between the rows. See
  [Properties that have no effect yet](#properties-that-have-no-effect-yet)
  and [Fonts](#fonts) for the difference from a font's `lineheight`.

### Numbers with decimals

`opacity` and `line-height` take a number with at most two decimals. The
firmware multiplies the value by 100 and takes the nearest integer. The
value is good when it is not more than 0.0001 from that integer.

- `0.5`, `0.29` and `1.25` are good.
- `0.333` and `1.234` are bad values (a third decimal).
- `0.2999999` is good: it is 29.99999 hundredths, and that is 0.00001 from
  30. The firmware reads it as `0.3`.
- The rule is the same in config.json and in `fb.style.set`.
- `opacity` is stored as 0..255: the hundredths times 2.55, to the nearest
  integer. `0.5` is 128.

`font-size` and `letter-spacing` take an integer. `14.5` is a bad value.

### Properties that do not exist

`font-weight` and `font-style` do not exist. A bold face is another font:
choose it with `font-family` (for example `montserrat_semibold`). Each of
the two names is logged as an unknown property.

### Properties that have no effect yet

These values are accepted without a warning, and change nothing on the
screen.

| Property | Has an effect | Has no effect |
|---|---|---|
| `background-color` | on a list: in the main block it is the background of the whole roller, in `selected` it is the background of the selected row | in a style that a button or a display item uses (main, `title` and `unit`) |
| `line-height` | on a list (main block); on a button label and a button top label in one format | on a display item (main, `title` and `unit`); in `selected`; on a button label in several formats |

## Errors in config.json

An error is logged as a warning. The property, the style or the name with
the error is ignored. The rest of the config loads.

| Log text | Cause | Result |
|---|---|---|
| `"styles" is not an object, ignored` | `styles` is not a JSON object | no styles; the built-in look |
| `styles: "<name>" is not a style name (letters, digits, _; not starting with a digit; max 24), skipped` | a bad style name | the style does not exist |
| `styles.<name> is not an object, skipped` | a style that is not an object | the style does not exist |
| `styles.<kind> is not an object, ignored` | `button`, `displayitem` or `list` is not an object | the kind has its built-in look |
| `styles: more than 32 styles, "<name>" and the rest are left out` | a 30th style besides the three reserved ones | that style and the styles after it do not exist |
| `styles.<style>: unknown property "<property>", ignored` | a name that is not a property | the property is ignored |
| `styles.<style>.<property>: bad value, ignored` | a value of the wrong type or outside the range | the property is ignored |
| `styles.<style>.<property>: unknown font, ignored` | `font-family` names no font | the property is ignored |
| `styles.<style>.<part> is not an object, ignored` | a sub-block that is not an object | the sub-block is ignored |
| `styles.<kind>.<part>: a <kind> has no such part, ignored` | a sub-block that the kind of a reserved style does not have (for example `unit` in `button`) | the sub-block is ignored |
| `"style" is not a string of style names, ignored` | an item's `style` is not a string | the item has no style of its own |
| `"style": "<name>" is the style of a kind and cannot be named by an item, left out` | an item names `button`, `displayitem` or `list` | that name is left out |
| `"style": "<name>" is not a style name, left out` | a bad name in an item's `style` | that name is left out |
| `"style": unknown style "<name>", left out` | a name that `styles` does not define | that name is left out |
| `"style": more than 4 styles, "<name>" left out` | a fifth name | that name is left out |
| `"<key>" is no longer read: <hint>` | a key of the old format ([Changed from the old format](#changed-from-the-old-format)) | the key is ignored |
| `A button has no "position": it is not shown` | a `buttons` entry without `position` | the entry is not loaded: no button, no topics, no name for scripts |

- For a property in a sub-block the log text has the sub-block's name
  after the style's name: `styles.alarm.title.color: bad value, ignored`.
- In an ordinary style, a sub-block that the item's kind does not have is
  ignored without a warning. Only a reserved style gets the "no such part"
  warning.

Two messages are logged as errors:

| Log text | Cause | Result |
|---|---|---|
| `styles: no memory for <n> styles, the device runs with the reserved ones only` | no heap for the table of styles | only `button`, `displayitem` and `list` exist |
| `styles: dropped for lack of LVGL memory (the pool is too low to hold their properties): <names>. Items and scripts that name them get "unknown style"` | no room in the LVGL pool for a style at boot | the named styles do not exist for this run |

The web IDE reports the same problems when you edit `config.json`.

## Names

- A style name is `[A-Za-z_][A-Za-z0-9_]{0,23}`: 1 to 24 letters, digits
  and `_`, not starting with a digit. This is the rule of `docs/NAMES.md`.
- Names are case sensitive: `Alarm` is not `alarm`.
- A config defines at most 32 styles, the three reserved styles included.
  So it defines at most 29 other styles. The three reserved styles count
  also when the config does not define them.

## The reserved styles

`button`, `displayitem` and `list` are the look of each kind. They always
exist. A config can define them to change the look of every item of the
kind. Without them, and for each property they do not set, the built-in
values apply:

| Style and block | `font-family` | `font-size` | `color` | Other |
|---|---|---|---|---|
| `button` (main: the label) | `montserrat` | 32 | `#ffffff` | |
| `button`, `title` (the top label) | `montserrat_semibold` | 18 | `#ffd700` | |
| `displayitem` (main: the value) | `montserrat` | 14 | `#ffffff` | |
| `displayitem`, `unit` | the value's | the value's | `#999999` | |
| `displayitem`, `title` (the caption) | `montserrat_semibold` | 14 | `#ffd700` | |
| `list` (main: the rows) | `montserrat` | 32 | `#808080` | `line-height` 1.25 |
| `list`, `selected` | the rows' | the rows' | `#000000` | `background-color` `#ffd700`; `opacity`, `letter-spacing` and `text-decoration`: the rows' |

- A reserved style has only the sub-blocks of its kind: `button` has
  `title` and `icon`, `displayitem` has `title`, `unit` and `icon`, `list`
  has `selected`.
- The selected row always has its own `color` and `background-color`. A
  main `color` of a list never colours it.
- A config without `styles` gives this table: the same look as before
  styles existed.

## The lookup

For one property of one part of an item, the firmware takes the first of:

1. the item's styles, the last name first: the block of that part;
2. the style of the item's kind (`button`, `displayitem` or `list`): the
   block of that part;
3. the built-in value.

- `font-family` and `font-size` are looked up separately. With
  `"style": "big semibold"`, where `big` sets `font-size` and `semibold`
  sets `font-family`, the item has the size of `big` and the face of
  `semibold`.
- For the unit and the selected row, a step 4 follows for face and size:
  the value's, or the rows'.
- For the selected row, the same step 4 follows for `opacity`,
  `letter-spacing` and `text-decoration`: the rows' value, which is the
  result of this lookup for the main block of the list.
- For the icon's colour, a step 4 follows: the colour of the main text.
- Steps 2 and 3 are resolved once, when the config loads.
- Text markup applies after the lookup, per span of a text. See
  [Text markup](#text-markup).

## Fonts

### The `fonts` table

`fonts` is a top-level array of config.json (`docs/MEDIA-STORE.md`, Fonts).
Each entry is an object:

| Field | Value | |
|---|---|---|
| `name` | `[A-Za-z_][A-Za-z0-9_]{0,23}` | Necessary. `font-family` and the `\f{name}` markup use it. |
| `id` | integer, 1 or more | Optional. `font-family` and `\f{id}` can use it in place of the name. |
| `src` | a key in the bucket `fonts`, at most 96 bytes, of `[A-Za-z0-9._-]`, ending in `.ttf` or `.otf` | Necessary for a new font. Not permitted for a system font. |
| `fallback` | a font name, or a font id | Optional. The font that draws a glyph this font does not have. |
| `lineheight` | number, 0.8..2.0 (em) | Optional. The line box of this font. |

- **Fallbacks are explicit.** A font without `fallback` has none. A glyph
  that no font in the chain has is drawn by LVGL's built-in Montserrat 12,
  the end of every chain. No font name is special in the firmware.
- **MDI icons.** Give the system fonts a fallback to an icon font, as in
  the example at the top. Without it, MDI codepoints (`\u{F0599}`) in
  texts show as boxes. The web IDE warns about this.

### `lineheight` of a font and `line-height` of a style

These are two different settings.

| | `lineheight` | `line-height` |
|---|---|---|
| Where | an entry of `fonts` | a block of a style |
| Unit | em: a multiple of the font size | a multiple of the font's line height |
| Range | 0.8..2.0 | 0.8..3.0 |
| Applies to | every text in that font, at every size | the parts that use the style |

- `lineheight` changes the line box of the font itself: the line is
  `font-size x lineheight` px high. The extra or missing space is divided
  equally above and below the text. Without it the font's own metrics
  apply. Use it to correct a font file whose lines are too high or too low.
- `line-height` adds space between the lines of one part:
  `(line-height - 1) x the font's line height`. The font's line height is
  the one that `lineheight` gave, when the font has one. Use it to space
  the rows of a list.
- Example: `montserrat` at 32 px with `"lineheight": 1.0` has lines of
  32 px. A list with `"line-height": 1.5` then puts 16 px between its rows.

### System fonts

Two fonts always exist, with or without a `fonts` entry:

| Name | File in bucket `fonts` |
|---|---|
| `montserrat` | `medium.ttf` |
| `montserrat_semibold` | `semibold.ttf` |

- **Reserved names.** A `fonts` entry with a system font's name may set
  only `id`, `fallback` and `lineheight`. A `src` there is logged and
  ignored. To use another font for the UI, set `font-family` in `button`,
  `displayitem` and `list`. The system fonts themselves are not replaced.
- **Protected files.** An S3 DELETE of `medium.ttf` or `semibold.ttf` is
  refused (403). A PUT is allowed (checked by `ttf_check` like any font),
  so the files can be updated. If a file is missing anyway, for example
  after a full erase, its font draws with LVGL's built-in Montserrat 12.
- **Firmware screens.** The modals (device info, credentials, Wi-Fi QR,
  OTA), the progress bar and the LVGL theme use the compiled font
  (`LV_FONT_DEFAULT`, Montserrat 12), not the styles. They stay legible
  whatever the config says.
- `icons.ttf` is an ordinary font file. It is shipped and flashed with the
  firmware, but it can be deleted and it has no special meaning.

### Limits

| Limit | Value |
|---|---|
| fonts | 8: the 2 system fonts and at most 6 from `fonts` |
| `fonts` entries that are read | 8; the rest is left out, with a warning |
| fonts in one fallback chain | 4, the first font included |
| `font-size` | 6..72 px |
| font instances | 24 |

A wrong entry is logged as a warning (`fonts: ...`) and skipped. A wrong
`fallback` or `lineheight` is ignored, and the entry is kept.

### Font instances

A font instance is one font at one size. The fallback fonts of that font at
that size are instances too. The device holds at most 24 instances and
never frees one.

- A block that sets both `font-family` and `font-size` holds its font
  instance in the style, for every part that uses it.
- A block that sets only one of the two makes no instance by itself. Each
  part that uses it gets the instance of the face and the size that the
  lookup gives. Three items with three different sizes that share one
  style with only `font-family` make three instances.
- When the table is full or the LVGL pool is low, a new instance is
  refused and logged.

## Padding of a display item

The space under the caption and under the box of a display item follows
the size of the value: the `font-size` that the lookup gives for the main
block.

- A size under 14 px: no padding.
- A size of exactly 14 px that none of the item's own styles set (it comes
  from `displayitem` or it is the built-in size): no padding. So an item
  without a style looks as it always did.
- Otherwise the nearest of the steps 14, 24, 32, 48 and 72 px. When two
  steps are equally near, the smaller one.

| Value size (px) | Step | Padding under the caption, `boxtype` 0 | Padding under the caption, other `boxtype` | Padding under the box |
|---|---|---|---|---|
| 14..19 | 14 | 2 | 2 | 1 |
| 20..28 | 24 | 4 | 4 | 3 |
| 29..40 | 32 | 4 | 0 | 8 |
| 41..60 | 48 | 10 | 6 | 8 |
| 61..72 | 72 | 14 | 6 | 12 |

- An item whose own style says `"font-size": 14` has the padding of the
  14 px step. An item that gets 14 px from `displayitem` has none.
- A size that text markup gives (`\s{40}`) does not count: the padding
  follows the styles only.

## Text markup

Markup in a text stays as it was. It applies per span of the text, and it
wins over every style.

| Markup | Effect |
|---|---|
| `\cRRGGBB` | colour of the following text |
| `\f{name}`, `\f{id}` | font of the following text; `\f{}` returns to the part's font |
| `\s{px}` | size in px, 6..72 |
| `\s1` .. `\s5` | the size steps 14, 24, 32, 48 and 72 px; `\s0` returns to the part's size |
| `\al`, `\ac`, `\ar` | the place of the text in its item: left, centre, right |

- **The title step rule.** In a title (a button's top label, a display
  item's caption) `\s2` is 18 px, not 24 px.
- A `\a` in a text wins over `text-align`. A text that starts with `\ac`
  and the same text in a part whose style says `"text-align": "center"`
  are drawn the same.
- A text without markup takes everything from the lookup. Give constant
  presentation to a style, and keep markup for texts with several colours
  or sizes.
- In a JavaScript string the backslash is written twice: `"\\ac12"`.

## Script API

The calls always exist, also on a device whose config has no `styles`.

| Call | Returns |
|---|---|
| `fb.button(name)` | the button's object: the same object as `fb.main.left` / `fb.main.right` or `fb.bars[i].left` / `.right` |
| `<button>.display.setStyle(styles)` | undefined |
| `<button>.display.addStyle(style)` | undefined |
| `<button>.display.removeStyle(style)` | undefined |
| `fb.main.display.setStyle(idOrName, styles)` | true; false for a numeric id that does not exist |
| `fb.main.display.addStyle(idOrName, style)` | true; false for a numeric id that does not exist |
| `fb.main.display.removeStyle(idOrName, style)` | true; false for a numeric id that does not exist |
| `fb.main.list.setStyle(nameOrIndex, styles)` | undefined |
| `fb.main.display.create(x, y, width, boxType, styles, alignment, label, unit, round, page)` | the id of the new display item |
| `fb.main.display.get(idOrName)` | an object; its `style` is the item's styles now, as a string. `null` for a numeric id that does not exist |
| `fb.style.set(style, property, value)` | undefined |

`<button>` is `fb.button(name)`, `fb.main.left`, `fb.main.right`,
`fb.bars[i].left` or `fb.bars[i].right`.

- **`styles`** is a string as in config.json: up to four style names,
  separated by spaces. It replaces the item's styles.
- **`setStyle(..., "")`** gives the item the styles of config.json again
  (none, where config.json gives none). For a display item that a script
  made, `""` gives the styles it was created with.
- **`style`** (`addStyle`, `removeStyle`) is one style name. A string of
  two names is an unknown style.
- **`addStyle`** puts the style last, so that it wins. A style the item
  has already moves to the last place. A fifth style throws, and nothing
  changes.
- **`removeStyle`** takes one style away, also one that config.json gave.
  A style that exists and that the item does not have: nothing changes.
- **A list** has `setStyle` only. It has no `addStyle` and no
  `removeStyle`.
- **`display.create`**: the fifth argument is a style string, `""` for no
  style.
- **`display.get().style`** is the names joined by one space, `""` for no
  style. It follows `setStyle`, `addStyle` and `removeStyle`.
- **`display.get(id)`** returns `null` for a numeric id that does not
  exist. A name that config.json does not have throws `ReferenceError`.
- `setStyle`, `addStyle` and `removeStyle` do not wait. They store the new
  list, and the display task draws it.
- Changes are not saved. See [Script restart](#script-restart).

```js
var close = fb.button("windowClose");
close.display.addStyle("night");                 // on top of "alarm"
close.display.removeStyle("night");              // "alarm" stays
fb.main.display.setStyle("co2", "big");          // no longer red
fb.main.display.setStyle("co2", "");             // "big alarm" again, from config.json
fb.main.list.setStyle("scenes", "picked night");
var id = fb.main.display.create(50, 60, 40, 0, "big alarm", 0, "New", "u", 0, 1);
print(fb.main.display.get(id).style);            // "big alarm"
```

### Errors of the style calls

In the messages, `<call>` is the name of the method:

- `setStyle`, `addStyle`, `removeStyle` for a button;
- `display.setStyle`, `display.addStyle`, `display.removeStyle`,
  `display.create` for `fb.main.display`;
- `fb.main.list.setStyle` for a list.

| Type | Message | Thrown for |
|---|---|---|
| `TypeError` | `<call>() requires style names (a string)` | `setStyle`, `display.create`: `styles` is not a string, or is absent |
| `TypeError` | `<call>() requires a style name (a string)` | `addStyle`, `removeStyle`: `style` is not a string, is `""`, or is absent |
| `TypeError` | `<call>(): "<name>" is the style of a kind and cannot be named by an item` | the name is `button`, `displayitem` or `list` |
| `TypeError` | `<call>(): the style names have a NUL character in them` | `setStyle`, `display.create`: a NUL character in `styles` (`"big\0junk"`) |
| `TypeError` | `<call>(): the style name has a NUL character in it` | `addStyle`, `removeStyle`: a NUL character in `style` |
| `ReferenceError` | `unknown style "<name>"` | a name that is no style of config.json, also for `removeStyle`; names are case sensitive |
| `RangeError` | `<call>(): an item has at most 4 styles` | `setStyle`, `display.create`: a fifth name; `addStyle`: the item has four other styles |
| `TypeError` | `display.setStyle() requires id and styles arguments` | fewer than two arguments |
| `TypeError` | `display.addStyle() requires id and style arguments` | fewer than two arguments |
| `TypeError` | `display.removeStyle() requires id and style arguments` | fewer than two arguments |
| `TypeError` | `display.create() requires 10 arguments: x, y, width, boxType, styles, alignment, label, unit, round, page` | fewer than ten arguments |
| `InternalError` | `Failed to create display item` | `display.create`: the item could not be made |
| `ReferenceError` | `unknown display item "<name>"` | an item name that config.json does not have |
| `ReferenceError` | `unknown list "<name>"` | a list name that config.json does not have |
| `RangeError` | `no list <n>` | a list index that does not exist |
| `TypeError` | `fb.main.list: a list name is required` | `fb.main.list.setStyle()` without arguments |
| `InternalError` | `failed to set style for button <n>` | `setStyle`, `addStyle`, `removeStyle` on a button that has no entry in `buttons`; `<n>` is its position. The argument is checked first. |
| `TypeError` | `fb.button() requires a button name (string)` | `fb.button`: the argument is not a string, or is absent |
| `ReferenceError` | `unknown button "<name>"` | `fb.button`: a name that no `buttons` entry has |
| `ReferenceError` | `button at position <n> is not connected` | `fb.button`: nothing is connected at the button's connector |

- An error changes nothing.
- A name given twice in `styles` counts once, so
  `"alarm night alarm big picked alarm"` is four styles.

### `fb.style.set(style, property, value)`

`fb.style.set` sets one value of a style. Every item that uses the style
is drawn again with the new value, a text with markup included.

- **`style`** is a style name, or `button`, `displayitem` or `list`.
- **`property`** is a property name for the main block (`"color"`), or one
  of the prefixes `title.`, `unit.`, `selected.` or `icon.` and a property
  name (`"title.color"`, `"selected.background-color"`, `"icon.color"`).
- **`value`** is a string or a number, as in config.json.

What can be set:

| Property | |
|---|---|
| `color`, `background-color`, `text-align`, `letter-spacing`, `text-decoration`, `opacity` | Can be set. A property that the block does not set in config.json is added. |
| `icon.color` | Can be set, also where config.json sets none. |
| `font-family`, `font-size`, `line-height` | Cannot be set, in any style. |

- **A property is added.** `fb.style.set("alarm", "opacity", 0.4)` works
  when `alarm` sets no `opacity` in config.json. The property is in the
  style from then on, for every item that uses the style, until the script
  restarts.
- **The block must exist.** A property is set in a block that the style
  has.
  - A style of `styles` has the blocks that config.json gives it. A
    sub-block that config.json does not give cannot be made:
    `fb.style.set("alarm", "title.color", ...)` throws when `alarm` has no
    `title` block. The firmware sizes the table of styles when the config
    loads.
  - The main block of a style of `styles` exists when the style sets at
    least one property of its own. A style that has only sub-blocks
    (`"picked": {"selected": {...}}`) and the style `{}` have no main
    block. A property with a bad value counts: `{"color": "red"}` has a
    main block.
  - `button`, `displayitem` and `list` always have all the parts of their
    kind: `button` has main and `title`, `displayitem` has main, `title`
    and `unit`, `list` has main and `selected`.
  - `icon.color` can be set for every style of `styles`, for `button` and
    for `displayitem`. A list draws no icon: `list` has no `icon` part.
- **The three fixed properties.** The font and the size of a part are
  calculated per item, and the line height per font. A new size makes a
  font instance, and the device never frees a font instance. To change
  the font or the size of an item from a script, give the item another
  style.
- **`background-color`** can be set like the other properties. It shows
  only on a list ([Properties that have no effect yet](#properties-that-have-no-effect-yet)).
- **`text-align`** that a script sets places the text as the same value in
  config.json does.
- A style that no item uses can be changed. An item that takes the style
  later shows the new value.
- `fb.style.set("button", "color", "#00ff00")` colours the label of every
  button that has no style of its own that sets `color`.

**The order of the checks.** The firmware makes the checks in this order.
The first check that fails gives the error.

1. The arguments: three of them, `style` and `property` are strings,
   `style` is not `""`.
2. `style` is the name of a style.
3. `property` has no NUL character, and is shorter than 32 characters.
4. `value` is a string or a number, and a string has no NUL character.
5. `property` is a property name (without a prefix, or with a prefix that
   is a part).
6. The property is not `font-family`, `font-size` or `line-height`.
7. The style has the block.
8. The value is good for the property.
9. There is memory for the change.

So `fb.style.set("big", "font-size", 14.5)` says that `font-size` cannot
be changed, not that 14.5 is a bad value. And
`fb.style.set("muted", "title.color", "red")` says that `muted` has no
`title` block, when it has none.

**Each call waits for the display task.** The display task holds its lock
for a whole render: 80 to 150 ms for a full repaint. The call blocks the
calling task for that time. Several calls in one event handler add up, and
a button handler does not see the next press during that time.

- Change one property per event.
- Or switch styles with `setStyle`, `addStyle` or `removeStyle`. They do
  not wait.

```js
fb.style.set("alarm", "color", "#00ff00");        // everything that uses "alarm" is green
fb.style.set("alarm", "title.color", "#80ff80");
fb.style.set("alarm", "opacity", 0.4);            // added: "alarm" sets no opacity in config.json
fb.style.set("big", "color", "#ff0000");          // added to a style that only sets a size
fb.style.set("button", "title.color", "#00ffff"); // the top label of every button
fb.style.set("button", "text-decoration", "underline");
fb.style.set("list", "selected.background-color", "#0000ff");
```

| Type | Message | Thrown for |
|---|---|---|
| `TypeError` | `fb.style.set() requires a style name, a property name and a value` | fewer than three arguments; a style or a property that is not a string; the style `""` |
| `ReferenceError` | `unknown style "<name>"` | a name that is no style |
| `TypeError` | `fb.style.set(): the property name has a NUL character in it` | a NUL character in the property |
| `TypeError` | `fb.style.set(): the value must be a string or a number` | `true`, `null`, `undefined`, an object, an array, a function |
| `TypeError` | `fb.style.set(): the value has a NUL character in it` | a NUL character in a string value |
| `TypeError` | `fb.style.set(): unknown property "<property>"` | a name that is not a property (`font-weight`, a name of the old format), a prefix that is no part (`main.color`), `icon.` with another name than `color`, a name of 32 characters or more |
| `TypeError` | `fb.style.set(): "<property>" cannot be changed by a script` | `font-family`, `font-size`, `line-height`, with or without a prefix |
| `TypeError` | `fb.style.set(): the style has no "<part>" block in config.json` | a style of `styles` that has no `title`, `unit` or `selected` block, with a property of that block |
| `TypeError` | `fb.style.set(): the style has no main block in config.json` | a style of `styles` that sets no property of its own, with a property without a prefix |
| `TypeError` | `fb.style.set(): the style of this kind has no "<part>" part` | `button`, `displayitem`, `list`: a part that the kind does not have (`button` with `unit.`, `list` with `icon.`) |
| `TypeError` | `fb.style.set(): not a valid value for "<property>"` | a value of the wrong type or outside the range (`"red"`, `"ff0000"`, `0.333` for `opacity`, `"0.5"` for `opacity`, `2.5` for `letter-spacing`) |
| `InternalError` | `out of memory` | no heap for the copy of the config values (the first `fb.style.set` after a script start), or no room in the LVGL pool for a property more; nothing is changed |

`<property>` in a message is the property as the script gave it, with its
prefix: `"title.font-size" cannot be changed by a script`. `<part>` is the
prefix without the dot: `title`, `unit`, `selected` or `icon`.

## Script restart

A script restart (`docs/SCRIPT-RESTART.md`) restores:

- the styles of every button, display item and list: the list that
  config.json gives it;
- every value that `fb.style.set` changed, in the styles of `styles` and in
  `button`, `displayitem` and `list`, the `icon` colours included;
- every property that `fb.style.set` added: the style no longer sets it.

After the restart the screen has the same pixels as before the script
changed a style, and the LVGL pool has as many blocks in use as before.

A display item that a script made is removed, with its styles.

`GET /config` writes an item's `"style"` as the names of its configured
list, joined by one space. It writes no `"style"` field for an item
without styles. It writes no `styles` block and no `fonts` block.

## Memory

How the styles are stored:

| What | Where | Size |
|---|---|---|
| `button`, `displayitem`, `list` | static: seven blocks (one per kind and part), each its properties and one shared `lv_style_t` | no heap |
| the other styles | one heap block, allocated when the config loads and sized to the config | per style 8 bytes; per block that the style has (main, `title`, `unit`, `selected`) its properties (20 bytes), one shared `lv_style_t` and 2 flag bytes; per name 1 byte and its length |
| a config with only reserved styles | | no heap block |
| the styles of an item | in the item | 4 bytes (the configured list) and one 32-bit word (the list now) |
| the properties of a shared style | LVGL pool | allocated once, when the config loads |
| a style on an object | LVGL pool | one entry in the object's style list |
| a block that sets `font-family` and `font-size` | LVGL pool | one font instance: 380 bytes, measured for one instance of 48 px |
| a property that `fb.style.set` added | LVGL pool | 5 bytes in the property block of the style's `lv_style_t`, until the script restarts |
| the copy of the config values for `fb.style.set` | one heap block, from the first `fb.style.set` after a script start until the restart | 152 bytes with no style beyond the reserved ones; 20 bytes more per block and 4 bytes more per style of `styles` |

- A name is stored once. An item stores indexes, never a name and never a
  copy of a style.
- The `icon` colour of a style is in the 8 bytes of the style. It has no
  `lv_style_t`.
- A block that holds nothing for LVGL (for example only `font-size`) is
  not added to an object.
- The boot log gives the size of the heap block:
  `Loaded <n> styles (<bytes> bytes)`.
- A style call that succeeds allocates nothing on the JS heap: the names
  are looked up in the firmware's table and are not copied. The exception
  is the string that `display.get().style` returns. The restore at a
  script restart allocates nothing.
- **A property that a script adds** needs no memory while the script does
  not add it. The record of every block has a place for every property.
  The added property takes its 5 bytes in the LVGL pool while it exists.
  A block that held nothing for LVGL (for example a style of a size only)
  is added to the objects of its users when it gets its first such
  property: one entry in the style list of each object. A restart takes
  the property out and takes the block off the objects again. The restore
  itself allocates no heap. The firmware then builds the property block
  of each changed style again at its smaller size, which needs a free
  block of that size in the LVGL pool for a moment.
- **A low LVGL pool, at run time.** A style is added to an object only
  when the pool has room. The firmware makes one check for all the parts
  of an item together: the caption, the value and the unit of a display
  item, the label and the top label of a button, the rows and the selected
  row of a list. Without room for all of them, every part keeps the styles
  that it carried, font and size included, and the device tries again a
  fraction of a second later. An item never shows half a list of styles,
  and never one part with the new styles and another part with the old
  ones. The log has
  `style: LVGL pool low, an item's styles were not added; tried again later (warned once)`
  once.
- **A low LVGL pool and `fb.style.set`.** A call that adds a property
  throws `InternalError` (`out of memory`) when the pool has less than
  about 4 KB free (its reserve for drawing), and changes nothing. A call that changes the value
  of a property that the style sets already needs no room.
- **A low LVGL pool, at boot.** A style that the pool has no room for is
  dropped (see [Errors in config.json](#errors-in-configjson)). A dropped
  style does not exist for the rest of the run: an item that names it logs
  "unknown style", and the script calls throw `ReferenceError` for it.
- **No heap for the table.** The device runs with the reserved styles
  only. It does not stop.

Budget for the JS heap: at most 350 bytes more than the firmware of commit
909014a. For the internal heap and the LVGL pool: no more than that
firmware.

Measurements on a device. The device is the bench device btn_a00002
(2 buttons), measured on 2026-11-19. Its own config was converted to four
size styles and no other styles. Each figure is after boot.

| Measurement | Firmware of commit 909014a | This firmware | Difference |
|---|---|---|---|
| JS heap free | 10,432 bytes | 10,248 bytes | 184 bytes more used: the style methods, which always exist now |
| LVGL pool free | 16,708 bytes | 16,878 bytes | 170 bytes more free |
| Internal heap free | 58.7 KB | 56.9 KB | not attributable: the readings of this device vary by about 1.5 KB from boot to boot, and the difference of 1.8 KB is inside that spread |

- The JS heap is inside the budget of 350 bytes.
- The LVGL pool has more free than before.
- The internal heap: the measurement cannot show a difference in one
  direction or the other.

| `sizeof` on the device build | Firmware of commit 909014a | This firmware |
|---|---|---|
| `Button` | 372 | 356 |
| `DisplayItem` | 316 | 300 |
| `List` | 132 | 124 |

| Other figures | Value |
|---|---|
| one font instance of 48 px, LVGL pool | 380 bytes |

Not measured on a device:

- **Three extra styles used by six items** (heap, LVGL pool, JS heap).
- **btn_a00001 with its config and script**, before and after the
  conversion. Its script needs six buttons, and the bench device has two.

For btn_a00001 the emulator gives these figures. The emulator runs the
firmware's own code for the styles, the script engine and LVGL, with the
device's config and script.

| Emulator, btn_a00001 converted against before | Figure |
|---|---|
| JS heap | 172 bytes more used |
| LVGL pool, lowest free on its forecast page | 140 bytes lower |
| its six size styles (one property each) | 308 bytes of heap, no LVGL pool |

## Changed from the old format

There is no backwards compatibility. The firmware does not read the old
format.

- A removed key is logged once per key name
  (`"<key>" is no longer read: <hint>`) and is ignored.
- The items under a removed array name are not shown.
- A `buttons` entry without `position` is not loaded, and logs a warning.
  It gives no button, subscribes no topics and has no name for scripts.
- A config in the old format loads without a crash and shows the built-in
  look.

### Config keys that the firmware reports

| Removed key | Where | Use (the hint in the log) |
|---|---|---|
| `style` | top level | use the top-level `"fonts"` and `"styles"` |
| `mqttbuttons` | top level | use `"buttons"` |
| `mqttdisplays` | top level | use `"displayitems"` |
| `mqttbrokers` | top level | use `"brokers"` |
| `font` | button, display item, list | use a style with `"font-family"` |
| `fontsize` | button, display item, list | use a style with `"font-size"` |
| `topfont` | button | use a style with `"title": {"font-family"}` |
| `topfontsize` | button | use a style with `"title": {"font-size"}` |
| `topstyle` | button | use a style with a `"title"` block |
| `topLabel` | button | use `"toplabel"` |
| `leds` | button | put the topics in `"topics"`; the topics under `"leds"` are no longer subscribed |
| `buttonuid` | button | `"position"` and `"page"` say where the button is |
| `id` | button | use `"position"` |
| `labelfont` | display item | use a style with `"title": {"font-family"}` |
| `labelfontsize` | display item | use a style with `"title": {"font-size"}` |
| `labelstyle` | display item | use a style with a `"title"` block |
| `iconcolor` | display item | use a style with `"icon": {"color"}` |
| `color` | list | use a style with `"color"` |
| `focuscolor` | list | use a style with `"selected": {"background-color"}` |
| `focustextcolor` | list | use a style with `"selected": {"color"}` |
| `focusstyle` | list | use a style with a `"selected"` block |
| `autoDiscovery` | broker | use `"hassdiscovery"`; Home Assistant discovery is on unless `"hassdiscovery"` is false |

- **`autoDiscovery`.** The old firmware read `"autoDiscovery": false` as
  "no Home Assistant discovery". This firmware does not read the key, so
  a broker with only `"autoDiscovery": false` has discovery on. Write
  `"hassdiscovery": false` to turn it off.
- **`hassdiscovery`.** Absent: discovery is on. Otherwise the value is
  read as a boolean: `false` and `0` turn discovery off.
- **`leds`.** The topics under a button's `leds` are not subscribed. Put
  them in the button's `topics`.

`GET /config` no longer writes `id` and `buttonuid` for a button, and no
longer writes `info.connectors[].connector`.

### The members of the old `style` block

The firmware reports `style` once and reads none of its members.

| Old | New |
|---|---|
| `style.fonts` | the top-level `fonts` (the same entries) |
| `style.text` | the main blocks of `button`, `displayitem` and `list` |
| `style.title` | the `title` blocks of `button` and `displayitem` |
| `style.key.label` | `button` (main block) |
| `style.key.toplabel` | `button`, `title` |
| `style.display.value` | `displayitem` (main block) |
| `style.display.unit` | `displayitem`, `unit` |
| `style.display.caption` | `displayitem`, `title` |
| `style.list` | `list` (main block) |
| `style.list.selected` | `list`, `selected` |
| `style.styles` | the top-level `styles`, next to the three reserved styles |
| one style per part (`style`, `topstyle`, `labelstyle`, `focusstyle`) | one field `style` with up to four names; a style has `title`, `unit`, `icon` and `selected` blocks for the other parts |

### Property names

| Old | New |
|---|---|
| `text_color` (`"#rrggbb"` or `"rrggbb"`) | `color` (`"#rrggbb"`; the `#` is necessary) |
| `text_font` | `font-family` |
| `text_size` | `font-size` |
| `text_align` (the lines inside one label) | `text-align` (the text in its item, as `\a` markup) |
| `text_letter_space` | `letter-spacing` |
| `text_line_space` (px) | `line-height` (a multiple of the font's line height) |
| `text_decor`: `"strikethrough"` | `text-decoration`: `"line-through"` |
| `text_opa` (0..255) | `opacity` (0..1) |
| `bg_color` and `bg_opa` | `background-color`: `"#rrggbbaa"` |

### Sizes

| Old | New |
|---|---|
| `fontsize` 1, 2, 3, 4, 5 | `font-size` 14, 24, 32, 48, 72 |
| `topfontsize` / `labelfontsize` 2 | `font-size` 18 in a `title` block |
| `fontsize` 6..72 | `font-size` with the same number |

The steps `\s1` .. `\s5` stay in text markup.

### Script calls

| Old | New |
|---|---|
| `fb.key(name)` | `fb.button(name)` |
| `setFont` (button, display item, list) | a style with `font-family` / `font-size`, given with `setStyle` or `addStyle` |
| `setTopFont` (button) | a style with a `title` block that sets `font-family` / `font-size` |
| `setTopStyle` (button) | `setStyle` with a style that has a `title` block |
| `setLabelStyle` (display item) | `setStyle` with a style that has a `title` block |
| `setFocusStyle` (list) | `fb.main.list.setStyle` with a style that has a `selected` block |
| `setStyle(..., "")`: the item has no style | `setStyle(..., "")`: the item has the styles of config.json again |
| `display.create(x, y, width, boxType, fontSize, ...)` | `display.create(x, y, width, boxType, styles, ...)`: a style string in the same place, `""` for none |
| `display.get().fontSize` | `display.get().style` |
| `fb.style.set` with an old property name | `fb.style.set` with the new property name; a prefix `title.`, `unit.`, `selected.` or `icon.` for a sub-block |
| `fb.style.set` changes only a property that the style sets in config | `fb.style.set` also adds a property, in a block that the style has |
| `fb.style` and `setStyle` exist only when the config has styles | the calls always exist |

A script that calls a removed method gets the engine's `TypeError`
("not a function").
