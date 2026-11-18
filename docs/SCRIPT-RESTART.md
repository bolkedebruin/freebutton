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
| Display item font, position, page | `setFont`, `setPosition`, `setPage` | the config values |
| Shown page | `showPage` | page 1 |
| Key label, top label, icon, fonts | `fb.key(n).display.*` | the config values |
| List items, selection, font | `fb.main.list.*` | the config `items`, `selected`, font |
| Named style of a part | `setStyle`, `setTopStyle`, `setLabelStyle`, `setFocusStyle` | the config `style`, `topstyle`, `labelstyle`, `focusstyle` (none: no named style) |
| Values of a named style | `fb.style.set` | the values of config.json `style.styles` |
| Image | `fb.main.image.setIcon` / `setSrc` | the config `icon` / `src` |
| LEDs | `led` / `backLed` `.setColor`, `.on`, `.off` | off (the firmware's own LED state) |

## Kept

Texts and icons that arrived on an item's or key's own config `topics` are
not script state and stay. Each text remembers its last writer:

- A script wrote last: the text goes back to the config default. If the
  field also has a config topic, the topic's value shows again only at its
  next message (a retained message is not delivered again).
- The config topic wrote last: the text stays.

Clocks, the fonts, the roles and the elements of the style are not reachable
from a script. Of the style a script reaches only the named styles
(`style.styles`): which one a part uses, and the values of the properties a
named style sets in config.json (not `text_font`, not `text_size`).

## Limits

- A removed config item stays in memory until the restart (about 250 bytes
  and its subscriptions); `remove` frees memory only for items a script made.
- A script and a config topic that write the same field at the same moment
  can leave the "last writer" wrong once; the next message or restart
  corrects it.
- An image keeps its config value in a small heap block from the script's
  first `setIcon` / `setSrc`. If that allocation fails (logged), the image
  shows nothing after a restart, until a reboot.
- The named styles keep their config values in one heap block (24 bytes
  per named style) from the script's first `fb.style.set` until the
  restart. If that allocation fails the call throws and changes nothing.
- Handlers, timers and MQTT subscriptions of the script are not part of
  this function (see `ScriptEngine`).
- MQTT after a restart: the new script's subscriptions are sent to the
  broker again, so it gets the retained messages again. Not for a topic
  that a config item or key also subscribes to (that subscription never
  left the broker): there the script gets the next message only.
- `mqtt.onConnect` handlers of the new script do not run for a broker that
  is already connected.
- A script that loads more bytecode with `load()` leaves one small mapping
  behind per restart.
