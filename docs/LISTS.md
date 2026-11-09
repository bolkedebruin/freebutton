# Lists on the main display

A list is a selection list on the main display, drawn as an LVGL roller:
- `rows` rows are visible;
- the selected item is in the middle row, on a rounded bar;
- when the selection moves, the list scrolls in about 200 ms.

The script decides which keys move the list. The list itself only shows
items and the selection.

```json
"lists": [{
  "name": "scenes", "page": 2, "x": 4, "y": 18, "width": 92, "rows": 3, "fontsize": 3,
  "items": ["Bed Time", "Evening"], "selected": 0,
  "color": "808080", "focuscolor": "ffd700", "focustextcolor": "000000"
}]
```

| Field | Default | |
| --- | --- | --- |
| `name` | | Scripts use the name. It follows the rules of `docs/NAMES.md`, and lists have their own names, separate from display items and keys. |
| `page` | 1 | 0 = every page. |
| `x`, `y`, `width` | 0, 0, 50 | Percent of the screen (320 x 240). |
| `rows` | 3 | Visible rows, 1..7. |
| `fontsize` | 3 | 1..5 = 14, 24, 32, 48, 72 px, as for display items. |
| `items` | [] | Up to 32 texts of up to 48 bytes each. The firmware replaces a newline in an item with a space. Plain text and icon glyphs only: colour codes such as `\c` are not read. |
| `selected` | 0 | The item selected at boot. |
| `color` | `808080` | Text colour of the rows that are not selected. |
| `focuscolor` | `ffd700` | Colour of the selection bar (the gold of captions and top labels). |
| `focustextcolor` | `000000` | Text colour of the selected item. |

## Script API

```js
fb.main.list.next("scenes");              // selection one down; returns the new index
fb.main.list.prev("scenes");              // one up (the selection stops at the first and last item)
fb.main.list.select("scenes", 0);         // false if the index is out of range
fb.main.list.getSelected("scenes");       // index, -1 if the list is empty
fb.main.list.count("scenes");
fb.main.list.setItems("scenes", ["Bed Time", "Evening", "Movie"]);
```

- The first argument can also be the list's index in `lists`.
- An unknown name causes a `ReferenceError`. An index that does not exist
  causes a `RangeError`.
- `fb.main.list` exists only when `config.json` has lists. It costs about
  400 B of JS heap: one object, six function objects and their property
  names. Measured on btn_a00001, minus the script functions removed at
  the same time.

## Memory

- The items stay in the general heap, as one string (`"Bed Time\nEvening"`).
- The roller exists only while its page is shown. It is created when the
  page appears and deleted when the page goes away. The roller is its LVGL
  objects, its styles and its own copy of the items.
- Firmware: the roller widget uses about 11 KB of flash.
- Measured on btn_a00001 (3 rows, 32 px font, 2 items):
  - while page 2 is shown, the roller uses 572 B of LVGL memory, and the
    device gets it back when the page closes;
  - the two display items that the list replaced used about 3.4 KB of
    LVGL memory, on every page.
