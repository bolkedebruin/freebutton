# Lists on the main display

A list is a selection list on the main display, drawn as an LVGL roller:
- `rows` rows are visible;
- the selected item is in the middle row, on a rounded bar;
- when the selection moves, the list scrolls in about 200 ms.

The script decides which buttons move the list. The list itself only shows
items and the selection.

```json
{
  "styles": {
    "list":   {"font-size": 32, "color": "#808080",
               "selected": {"color": "#000000", "background-color": "#ffd700"}},
    "roomy":  {"line-height": 1.5},
    "picked": {"selected": {"background-color": "#00c853"}}
  },
  "lists": [{
    "name": "scenes", "page": 2, "x": 4, "y": 18, "width": 92, "rows": 3,
    "items": ["Bed Time", "Evening"], "selected": 0, "style": "roomy picked"
  }]
}
```

| Field | Default | |
| --- | --- | --- |
| `name` | | Scripts use the name. It follows the rules of `docs/NAMES.md`, and lists have their own names, separate from display items and buttons. |
| `page` | 1 | 0 = every page. |
| `x`, `y`, `width` | 0, 0, 50 | Percent of the screen (320 x 240). |
| `rows` | 3 | Visible rows, 1..7. |
| `items` | [] | Up to 32 texts of up to 48 bytes each. The firmware replaces a newline in an item with a space. Plain text and icon glyphs only: colour codes such as `\c` are not read. |
| `selected` | 0 | The item selected at boot. |
| `style` | | Up to four style names, separated by spaces (`docs/STYLE.md`). A later name wins. |

## The look of a list

A list has no font or colour fields of its own. Its look comes from its
`style` field and from the reserved style `list` (`docs/STYLE.md`).

| Block of a style | Part of the list |
| --- | --- |
| main (the style itself) | the rows |
| `selected` | the selected row |

The built-in look, without `styles`:

| Part | Font | Size | `color` | Other |
| --- | --- | --- | --- | --- |
| the rows | `montserrat` | 32 px | `#808080` | `line-height` 1.25 |
| the selected row | the rows' | the rows' | `#000000` | `background-color` `#ffd700` (the selection bar); `opacity`, `letter-spacing` and `text-decoration`: the rows' |

- **`list`** changes the look of every list. A style that a list names in
  `style` changes that list only.
- **`font-family`, `font-size`.** The selected row has the font and the
  size of the rows, unless a `selected` block sets one. Face and size are
  taken separately.
- **`color`.** A main `color` colours the rows that are not selected. It
  does not colour the selected row: that row has its own `color`.
- **`opacity`, `letter-spacing`, `text-decoration`.** The selected row has
  the rows' value of each of these three, unless a `selected` block sets
  it. This is an exception: the parts of a style do not inherit from each
  other (`docs/STYLE.md`, Styles and parts). An underline on the rows
  shows on the selected row too. To have a selected row without it, set
  `"selected": {"text-decoration": "none"}`. For `opacity` use
  `"selected": {"opacity": 1}`, for `letter-spacing`
  `"selected": {"letter-spacing": 0}`.
- **`line-height`** (main block) is the space between the rows, as a
  multiple of the font's line height. 1.25 gives a quarter of a line
  between the rows. In `selected` it has no effect.
- **`background-color`.** In the main block it is the background of the
  whole roller. In `selected` it is the colour of the selection bar.
  `"#rrggbb"` is opaque, `"#rrggbbaa"` has an opacity.
- **`text-align`** in the main block places the rows, the selected row
  included: `"left"` (the default), `"center"` or `"right"`. In `selected`
  it is ignored.
- `title`, `unit` and `icon` blocks are not used by a list.

## Script API

```js
fb.main.list.next("scenes");              // selection one down; returns the new index
fb.main.list.prev("scenes");              // one up (the selection stops at the first and last item)
fb.main.list.select("scenes", 0);         // false if the index is out of range
fb.main.list.getSelected("scenes");       // index, -1 if the list is empty
fb.main.list.count("scenes");
fb.main.list.setItems("scenes", ["Bed Time", "Evening", "Movie"]);
fb.main.list.setStyle("scenes", "picked"); // the list's styles; "" = the styles of config.json again
```

- The first argument can also be the list's index in `lists`.
- An unknown name causes a `ReferenceError`. An index that does not exist
  causes a `RangeError`.
- `setStyle` takes a string of up to four style names and returns
  `undefined`. A list has no `addStyle` and no `removeStyle`. The errors
  are in `docs/STYLE.md`, Script API.
- `fb.style.set("list", "selected.background-color", "#0000ff")` changes
  the selection bar of every list (`docs/STYLE.md`). `fb.style.set` can
  also add a property that the style does not set in config.json, for
  example `fb.style.set("list", "text-decoration", "underline")`.
- A script cannot set a font on a list. Give the list a style that sets
  `font-family` or `font-size`.
- `fb.main.list` exists only when `config.json` has lists.

## Memory

- The items stay in the general heap, as one string (`"Bed Time\nEvening"`).
- The roller exists only while its page is shown. It is created when the
  page appears and deleted when the page goes away. The roller is its LVGL
  objects, its styles and its own copy of the items.
- Firmware: the roller widget uses about 11 KB of flash.
- Measured on btn_a00001 (3 rows, 32 px font, 2 items), before styles:
  - while page 2 is shown, the roller uses 572 B of LVGL memory, and the
    device gets it back when the page closes;
  - the two display items that the list replaced used about 3.4 KB of
    LVGL memory, on every page.
