# Text styles: config.json `"style"`

`"style"` sets the fonts and the text look of the UI, like a `<style>` block in
the `<head>` of an HTML page. It holds the font table and the text styles of
two roles and seven elements. Items do not need it: their own fields (`font`,
`fontsize`, ...) still work and take precedence.

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
| 4. item | item fields (`font`/`fontsize`, `topfont`/`topfontsize`, `labelfont`/`labelfontsize`, list `color`/`focuscolor`/`focustextcolor`), JS `setFont`, `\f{}` `\s{}` `\c` markup | local styles (`lv_obj_set_style_*`) |

- Layers 1 to 3 are resolved once at boot, when config.json is loaded
  (`src/ui/style_logic.c`). The result fills one static `lv_style_t` per
  element (`src/ui/ui_style.c`). Every key, display item and list adds that
  shared style, so the style costs no memory per object.
- Layer 4 is a local style on the object. A local style beats an added
  style in LVGL, so an item field always wins over `"style"`.
- A config change reboots the device, so the style is never changed at run
  time. JS `setFont(..., "")` returns an item to its config fields, and
  without those to the style.

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
| `bg_color` | string | `"#rrggbb"` or `"rrggbb"`; `list.selected` only | `bg_color` |
| `bg_opa` | integer | 0..255; `list.selected` only | `bg_opa` |

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

Item fields keep their meaning. They are local styles over the style.

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
- **A font without a size**, or a size without a font, takes the other
  from the style.
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
