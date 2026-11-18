# Text styles: config.json `"style"`

`"style"` sets the fonts and the text look of the UI, like a `<style>` block in
the `<head>` of an HTML page. It holds the font table, the text styles of
two roles and seven elements, and the named styles. Items do not need it:
their own fields (`font`, `fontsize`, ...) still work and take precedence.

```json
"style": {
  "fonts": [
    {"id": 1, "name": "mdi", "src": "icons.ttf"},
    {"name": "montserrat",          "fallback": "mdi"},
    {"name": "montserrat_semibold", "fallback": "mdi"}
  ],
  "text":  {"text_font": "montserrat",          "text_color": "#ffffff"},
  "title": {"text_font": "montserrat_semibold", "text_color": "#ffd700"},
  "key":     {"label": {"text_size": 32}, "toplabel": {"text_size": 18}},
  "display": {"value": {"text_size": 14}, "unit": {"text_color": "#999999"},
              "caption": {"text_size": 14}},
  "list":    {"text_size": 32, "text_color": "#808080",
              "selected": {"bg_color": "#ffd700", "text_color": "#000000"}}
}
```

This is the factory `data/config.json` block. It gives the same look as a
config without `"style"`, plus the MDI icons as the fallback of the system
fonts.

## The cascade

The style works like LVGL's own styles. Each layer sets some properties. A
property that a layer does not set comes from the layer below it.

| Layer (lowest first) | config.json | LVGL |
|---|---|---|
| 1. firmware defaults | none (the table below) | the theme layer |
| 2. role | `style.text`, `style.title` | resolved into the element's style |
| 3. element | `style.key.label`, `style.display.caption`, ... | one shared `lv_style_t` per element, added to its objects |
| 3a. named style | `style.styles.<name>`, chosen per part by an item field (`style`, `topstyle`, `labelstyle`, `focusstyle`) or by a script | one shared `lv_style_t` per named style, added to the part's objects after the element style |
| 4. item | item fields (`font`/`fontsize`, `topfont`/`topfontsize`, `labelfont`/`labelfontsize`, list `color`/`focuscolor`/`focustextcolor`), JS `setFont`, `\f{}` `\s{}` `\c` markup | local styles (`lv_obj_set_style_*`) |

- Layers 1 to 3 are resolved once at boot, when config.json is loaded
  (`src/ui/style_logic.c`). The result fills one static `lv_style_t` per
  element (`src/ui/ui_style.c`). Every key, display item and list adds that
  shared style, so the style costs no memory per object.
- Layer 3a is optional: see [Named styles](#named-styles). Among added
  styles the one added last wins in LVGL, so a named style wins over the
  element style. A property the named style does not set comes from the
  element style.
- Layer 4 is a local style on the object. A local style beats an added
  style in LVGL, so an item field always wins over `"style"`, a named
  style included.
- A config change reboots the device, so layers 1 to 3 are never changed at
  run time. A script can change layer 3a: which named style a part uses,
  and the values of a named style (`fb.style.set`). JS `setFont(..., "")`
  returns an item to its config fields, and without those to the style.

### Firmware defaults (layer 1)

| Element | Font | Size | Colour |
|---|---|---|---|
| `key.label` | `montserrat` | 32 | `#ffffff` |
| `key.toplabel` | `montserrat_semibold` | 18 | `#ffd700` |
| `display.value` | `montserrat` | 14 | `#ffffff` |
| `display.unit` | the value's | the value's | `#999999` |
| `display.caption` | `montserrat_semibold` | 14 | `#ffd700` |
| `list` | `montserrat` | 32 | `#808080` |
| `list.selected` | the list's | the list's | `#000000` on `bg_color` `#ffd700` |

The key labels are centred and the list is left aligned, unless `text_align`
is set.

### Roles and elements

| Role | Elements |
|---|---|
| `text` | `key.label`, `display.value`, `display.unit`, `list` (and `list.selected` through the list) |
| `title` | `key.toplabel`, `display.caption` |

| Element | Object | LVGL selector |
|---|---|---|
| `key.label` | the key's bottom label | `LV_PART_MAIN` |
| `key.toplabel` | the key's top label | `LV_PART_MAIN` |
| `display.value` | the value of a display item | `LV_PART_MAIN` |
| `display.unit` | the unit of a display item | `LV_PART_MAIN` |
| `display.caption` | the caption (`label`) of a display item | `LV_PART_MAIN` |
| `list` | the roller of a list | `LV_PART_MAIN` |
| `list.selected` | the selected row of a list | `LV_PART_SELECTED` |

Two exceptions follow LVGL's own inheritance:

- **`display.unit`** has no font or size of its own. It uses the value's
  font and size, after the item layer, unless `style.display.unit` sets
  `text_font` or `text_size` itself. The `text` role does not give the unit
  a font or size.
- **`list.selected`** starts from the resolved list. In LVGL a text
  property of the `SELECTED` part that is not set falls back to the `MAIN`
  part. So an item font on the list also reaches the selected row, unless
  `list.selected` sets its own font.

## Properties

The property names are LVGL's style property names. `text_size` is the one
extension.

| Property | Type | Values | LVGL |
|---|---|---|---|
| `text_font` | string or integer | a font name, or the `id` of a `style.fonts` entry | `text_font` (with `text_size`) |
| `text_size` | integer | px, 6..72 | none: part of `text_font` |
| `text_color` | string | `"#rrggbb"` or `"rrggbb"` | `text_color` |
| `text_opa` | integer | 0..255 | `text_opa` |
| `text_align` | string | `"left"`, `"center"`, `"right"` | `text_align` |
| `text_letter_space` | integer | px, -100..100 | `text_letter_space` |
| `text_line_space` | integer | px, -100..100 | `text_line_space` |
| `text_decor` | string | `"none"`, `"underline"`, `"strikethrough"`, `"underline strikethrough"` | `text_decor` |
| `bg_color` | string | `"#rrggbb"` or `"rrggbb"`; `list.selected` and named styles only | `bg_color` |
| `bg_opa` | integer | 0..255; `list.selected` and named styles only | `bg_opa` |

- **`text_size`.** In LVGL the size is part of the font: a TrueType font
  at one pixel size is one `lv_font_t`. The cascade therefore resolves
  `text_font` and `text_size` separately and then makes one font instance,
  `fonts_get(face, px)`. A role can give the font and an element the size.
- **`text_line_space` on a list.** Without it the list keeps a quarter of
  the line height between rows, as before `"style"`.
- **`text_align` on display items.** Display text is positioned by the item
  `align` and the `\a` markup. `text_align` acts only inside one label.
- **Errors.** An unknown key, an unknown property, a bad value or an
  unknown font is logged as a warning and ignored. The rest of the style
  still applies.

## Named styles

`style.styles` defines styles with a name. An item refers to one per part.
A script can give a part another named style, take it away, and change a
value of a named style, which restyles every part that uses it.

```json
"style": {
  "styles": {
    "alarm":  {"text_color": "#ff0000"},
    "muted":  {"text_color": "#808080"},
    "big":    {"text_font": "montserrat_semibold", "text_size": 48},
    "picked": {"bg_color": "#00c853", "text_color": "#000000"}
  }
},
"buttons":      [{"position": 3, "label": "Light", "style": "big", "topstyle": "muted"}],
"displayitems": [{"name": "co2", "label": "CO2", "unit": "ppm", "style": "alarm"}],
"lists":        [{"name": "scenes", "items": ["Morning", "Night"], "focusstyle": "picked"}]
```

- **Names.** `[A-Za-z_][A-Za-z0-9_]{0,23}`, case sensitive. A config holds
  at most 16 named styles.
- **Properties.** The property table above, with the same names, types and
  ranges. A named style is flat: it has no parts and no nested blocks. A
  style without properties (`{}`) is valid and counts as one of the 16.
- **`bg_color` and `bg_opa`** are allowed in a named style. A style that
  sets one of them is for a list only (`style` or `focusstyle`). Keys and
  display items have no background of their own.
- **Errors.** A bad name, a style that is not an object and every style
  after the 16th are logged as a warning and skipped. An unknown property
  or a bad value is logged and ignored; the rest of the style applies. The
  rest of the config still loads. The web IDE reports the same problems.
- A config without `style.styles`, or with an empty one, has no named
  styles. It uses no memory for them, and scripts have no `fb.style` and
  no `setStyle` methods.

### Item fields

| Item | Field | Element |
|---|---|---|
| key (`buttons`) | `style` | `key.label` |
| key (`buttons`) | `topstyle` | `key.toplabel` |
| display item | `style` | `display.value` and `display.unit` |
| display item | `labelstyle` | `display.caption` |
| list | `style` | `list` |
| list | `focusstyle` | `list.selected` |

- The value is the name of a `style.styles` entry. `""` or no field: the
  part has no named style.
- A field that names no existing style, a value that is not a string, and
  a style with `bg_color` or `bg_opa` on a key or a display item are logged
  as a warning and ignored. The item looks as without the field.
- **Display items.** `style` is added to the value and to the unit. A
  named `text_color` therefore colours the unit too.
- **Lists.** The item's `color`, `focuscolor` and `focustextcolor` stay
  local and win. A named `text_color` on `style` does not colour the
  selected row, because `list.selected` sets its own. A named
  `text_line_space` replaces the quarter-line default of the list.
- `GET /config` returns the six item fields by name. It returns no `style`
  block, as before.

### Fonts with a named style

In LVGL the size is part of the font, so the font of a part is resolved
from all layers together:

| | First that sets it |
|---|---|
| font | the item field (`font`, `topfont`, `labelfont`), else the named style's `text_font`, else the element style |
| size | the item `fontsize` (the steps and the title rule, as in [Item fields](#item-fields-layer-4)), else the named style's `text_size` (px), else the element style |

- **The unit of a display item:** the item's `font` / `fontsize`, else the
  named style's, else `style.display.unit` where it sets one, else the
  value's.
- **Padding of a display item.** A named `text_size` takes the place of
  the element style's size in the padding rule below. An item `fontsize`
  works as before.
- A named style that sets both `text_font` and `text_size` holds its font
  in its shared style. A named style that sets one of the two makes the
  part resolve its font locally, like an item field does.
- **Font instances.** The device holds at most 24 font instances (a font
  at one size) and never frees one. Named styles add to them as follows:
  - A named style that sets both `text_font` and `text_size` makes one
    instance at boot, used or not.
  - A named style that sets only `text_font` or only `text_size` makes no
    instance by itself. It makes one for each different font that its
    users end up with: the style's half together with the other half from
    each user's item fields or element style. Three parts with three
    different sizes that use one style with only `text_font` make three
    instances.
  - The selected row of a list (`focusstyle`) with only one of the two
    takes the other half from the rows of that list: their item `font` /
    `fontsize` (or a script's `setFont`), else the rows' named style, else
    `style.list`. Where `style.list.selected` sets a font or size of its
    own, that comes before the rows' font.

### Script API

The calls exist only when config.json has at least one named style. On a
device without named styles a script gets the engine's "not a function"
error, and `typeof fb.style` is `"undefined"`.

| Call | Returns |
|---|---|
| `fb.key(k).display.setStyle(style)` | undefined |
| `fb.key(k).display.setTopStyle(style)` | undefined |
| `fb.main.display.setStyle(idOrName, style)` | boolean: false for an unknown numeric id |
| `fb.main.display.setLabelStyle(idOrName, style)` | boolean: false for an unknown numeric id |
| `fb.main.list.setStyle(nameOrIndex, style)` | undefined |
| `fb.main.list.setFocusStyle(nameOrIndex, style)` | undefined |
| `fb.style.set(style, property, value)` | undefined |

- **`style`** is the name of a `style.styles` entry. `""` takes the named
  style away from the part: the part then has none. To get the configured
  one back, name it.
- **`fb.style.set`** changes the value of a property that the style sets
  in config.json. `value` is a string or a number as in config.json. Every
  key, display item and list that uses the style is redrawn with the new
  value, text with markup included.
- **`fb.style.set` waits.** Each call waits for the display task, which
  can be busy for 80 to 150 ms while it draws. Several calls in one event
  handler add up, and a key handler does not see the next press during
  that time. Change one property per event, or swap styles with
  `setStyle`, which does not wait.
- A display item that a script created (`display.create`) takes a style
  like any other.
- Changes are not saved. A script restart gives every part its configured
  named style and every named style its config values
  (`docs/SCRIPT-RESTART.md`).

| Error | Thrown for |
|---|---|
| `ReferenceError` | a style name that `style.styles` does not have; an unknown item or list name, as for `setFont` |
| `TypeError` | a style that is not a string |
| `TypeError` | a style that sets `bg_color` or `bg_opa`, on a key or a display item |
| `TypeError` | `fb.style.set`: a property the style does not set in config.json |
| `TypeError` | `fb.style.set`: `text_font` or `text_size` |
| `TypeError` | `fb.style.set`: an unknown property, or a bad value |
| `TypeError` | `fb.style.set("", ...)`: `""` names no style |
| `TypeError` | `fb.style.set`: a value that is not a string or a number (`true`, `null`, an object) |
| `TypeError` | `fb.style.set`: a property or a string value with a NUL character in it |
| `InternalError` (out of memory) | the first `fb.style.set` after a script start, when the device has no heap to keep the config values (24 bytes per named style); nothing is changed |
| `InternalError` | `setStyle` / `setTopStyle` on a key that config.json `buttons` does not list |

`fb.style.set` cannot add a property to a style and cannot change its font
or size: both would cost memory at run time. To change the font or size of
a part from a script, give it another named style.

```js
fb.main.display.setStyle("co2", "alarm");       // the value and unit are red
fb.main.display.setStyle("co2", "");            // no named style again
fb.style.set("alarm", "text_color", "#00ff00"); // everything that uses "alarm" is green
```

### Memory

| What | Cost |
|---|---|
| a named style | 40 bytes of heap, plus 1 byte and the length of its name; all named styles are one allocation |
| a property of a named style | 5 bytes of the LVGL pool |
| an object that a named style is added to | 8 bytes of the LVGL pool |
| a part of a key or a display item | 2 bytes in the item (the configured style and the current one) |
| a part of a list | 3 bytes in the list (the configured style, the current one and the one the roller carries) |
| a named style that sets both `text_font` and `text_size` | one font instance in the LVGL pool, from boot |
| `fb.style.set`, from the first call until the script restarts | 24 bytes of heap per named style, in one allocation |

- A name is stored once. An item stores an index, never a name and never
  a copy of the style.
- The script methods and `fb.style` use JS heap once at boot, and only when
  the config has named styles. A call allocates nothing on the JS heap.
- A part is more than one LVGL object: a display item part is its
  container and one label per span of the text.
- A style is added to an existing object only when the LVGL pool has room.
  Without room the device logs a warning once, and the part shows without
  the named style: also without its font and size, so that the part never
  shows half a style. The device tries again a fraction of a second later,
  and keeps trying until the style is there.
- A named style that the LVGL pool has no room for at boot is dropped. One
  log line names the dropped styles. A dropped style does not exist for
  the rest of the run: an item that names it logs "unknown style" and
  looks as without the field, and the script calls throw `ReferenceError`
  for it. When every named style is dropped, the script calls do not
  exist.

Measured on the bench device btn_a00002 on 2026-11-18, after boot:

| Config | Internal heap | LVGL pool | JS heap |
|---|---|---|---|
| no named styles, against the firmware before named styles | the same within the noise of the readings | the same within the noise of the readings (16,698 against 16,708 bytes free) | identical |
| three named styles on six parts, none with a font | about 140 bytes (calculated: 40 bytes per style plus the names) | 142 bytes | 436 bytes |
| the same plus `big`, a style with a 48 px font | about 190 bytes (calculated) | 522 bytes | 436 bytes |

- The three named styles are `alarm`, `muted` and `picked`. The six parts
  are on two display items, two keys and one list.
- The 436 bytes of JS heap are the seven methods and `fb.style`. The
  device makes them once at boot, whatever the number of styles.
- `big` sets both `text_font` and `text_size`, so it creates a font
  instance: 380 bytes of the LVGL pool for this font at this size.

## Fonts

`style.fonts` is the font table (`docs/MEDIA-STORE.md`, Fonts). Each entry has
a `name` (`[A-Za-z_][A-Za-z0-9_]{0,23}`), an optional `id` (an integer >= 1),
a `src` (a key in the bucket `fonts`), an optional `fallback` (a font name
or id) and an optional `lineheight` (0.8..2.0 em). Up to 6 entries add fonts.

- **Fallbacks are explicit.** A font without `"fallback"` has none. A glyph
  that no font in the chain has is drawn by LVGL's built-in Montserrat 12,
  the end of every chain. No font name is special in the firmware.
- **MDI icons.** Give the system fonts a fallback to an icon font, as in
  the example above. Without it, MDI codepoints (`\u{F0599}`) in labels
  show as boxes. The web IDE warns about this.

### System fonts

Two fonts always exist, with or without a config entry:

| Name | File in bucket `fonts` |
|---|---|
| `montserrat` | `medium.ttf` |
| `montserrat_semibold` | `semibold.ttf` |

- **Reserved names.** A `style.fonts` entry with a system font's name may
  set only `id`, `fallback` and `lineheight`. A `src` there is logged and
  ignored. To use another font for the UI, set `text_font` in the style;
  the system fonts themselves are not replaced.
- **Protected files.** An S3 DELETE of `medium.ttf` or `semibold.ttf` is
  refused (403). A PUT is allowed (checked by `ttf_check` like any font),
  so the files can be updated. If a file is missing anyway, for example
  after a full erase, its font draws with LVGL's built-in Montserrat 12.
- **Firmware screens.** The modals (device info, credentials, Wi-Fi QR,
  OTA), the progress bar and the LVGL theme use the compiled font
  (`LV_FONT_DEFAULT`, Montserrat 12), not the style. A bitmap font draws
  a text-heavy modal at once; with a TTF every glyph is rasterized on every
  draw (~0.6-1 ms each). They stay legible whatever the config says.
- `icons.ttf` is an ordinary font file. It is shipped and flashed with the
  firmware, but it can be deleted and it has no special meaning.

## Item fields (layer 4)

Item fields keep their meaning. They are local styles over the style and
over the item's named style.

| Item | Fields | Element |
|---|---|---|
| key (`buttons`) | `font`, `fontsize` | `key.label` |
| key (`buttons`) | `topfont`, `topfontsize` | `key.toplabel` |
| display item | `font`, `fontsize` | `display.value` and `display.unit` |
| display item | `labelfont`, `labelfontsize` | `display.caption` |
| list | `font`, `fontsize` | `list` |
| list | `color` | `list` (`text_color`) |
| list | `focuscolor`, `focustextcolor` | `list.selected` (`bg_color`, `text_color`) |

- **`fontsize`.** 1..5 are the legacy steps: 14, 24, 32, 48 and 72 px. On a
  title element (`key.toplabel`, `display.caption`) step 2 is 18 px. 6..72
  are px. An absent field uses the style's size.
- **Padding of a display item.** The space under the caption and the
  value follows the value's size: the step itself, or for a size in px the
  nearest step (`"fontsize": 14` = step 1, 24 = step 2, ...). Two cases
  have no such padding: a size under 14 px, and an item without
  `fontsize` whose 14 px comes from the style.
- **A font without a size**, or a size without a font, takes the other
  from the item's named style, and without one from the style.
- **Markup** (`\f{name}`, `\s{px}`, `\s1`..`\s5`, `\c`) applies per span, on
  top of everything else.

## Examples

**Another UI font.** Upload `inter.ttf` to the bucket `fonts`, then:

```json
"style": {
  "fonts": [
    {"id": 1, "name": "mdi", "src": "icons.ttf"},
    {"name": "inter", "src": "inter.ttf", "fallback": "mdi"}
  ],
  "text":  {"text_font": "inter"},
  "title": {"text_font": "inter"}
}
```

Every key, display item and list now uses Inter, at the default sizes and
colours. Items with their own `font` keep it.

**A bigger key label.**

```json
"style": {"key": {"label": {"text_size": 40}}}
```

All key labels are 40 px. A key with `"fontsize": 2` stays at 24 px.

**Green titles.**

```json
"style": {"title": {"text_color": "#00c853"}}
```

The key top labels and the display captions are green. The selected list
row keeps its gold bar, because it is set by `list.selected` and not by the
`title` role.

**No style.** Without `"style"` the device uses the firmware defaults above:
the same look, with system fonts that have no fallback (MDI codepoints
show as boxes).
