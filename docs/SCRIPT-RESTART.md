# Script restart: what goes back to config.json

A new script can start without a reboot. Before it runs, the firmware calls
`ConfigManager::restoreConfigState()` (main task, script engine lock held),
which puts what a script can change back to the state after boot.

## Restored

| State | Script API | After the restart |
|---|---|---|
| Display items a script made | `fb.main.display.create` | removed; `create` returns the same ids as after boot |
| Config display items a script removed | `remove` | shown again, at their place in the config order |
| Display item value, caption, unit | `setValue`, `setLabel`, `setUnit` | the config text (value: empty) |
| Display item icon | `setIcon` | the config `icon` (none: hidden) |
| Display item position, page | `setPosition`, `setPage` | the config values |
| Shown page | `showPage` | page 1 |
| Button label, top label, icon | `fb.button(name).display.*` | the config values |
| List items, selection | `fb.main.list.*` | the config `items`, `selected` |
| Styles of a button, a display item, a list | `setStyle`, `addStyle`, `removeStyle` | the list of the config `style` field (none: no style of its own) |
| Values of a style | `fb.style.set` | the values of config.json `styles`; for `button`, `displayitem` and `list` also the built-in values. A property that `fb.style.set` added is not set any more |
| Image | `fb.main.image.setIcon` / `setSrc` | the config `icon` / `src` |
| LEDs | `led` / `backLed` `.setColor`, `.on`, `.off` | off (the firmware's own LED state) |

## Kept

Texts and icons that arrived on an item's or button's own config `topics` are
not script state and stay. Each text remembers its last writer:

- A script wrote last: the text goes back to the config default. If the
  field also has a config topic, the topic's value shows again only at its
  next message (a retained message is not delivered again).
- The config topic wrote last: the text stays.

Clocks and the fonts (config.json `fonts`) are not reachable from a script.
Of the styles (`docs/STYLE.md`) a script reaches two things: which styles
an item uses, and the values of the properties of a style (not
`font-family`, not `font-size`, not `line-height`). `fb.style.set` can
also add a property that the style does not set in config.json. All of it
goes back at a restart: an added property is no longer set.

## Limits

- A removed config item stays in memory until the restart (about 250 bytes
  and its subscriptions); `remove` frees memory only for items a script made.
- A script and a config topic that write the same field at the same moment
  can leave the "last writer" wrong once; the next message or restart
  corrects it.
- An image keeps its config value in a small heap block from the script's
  first `setIcon` / `setSrc`. If that allocation fails (logged), the image
  shows nothing after a restart, until a reboot.
- The styles keep their config values in one heap block from the script's
  first `fb.style.set` until the restart: 152 bytes with only the reserved
  styles, 20 bytes more per block and 4 bytes more per style of `styles`.
  If that allocation fails the call throws and changes nothing. The
  restore itself allocates nothing. A property that a script added takes
  5 bytes of the LVGL pool until the restart.
- Handlers, timers and MQTT subscriptions of the script are not part of
  this function (see `ScriptEngine`).
- MQTT after a restart: the new script's subscriptions are sent to the
  broker again, so it gets the retained messages again. Not for a topic
  that a config item or button also subscribes to (that subscription never
  left the broker): there the script gets the next message only.
- `mqtt.onConnect` handlers of the new script do not run for a broker that
  is already connected.
- A script that loads more bytecode with `load()` leaves one small mapping
  behind per restart.
