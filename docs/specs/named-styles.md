# Spec: styles (version 2)

Status: approved for building (2026-11-18). The section "Decisions and corrections after review" at the end is binding and overrides the text above where they differ. Replaces version 1 of this spec, whose
implementation is in the working tree (uncommitted) and is the starting point:
named styles in `style.styles`, four item fields, LVGL property names. Version
2 keeps its machinery (one shared `lv_style_t` per style, the table, the
script path through the LVGL task, restart restore) and changes the model, the
config format and the names. There is **no backwards compatibility**: the old
format is removed from the parser, and the configs in the repository and on
the two devices are rewritten.

## The model

1. `styles` is a set of named looks.
2. An item lists the ones it wants: `"style": "big night"`. A later name wins
   where two set the same property.
3. Whatever none of them sets comes from the look named after the item's
   kind: `button`, `displayitem` or `list`.

A style only says what differs. A property the kind's style does not set
either is the firmware's built-in look (the table "Firmware defaults" of
`docs/STYLE.md`, unchanged on screen). There are no pages in this model: a
page is only a number on an item, so the items of a page name their style
themselves (the IDE offers "add a style to every item of this page").

```json
"fonts": [
  {"id": 1, "name": "mdi", "src": "icons.ttf"},
  {"name": "montserrat",          "fallback": "mdi"},
  {"name": "montserrat_semibold", "fallback": "mdi"}
],
"styles": {
  "button":      {"font-size": 32, "title": {"font-size": 18}},
  "displayitem": {"font-size": 14},
  "list":        {"font-size": 32, "selected": {"background-color": "#ffffff"}},

  "big":   {"font-size": 48},
  "alarm": {"color": "#ff0000", "title": {"color": "#ff8080"}},
  "night": {"color": "#606060"}
},
"buttons": [
  {"name": "windowOpen",  "label": "Open"},
  {"name": "windowClose", "label": "Dicht", "style": "alarm"}
],
"displayitems": [
  {"name": "time", "page": 2, "style": "big night"},
  {"name": "soc",  "page": 2, "style": "night"},
  {"name": "co2",  "page": 1, "style": "big alarm"}
]
```

`"style"` is a string of up to three style names separated by single spaces.
More than three, an unknown name or a bad name: a warning, and that name is
left out.

### What is removed

- The `style` block: `style.fonts` (now top-level `fonts`), the roles
  `style.text` and `style.title`, the elements `style.key.*`,
  `style.display.*`, `style.list*`, and version 1's `style.styles`.
- The item fields `font`, `fontsize` (and its steps 1..5), `topfont`,
  `topfontsize`, `labelfont`, `labelfontsize`, the list fields `color`,
  `focuscolor`, `focustextcolor`, and version 1's `topstyle`, `labelstyle`,
  `focusstyle`.
- The array aliases `mqttbuttons` and `mqttdisplays` (only `buttons` and
  `displayitems` are read), and any other alias of the vendor's original
  config format that the parser still accepts: the reviewer lists them, and
  they are removed unless the firmware itself writes them.
- The script calls `setFont` and `setTopFont` (a script sets a style
  instead), and `fb.key()`, which becomes `fb.button()`.

A removed key in a config is reported once per key name at load as a warning
that names its replacement (for example `"fontsize" is no longer read: use a
style with "font-size"`), and is otherwise ignored. The web IDE reports the
same as config problems.

## A style

A style is an object of properties for the item's main text, with up to four
optional sub-blocks of the same properties:

| Part | Button | Display item | List |
|---|---|---|---|
| main (the style itself) | the label | the value | the rows |
| `title` | the top label | the caption (`label`) | not used |
| `unit` | not used | the unit | not used |
| `icon` | the icon | the icon | not used |
| `selected` | not used | not used | the selected row |

`icon` takes `color` only (the tint of an icon set with `setIcon` or the
config's icon field). It is in this version only if the icon's tint already
runs through the text colour of an object a style can be added to; the
reviewer confirms how icons are tinted today and what `icon` costs. If it
needs new objects or a new draw path it is deferred, and the spec says so.

- A sub-block an item's kind does not have is ignored.
- Parts do not inherit from each other inside a style: a style without
  `title` leaves the title as the kind's style has it. Two exceptions, as
  today: the **unit** uses the value's font, size and the unit's own colour
  unless the style's `unit` block says otherwise; the **selected row** uses
  the rows' font unless `selected` sets one.
- **Names:** `[A-Za-z_][A-Za-z0-9_]{0,23}`, case sensitive, at most 32
  styles including the three reserved ones. `button`, `displayitem` and
  `list` are reserved: they can be defined (to change the defaults) and
  changed by `fb.style.set`, and an item may also name one explicitly.
- A style that an item of another kind uses simply applies its main text and
  the sub-blocks that kind has. There is no restriction by kind.

### Properties

CSS names. Values are JSON strings or numbers as given; no units in strings.

| Property | Value | Maps to |
|---|---|---|
| `color` | `"#rrggbb"` | LVGL `text_color` |
| `font-family` | a font name, or the `id` of a `fonts` entry | font face |
| `font-size` | integer px, 6..72 | font size (one font instance per face and size) |
| `text-align` | `"left"`, `"center"`, `"right"` | the position of the text in the item, as the `\\a` markup (see below) |
| `letter-spacing` | integer px, -100..100 | `text_letter_space` |
| `text-decoration` | `"none"`, `"underline"`, `"line-through"`, `"underline line-through"` | `text_decor` |
| `opacity` | number 0..1 | `text_opa` (the text; see Out of scope) |
| `line-height` | number 0.8..3.0, a multiple of the font's line height | `text_line_space`, computed per font |
| `background-color` | `"#rrggbb"` or `"#rrggbbaa"` | `bg_color` and `bg_opa` |

- `text-align` positions the whole text of the part in its item, exactly as
  the `\\al` / `\\ac` / `\\ar` markup at the start of a text does today (and as
  the list and the button labels already do). Version 1 and the old style
  system only aligned lines inside one label, which is why device scripts
  prefix texts with `\\ac`. A `\\a` in a text still wins over the style. The
  item's own `align` field (which positions the box) is unchanged; the
  reviewer states exactly how `align`, `\\a` and `text-align` combine today
  and proposes the rule.
- `background-color` is drawn only for a list (rows and `selected`). In a
  style used by a button or a display item it is accepted and has no effect
  (their text containers are transparent by construction); the IDE says so.
  Item backgrounds are out of scope.
- `line-height`: the gap LVGL needs is `(line-height - 1) x the font's line
  height`, so it depends on the font and is set per object where it is used.
  The list's built-in default stays a quarter of a line: `line-height` 1.25.
- There is no `font-weight` or `font-style`: a bold face is another font,
  chosen with `font-family`. The IDE says so for these two names.
- Errors: an unknown property, a bad value, an unknown font, a bad style
  name or a 33rd style is logged as a warning and ignored; the rest loads.

### Fonts in a style

In LVGL the size is part of the font. For a part of an item, face and size
are each taken from the item's styles (last name first), else the
kind's style, else the built-in default. A style whose part sets both
`font-family` and `font-size` holds that font instance in its shared
`lv_style_t`; otherwise the component sets the resulting font locally on the
object, as version 1 does for "one of the two". Font instances are limited to
24 and never freed.

The padding rule of a display item (`docs/STYLE.md`, "Padding of a display
item") used the item's `fontsize` step. It now uses the value's resulting
size: the nearest step (14, 24, 32, 48, 72), no padding under 14 px, and no
padding when the size is the built-in 14 that no style set (so today's
default look is unchanged). The reviewer checks this against `box_logic.h`
and proposes the exact rule that keeps every current screen identical.

## The lookup, complete

For one property of one part of an item:

1. the item's styles, last name first: that part's block;
2. the kind's style (`button` / `displayitem` / `list`): that part's block;
3. the firmware's built-in value.

In LVGL: the built-in and kind layers are resolved into one shared
`lv_style_t` per kind and part (the seven element styles that exist today);
each style of the item is one shared `lv_style_t` per style and part, added
after it in the order of the list (the added-last style wins in LVGL 9.6);
nothing is local except a font, line height or alignment that had to be
computed per object. Face and size are each taken separately through the
same order, so `"big semibold"` gives the size of `big` and the face of
`semibold`.

## Script API

| Call | Effect |
|---|---|
| `fb.button(name)` | what `fb.key(name)` was; `fb.key` no longer exists |
| `<button side>.display.setStyle(styles)` | the button's style list |
| `fb.main.display.setStyle(idOrName, styles)` | the display item's style list (returns boolean as its neighbours) |
| `fb.main.list.setStyle(nameOrIndex, styles)` | the list's style list |
| `...addStyle(..., style)` on the same three | add one style on top of what the item has |
| `...removeStyle(..., style)` on the same three | take one style away |
| `fb.style.set(style, property, value)` | change a value of a style |

- `styles` is a string as in config.json (up to three names). `""` returns
  the item to the list config.json gives it (none, if it gives none).
- `addStyle` appends a name (so it wins); naming a style the item already
  has moves it to the end. A fourth name: `RangeError`, nothing changes.
  `removeStyle` of a style the item does not have does nothing. These two
  let a script switch a state (`alarm`) without knowing the item's other
  styles.
- Unknown style: `ReferenceError`. Not a string: `TypeError`.
- `fb.style.set`: `property` is a property name, or `title.`, `unit.` or
  `selected.` followed by one (`"title.color"`). It changes a value **that
  the style sets in config**; `font-family`, `font-size` and `line-height`
  cannot be changed (they are per font), a property the style does not set
  cannot be added: both `TypeError`. It works on the reserved styles too, so
  `fb.style.set("button", "color", ...)` recolours every button without a
  style of its own that sets a colour. Every user restyles at once. Each
  call waits for the display task (see `docs/STYLE.md`).
- `setFont` and `setTopFont` are removed from buttons, display items and
  lists, with their natives, method ids and font-argument helper if nothing
  else uses it.
- The methods are always present (the reserved styles always exist), so
  version 1's gating on "config has named styles" goes away. The JS heap
  cost of the methods is measured and reported; the removed `setFont`
  methods offset part of it.
- Threads, restart restore and the backup block: as version 1 (script calls
  only store a byte and a dirty flag; `fb.style.set` takes the LVGL lock for
  the values and a dirty bit; the LVGL task refills and has users redraw; a
  script restart restores every item's configured style list and the
  values changed by `fb.style.set`, allocating nothing).

## Data model and memory

- The three reserved styles always exist (static, as the seven element
  styles are today: their resolved properties and `lv_style_t`s). When the
  config defines `button` / `displayitem` / `list`, its values are resolved
  into them at load; no table entry is spent on them beyond a name.
- Other styles: one allocation at config load sized to the config, per
  style one entry per part it actually has (main, `title`, `unit`,
  `selected`): `style_props_t` + `lv_style_t` + flags, and the names in one
  character block, each once. No allocation when the config defines no
  style beyond the reserved ones.
- An item stores its configured and its current style list: three index
  bytes each (0 = empty slot).
- The item's font fields (`fontFace`, `fontSize`, `font_` and their top /
  label variants) and the list colour fields are removed with the fields
  they stored. Report `sizeof` of Button, DisplayItem and List before and
  after.
- No `std::string`, no `std::function`, no per-item copies; pure rules in
  `src/ui/style_logic.[ch]`; allocation failure runs without the styles
  beyond the reserved ones and never aborts; pool checks as version 1.

## Web IDE, emulator, generated files

- The TypeScript mirror follows the new model exactly: `style-config.ts`,
  `font-config.ts`, `config-model.ts`, `config-edit.ts`, `item-props.ts`,
  `validator.ts`, `autocomplete.ts`, the popover and the fallback HTML
  preview. Config problems: removed keys (with the replacement), unknown
  style, bad name, more than 32, unknown property, bad value,
  `font-weight` / `font-style`, `background-color` in a style used by a
  button or display item (a note, not an error).
- The style editor of the IDE (wherever `style.text` etc. are edited or
  previewed today) works on `styles` and `fonts`.
- Script help: `fb.button`, the new calls, no `fb.key`, `setFont`,
  `setTopFont`. A script that still calls a removed one is reported by the
  validator with the replacement. Generated files only through the
  generators.
- Emulator: rebuilt; the base-font copies in `tools/js/emulator` follow the
  firmware.

## Files to rewrite to the new format

- `data/config.json` (factory config) and everything under `data/` that
  holds config or scripts (the example scripts use `fb.key` and possibly
  `setFont`).
- Documentation: `docs/STYLE.md` is rewritten around the model above (not
  patched); `docs/LISTS.md`, `docs/SCRIPT-RESTART.md`, `docs/NAMES.md` and
  any other doc, README or IDE help text that shows the old block, the old
  fields, `fb.key` or `setFont`.
- Tests and fixtures in `test/` and `web/`.
- `devices/btn_a00001/config.json` and `btn_a00001.js`, and
  `devices/btn_a00002/config.json` and its script (a separate, git-ignored
  folder that the emulator tests read): converted so that they render
  exactly as before. They are deployed to the devices by hand afterwards.

## Converting btn_a00001

The device's config and script are the test of whether the design pays off:

- `displayitem` gets `font-size` 24 (16 of its 34 items use it); the other
  sizes become named styles; the 28 `fontsize` fields become about 12 style
  names.
- Constant presentation leaves the script: texts that today start with
  `\\ac`, a fixed `\\c` colour or a fixed `\\s` size get a style in the config
  and the script sets the plain text (about 12 lines). Texts with several
  colours and glyphs coloured by a value keep their markup.
- `fb.key` becomes `fb.button` (8 places).
- The converted device must render exactly as before, page by page.

## Tests

- Native: the property parser (every property, ranges, `#rrggbbaa`,
  `line-height` conversion, `text-decoration` words, `opacity`), names and
  the limit of 32, reserved names, sub-blocks, the lookup (item styles in order /
  kind / built-in, per part, unit and selected exceptions), fonts (both /
  one / none), padding, `fb.style.set` validation including dotted
  properties, removed-key detection.
- Web: the same rules in the mirror, compared case by case with the
  emulator; config problems; the script validator for the renamed and
  removed calls; through the emulator: a style on a button, a display item
  and a list; two and three styles on one item, order and per-property
  winner, face from one and size from another; a reserved style changed in
  config; `setStyle`, `addStyle`, `removeStyle`, `fb.style.set` including on
  `button`; `text-align` against the same text with `\\ac`; restart restore.
- **No visual change:** the golden PNGs in `web/src/js/__tests__/golden` are
  the proof that the converted configs render exactly as before. Known
  exception: `btn_a00001-run-main-page2` is stale since an unrelated edit of
  the device's files on 2026-11-17; regenerate that one image **before**
  converting anything (with the current emulator), then no golden may
  change.

## Out of scope

- Backgrounds, borders, padding and radius of buttons and display items;
  `opacity` for anything but the text.
- Inline per-item overrides, a style above the three kinds, styles for
  pages.
- Buttons whose labels follow the shown page, and page cycling / idle return
  as settings: separate specs.
- Changing fonts or adding properties from a script; creating styles from a
  script; persisting script changes.
- Renaming "key" inside the firmware's sources and documents beyond the
  config reserved name `button` and the script call `fb.button`.

## Acceptance

1. The factory config and both device configs, converted, render exactly as
   before (goldens; on the bench device by screenshot).
2. The example at the top works: `windowClose` and `co2` red, `time` at 48
   px and dimmed, `soc` dimmed.
3. From the console: `setStyle`, `setStyle("")`, `addStyle`, `removeStyle`,
   `fb.style.set("alarm", "color", ...)`, `fb.style.set("button",
   "title.color", ...)`; a script restart brings the configured look back.
4. A config with only the three reserved styles: free internal heap, LVGL
   pool and JS heap after boot compared with the committed firmware
   (909014a), figures reported; the target is no more than before.
5. Memory of three extra styles used by six items reported (heap, pool, JS
   heap).
6. An old-format config loads without crashing, logs one warning per
   removed key name, and shows the built-in look.
7. btn_a00001 converted (see "Converting btn_a00001") renders exactly as before, and its measured
   free internal heap, LVGL pool and JS heap on the bench device (with
   btn_a00001's config and script) are reported against today's.

## Decisions and corrections after review

Owner's decisions:

1. btn_a00001: `displayitem` gets `font-size` 24. Six items thereby gain a
   step-1 padding that changes no pixel (none has a caption or an underline).
2. An item lists up to **four** styles (not three). Storage: four configured
   bytes and four current bytes per item, the current ones in one
   `std::atomic<uint32_t>` so they change together.
3. The display item field `iconcolor` is removed. The tint of an icon is
   `icon.color` of the item's styles (last first), else of the kind's style,
   else the main text colour.
4. `addStyle` / `removeStyle` exist on buttons and display items, not on
   lists (a list has `setStyle` only).
5. An item may not name a reserved style (`button`, `displayitem`, `list`):
   warning and ignored in config, `TypeError` from a script, a config
   problem in the IDE.
6. `fb.main.display.create(x, y, width, boxType, fontSize, alignment, ...)`:
   the `fontSize` argument becomes a style string (same position; `""` for
   none). `display.get()` returns `style` (the current list as a string) in
   place of `fontSize`. data/scripts/examples/displays.js is updated.
7. All remaining vendor aliases are removed from the parser: `mqttbrokers`,
   broker `autoDiscovery`, button `topLabel`, button `leds[].topics` in its
   vendor form, `buttonuid`, button `id` as a position fallback, and
   `info.connectors[].connector` written "for backwards compatibility". Keep
   only what the firmware's own current format needs; each removed key is
   reported once like the others. data/config.json is converted
   (`topLabel` -> `toplabel`).
8. Version 1 is committed as the baseline before this work starts, together
   with the regenerated golden of page 2 and new goldens for pages 3, 4 and
   5 of btn_a00001.

Corrections from the review (see the review for the code references):

- **Padding of a display item.** px = the value's resulting `font-size`.
  No padding under 14 px, and none at exactly 14 px unless one of the
  item's own styles sets `font-size` in its main block; else the nearest of
  14 / 24 / 32 / 48 / 72, a tie to the smaller. (This is today's function
  with "the item has a fontsize" replaced by "one of the item's styles sets
  the size".)
- **Unit.** Font and size: the last of the item's styles whose `unit` block
  sets it, else `displayitem.unit` where it sets it, else the value's
  resulting face / px. Colour and the other properties: the item styles'
  `unit` blocks, else `displayitem.unit`, else the built-in `#999999`. A
  main `color` does **not** colour the unit (version 1 did).
- **Selected row.** `selected` blocks go on the roller with
  `LV_PART_SELECTED`, main blocks with `LV_PART_MAIN`. The built-in selected
  row always has `color #000000` and `background-color #ffd700`, and no
  font: a main `color` never colours it, and its font follows the rows
  unless a `selected` block sets one.
- **Titles** do not inherit from main.
- **`text-align`.** 1. The item's `align` field places the box; unchanged.
  2. The text of a part is positioned in the part's width by the last `\a`
  in the text, else by `text-align` of the last of the item's styles that
  sets it for that part, else the kind's. 3. With none, as today: display
  item parts are as wide as their content, button labels are centred, lists
  are left aligned. 4. `text-align` in `unit` and `selected` is ignored, and
  a button label with several formats stays centred (existing behaviour).
  Implementation: `ui_render_text` gets the part's resolved alignment and
  starts from it where it starts from "left, not aligned" today; when a
  `\a` overrides a style's alignment, the span labels get the local
  alignment too.
- **`icon`.** `color` only. Applied as the tint when the icon is drawn (A8
  rasters; a colour raster is not tinted; an MDI glyph in a text is text and
  takes the main `color`). No `lv_style_t` for it: a colour and a flag per
  style that has the block. `fb.style.set(x, "icon.color", ...)` works
  through the existing redraw.
- **`background-color`.** `#rrggbb` also sets full opacity; `#rrggbbaa`
  sets both. On a list's main block it is the whole roller's background.
- **`line-height` and `opacity`.** At most two decimals; stored as integers
  (x100, and 0..255); `style_logic.c` stays integer-only and the callers
  convert. `line-height` 1.25 must give exactly today's `line_height / 4`
  for a list. `line-height` in `selected` has no effect. In this version
  `line-height` applies to lists and button labels only; on a display item
  it is accepted and has no effect.
- **`text-decoration`.** Both word orders of `underline line-through` are
  accepted.
- **`fb.style.set` on the reserved styles** can change every property the
  kind's resolved style holds, built-in or from config (not only those set
  in config). Dotted prefixes: `title.`, `unit.`, `icon.`, `selected.`.
- **Backup.** The first `fb.style.set` after a script start allocates the
  backup (one block, freed at restart); the restore itself allocates
  nothing.
- **JS heap.** Acceptance 4 for the JS heap becomes a budget: at most 350
  bytes more than the committed firmware 909014a, measured and reported.
  Internal heap and LVGL pool: no more than before.
- **Acceptance 6.** An old-format config loads without crashing and logs
  the warnings; items under removed aliases are not shown.
- **Markup.** `\s1`..`\s5` and the title step rule stay in text markup;
  only the config fields and script calls are removed.
- **Fonts.** A font's own `lineheight` (in `fonts`) is unchanged and is not
  the style property `line-height`; the docs explain the difference.
- **`"style"` string.** Names separated by one or more spaces; a duplicate
  name counts once, at its last position. GET /config writes the names
  joined by one space; it writes no `styles` and no `fonts` block (as it
  writes no `style` block today).
- **Attaching.** A style part that holds no LVGL property (for example only
  `font-size`) is not attached. Span labels get only the styles that set
  opacity, spacing, decoration or alignment (font and colour are local on
  the labels already). "Never half a list": when the pool has no room the
  object keeps what it carried and the widget retries, as version 1.
- **Method ids** live in one header that both sources include (added to
  tools/js/emulator/sources.txt). Renumbering is free: ids are created at
  boot and are not in stored bytecode.
- **Messages** say "button": `fb.button() requires a button name`, `unknown
  button`.
- **Removed-key warnings** need a warn-only pass that does not build the
  items it matches.
- **Version 1 leftovers to remove:** the "background only on a list"
  refusal (`style_hw_has_bg`, `style_logic_named_fits`), the gating of the
  script methods, the per-part item fields.
- **The IDE's "add a style to every item of this page"** is deferred.
- **Goldens.** The proof of "renders exactly as before" needs goldens for
  pages 3, 4 and 5 of btn_a00001 (added before converting). Fixture tests
  whose inline configs use removed fields are converted to exact style
  equivalents; a golden may change only if listed and explained.
- **Other documents to update:** docs/MEDIA-STORE.md, web/TODO.md,
  CLAUDE.md (the "backwards compatibility" paragraph), besides those listed
  above. The example scripts under data/ do not use `fb.key` or `setFont`;
  only displays.js changes (decision 6).

Build order: step 1 firmware model, rendering and the three configs (goldens
unchanged); step 2 script API and the device scripts (memory measured here);
step 3 web IDE and documents.

