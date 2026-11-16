# Emulator: the script engine in the web IDE

The IDE's virtual device can run the user's script in the page: keys can be
clicked, the display and LEDs follow, MQTT works against an in-page broker.
Stage 1 is the engine, its TypeScript API and tests. It draws nothing and
talks to no device and no broker.

## Using the virtual device

- **Run** is in the toolbar, after Save and Deploy and a divider, in the
  virtual device's colour: Save and Deploy act on the real device, Run does
  not. It starts `scripts/main.js` with `config.json`: the editor's text for
  the file that is open, unsaved changes included, else the stored file.
  Nothing is sent to the device. The engine (`fb-emulator.wasm`) is fetched
  at the first Run, not when the IDE loads. While it runs the toolbar has
  Run again and Stop. Run needs a `scripts/main.js` (open or stored).
  Keys: Ctrl/Cmd+Shift+Enter runs (again), Ctrl/Cmd+Shift+. stops.
- **It runs: the face glows.** While a script runs the border of the
  virtual device's face glows softly in purple (a slow pulse; steady with
  "reduce motion"), and the toolbar shows Stop. There is no status line: an
  error is in the Virtual console (its tab gets a red dot) and marked in the
  editor, and the toolbar's Run shows a red dot when the script stopped with
  one. When the script or `config.json` changed since Run, a note says so
  for a moment and Run again keeps an orange dot.
- **The info side** (the (i) button) is about the virtual device, not the
  real one: its id (`virt_btn_` + 6 hex digits, made once per browser and
  kept in its local storage; `system.id()` gives it to the script), its
  state and the last error, how long the script runs, the script's free JS
  heap of the device's 16 KB, the bytecode size against what the device
  stores, timers and MQTT subscriptions in use against the firmware's
  limits, lost MQTT events, the MQTT mode, the brokers of `config.json`,
  the number of sensors, the modules it emulates, and the model and
  firmware version the engine was built from. Before the first Run it says
  what Run will use (the editor's text or the stored file). It is measured
  once a second while that side is shown; nothing is asked of the real
  device, whose information is in the Device view.
- **Keys.** Press a key with the pointer, or Space / Enter on a focused key;
  how long it is held gives click, long press (0.8 s) and repeats, as on the
  device. The left and right half of the main display are its two keys. The
  key's label, its LED (and the LED at its back, drawn as a glow at the key's
  outer edge), the display items, lists, images and the shown page follow the
  script.
- **What is drawn while it runs** is what the firmware resolved, not the
  design-time preview's own reading of config.json: every text as runs in
  the font, size and colour the firmware chose (markup, style, `setFont`),
  cut to the firmware's slot sizes (value 58 bytes, caption and unit 26, a
  key's labels 90); a key's display with its icon; the list as the
  firmware's roller; images by the raster it draws; the analog clock at the
  device's local time (`core.timezone`, else the firmware's default). Still
  approximate until LVGL is in the wasm (stage 4): line wrapping and
  clipping are the browser's, glyph metrics are the web fonts', the clock's
  face is a plain one, and an image that is not an icon is a labelled box.
- **The first Run** fetches the engine (167 KB compressed, served slowly by
  a device): "Loading the virtual device…" shows on the face meanwhile.
- **Editing.** While a script runs nothing can be moved or changed on the
  virtual device; Stop first (a try to drag or right-click says so for a
  moment). What a script changes is never written to
  `config.json`. An edit of `config.json` (or of the script) in the editor
  does not restart what runs: the note "config.json changed: Run again"
  appears and the device keeps showing the config it was started with.
- **Consoles.** There are two, and they share nothing. **Device** is the
  real device's REPL and what the IDE reports about the device and its files
  (saved, deployed, storage, images, fonts). **Virtual** is the script in the
  page: its `print()` output, its errors, and the lines typed there, which
  are evaluated in it. Each is a tab with its own output, input, input
  history and Clear; every line names its console (`ConsoleLine.to`). The
  Virtual and MQTT tabs appear at the first Run and stay. After Stop the
  virtual output can still be read and its input is off; the next Run adds a
  separator line and keeps what was there. A dot on a tab means it got lines
  while another tab was shown (red: an error among them). Run shows the
  Virtual tab, Deploy the Device tab. An error with a position is also
  marked in the editor when `scripts/main.js` is open.
- **MQTT tab** (after the first Run; the form needs a running script). Mode:
  Offline (the broker in the page, the mode at every start), Live read-only
  or Live publish (the real broker of `config.json`, over its WebSocket
  address: see "MQTT safety model"). Per broker: its WebSocket address, the
  state of the connection and the script's subscriptions. Inject a message
  (topic, payload, retain) as if the broker sent it: local in every mode.
  The log lists, newest first, what was injected, what the script received
  and from where, and for every publish where it went: to the in-page
  broker, sent, would publish (read-only), or held with the reason.
  Loopback (offline) returns a publish to the script's own subscriptions.
  Export saves the session as a JSON file; Import replays its incoming
  messages at their recorded times. "Sensors" sets the readings of the three
  default sensors. While live, the toolbar and the MQTT tab show "read" or,
  in the warning colour, "live", and in Live publish the glow of the face
  is orange.
- **Without a script too:** the virtual device is the firmware reading
  config.json: config `topics` fill items and key labels, a key press
  publishes its configured topics and the Home Assistant event, sensors
  publish their state every second, and (in a live mode that allows it) the
  device announces itself to Home Assistant.

## What is the firmware's code, what is TypeScript

`web/public/assets/fb-emulator.wasm` (379 KB, 167 KB gzipped; fetched at the
first Run only) is built by `tools/js/build_emulator_wasm.sh` from the
firmware's own sources, unchanged. Since stage 2 that is the device itself,
down to where hardware or LVGL begins:

| Sources | What they decide |
|---|---|
| `lib/mquickjs` + generated stdlib | the engine, the built-ins, bytecode (wasm32 = 32-bit, as the ESP32) |
| `src/scripting/mquickjs_stdlib.c` (`js/*.c`), `*_binding.cpp`, `*_hardware.cpp` | every script API with its argument checks, error texts and limits; the `fb` tree; the 2 KB event ring between MQTT and the loop |
| `src/components/` (`ConfigManager`, `DisplayItem`, `Button`, `List`, `ImageItem`, `Clock`, `TopicHandler`, `ButtonEventPlan`, the `*Logic.h`) | reading config.json; config `topics`; text slots and their sizes; what a script restart restores; a key's press topics |
| `src/mqtt/` (`Broker`, `Registry`, `Component`, `HomeAssistantDiscovery`, `HaEntities`, `HaSlug`) | subscriptions and their batches, the birth message and last will, discovery and its pacing |
| `src/drivers/` (`PushButton`, `ButtonManager`, `ButtonLed`, `NeoPixel`, `Sensors`, `HardwareScanner`), `src/core/HardwareInitializer.cpp` | the module scan, click / long press / repeat, the default action of a press, LED state, the sensors and their 1 s sample |
| `src/ui/` (`text_spans.c`, `style_logic.c`, `fonts/font_logic.c`, `box_logic.h`, `media_image_check.c`, `PendingRender.cpp`, `MainPage.cpp`) | text markup, the style cascade, the font table and fallbacks, the fonts and paddings of a box, pages |

`tools/js/emulator/` is what stands in for the rest: `runner.c` (the script
engine's owner: context, start, restart, the `loop()` pass), `device.cpp`
(`setup()` and the firmware's tasks; the state records), `ui_shim.cpp` (the
widgets: `ui_box`, `ui_button`, the clock, roller and image, without LVGL),
`mqtt_client_shim.cpp` (esp-mqtt over the host's transport), `tz.c` (local
time from the POSIX TZ string), `seam_*.cpp` (three firmware files included
with a wrapper around one function each) and `shim/` headers (Arduino,
FreeRTOS, I2C, the port expander, the BME280, FastLED, the filesystem).
Constants the firmware keeps where C cannot include them are read from their
files by the build script (key scan period, `CONFIG_MQTT_BUFFER_SIZE`,
`loop()`'s delay, `NUMBER_OF_LEDS`, `CONFIG_MQTT_EVENT_QUEUE_SIZE`).

Two small changes were made in `src/` for this, without a change of
behaviour: the font and padding rules of a display item box and of a text
span moved from `ui_box.c` / `ui_text.c` into `src/ui/box_logic.h` (pure C,
`test/test_box_logic`), so that the emulator applies the same functions.

TypeScript (`web/src/js/emulator/`) holds results and the outside world:
`wasm.ts` (loader, marshalling), `host.ts` (`EmulatorHost`), `device-state.ts`
(`DeviceState`: the mirror of what the firmware shows, and the virtual
hardware), `preview-overlay.ts`, `clock.ts`, `index.ts` (`Emulator`), and the
MQTT side (`mqtt.ts`, `publish-policy.ts`, the live transport).

## The seam

The wasm has no Emscripten loader; its 23 imports are the seam
(`tools/js/emulator/fbh.h` and `imports.txt`, checked by the build and by
`emulator-seam.test.ts`):

| `EmulatorHost` group | Imports | What the host is |
|---|---|---|
| `system`, `console`, `files` | `fbh_now_ms`, `fbh_interrupt`, `fbh_device_id`, `fbh_free_heap`, `fbh_console`, `fbh_log`, `fbh_script_error`, `fbh_file_read` | the clock, the console, `/config.json` and the files for `load()` |
| `hardware` | `fbh_i2c_probe`, `fbh_mcp_gpio`, `fbh_sensor_read`, `fbh_led`, `fbh_media_exists` | the modules on the I2C buses, the key switches, sensor readings, the LED strip, which rasters exist |
| `mqtt` | `fbh_mqtt_start`, `fbh_mqtt_publish`, `fbh_mqtt_subscribe`, `fbh_mqtt_unsubscribe` | the transport of each broker; a publish says who made it (`script` / `firmware`) and what it is (`press`, `discovery`, `availability`, `sensor`, `state`) |
| `display` | `fbh_state` | one JSON record per display item, key, list, image, clock that changed, with what the firmware decided to show |
| – | `fbh_key_event` | a key event and whether the script prevented its default action |

**The state records.** After every pass `device.cpp` reads the firmware's
components and the widgets they drew and sends what changed. A display item:
its config fields, the texts it holds, and `box` (null while its page is not
shown: the device keeps no widget then) with pixel position, paddings, and
caption / value / unit as runs `{text, font, face, px, color}`. A key: label
and top label as runs, the icon raster; `display: null` for a key without a
display of its own. `DeviceState` keeps the latest of each and gives every
entry the `ref` of `config-model.ts` (by its place in the file, or its name),
which is all it reads from config.json besides the modules.

**The virtual hardware.** `connectors` (config.json `info.connectors`, an
option, or inferred from the keys) decide which port expanders answer on the
I2C bus; the firmware's scanner finds the modules from that. `sensors`
(default: a BME280) decide which sensor addresses answer.

## Build and CI

`bash tools/js/build_emulator_wasm.sh` needs `emcc` and the firmware's JSON
libraries in `.pio/libdeps/native` (`~/.local/bin/pio pkg install -e native`).
It writes `fb-emulator.wasm`, `fb-emulator.stamp.txt` and
`tools/js/emulator/sources.txt` (every project file a compilation read, from
the compilers' dependency lists); commit all three. The stamp is the stdlib
fingerprint plus a hash of each of those files.
`python3 tools/js/wasm_stdlib_fingerprint.py --check`, which CI already runs,
fails when the stamp no longer matches (it names the changed files): a change
in any firmware file the emulator compiles needs a rebuild of the wasm. The
version line of `include/config.h` is left out of the hash; `system.version()`
is the version of the wasm's build.

The emulator wasm also compiles (`fbe_compile`). It could replace
`mquickjs.wasm` once the runner has `set_target_rom_base`; until then both
are kept and `compiler.ts` is untouched.

## Limits and known differences from the device

Emulated since stage 2, by the firmware's code: config `topics`, the default
action of a key press, Home Assistant discovery and availability, text
markup, styles, fonts, text slot sizes, clocks, the time zone, the sensors'
states.

- Heap: 16 KB like the device, bytecode outside it. btn_a00001's script:
  `jsHeapFree` 5324 in the emulator, 5256 on the device.
- A runaway script: the device has no interrupt handler (the loop task
  hangs). The emulator stops the script after `interruptBudgetMs` (2 s) with
  the engine's uncatchable "interrupted", and reports it. An engine trap
  discards the instance; the next run starts a new one.
- Exceptions in timer / MQTT / key handlers are reported (`error` event); the
  device only logs some of them.
- No LVGL (stage 4). The widgets are records, not pixels: no wrapping,
  clipping or flex layout, no glyph metrics (a font's `lineHeight` is an
  estimate), and the pool is never short, so the firmware's low-memory
  fallbacks (a text drawn in one format, a widget left out) never happen.
  `ui_shim.cpp` mirrors which runs `ui_text.c`, `ui_box.c` and `ui_button.c`
  draw; it has to follow them until LVGL is compiled in.
- Rasters: an icon or image is its key in the images bucket; every key is
  taken to exist (option `mediaExists`). Fonts are names; a missing TTF file
  is not noticed.
- The general heap is a constant (`freeHeapBytes`): nothing fails for lack of
  it, discovery never waits for it.
- MQTT: no outbox and no network delay; a publish while disconnected is
  dropped. A message larger than the in-buffer is dropped by the firmware as
  on the device; the size of the PUBLISH packet is computed for QoS 0.
- A display item or key label set and overwritten within one script turn
  shows only its last text, as on the device (the LVGL task draws after the
  script's turn); a widget made and removed within one turn never shows.
- LEDs: the strip's pixels as the firmware wrote them; effects ("glow") do
  not run. The factory button, WiFi, the web server, OTA, S3 and the screens
  they draw are not part of the virtual device.
- Time is the page's clock from the start (no NTP wait). `millis()` starts
  at 0 when the emulator is made.
- Sensors: the readings of the hardware sensors are what `setSensor()` gives
  (a BME280 by default); the STS35 driver's moving average and the memory
  sensors are the firmware's.
- Bytecode compiled for a device (`romBase` set) cannot be run: pass source.

## MQTT safety model

Identity: `virt_btn_<6 hex>`, made once per browser; `Emulator.create`
refuses anything else, so `system.id()` and every topic built from it are
virtual. It is also the MQTT client id of a live connection: a client id in
`config.json` is not used.

The mode is chosen in the MQTT tab while a script runs. It is never stored:
every start is `offline`. Run again with the same brokers keeps the mode and
the connection; Stop, a script that does not start, a reload, or a config
with other brokers ends the connection and is `offline` again.

| Mode | Connection | A publish of the script or the firmware |
|---|---|---|
| `offline` | the broker in the page | goes to the in-page broker; nothing leaves the page |
| `live read-only` | the real broker, as the virtual identity | is not sent; logged as "would publish" |
| `live publish` | the same | is sent, with the four exceptions below |

In `live publish`, by default:

1. **Retain is cleared.** "Send retained" keeps the flag. A retained message
   stays on the broker for a device that does not exist.
2. **Announcements are not sent**: a publish of kind `discovery` or
   `availability` (the firmware's birth message `status` = `online` at every
   connect, and its discovery configs, about fifty for btn_a00001), or a
   script's publish to `homeassistant/...` or `buttonplus/<id>/status`. "Announce to Home Assistant" lets them
   through (unretained unless "Send retained" is on too). Every announcement
   topic sent is remembered for the session; "Remove announcement" publishes
   an empty retained message to each, which removes the entities from Home
   Assistant and the retained configs from the broker.
3. **A real device's topic asks first.** A topic that names a device id
   (`btn_` + 6 hex as a token, e.g. `buttonplus/btn_a00001/cover/set`;
   not `virt_btn_...`) is held, and the IDE asks: Once / For this session /
   No. "No" holds for that topic for the session; while the question is open
   more publishes to the topic are dropped, not queued. Topics that name the
   virtual identity as a token (what the firmware builds from its id, what a
   script builds from `system.id()`) and plain topics are not asked about.

4. **The device's own states are not sent**: what the firmware reports by
   itself under the virtual identity, kind `sensor` or `state` (eight
   sensors, once a second). "Publish the virtual device's own states" lets
   them through. They are harmless and noisy.

What is sent without any switch: what the script publishes, and a key's own
press topics (kind `press`: the Home Assistant event of the key, and the
`topics` of the key in config.json), under rules 1 to 3. A press topic under
the virtual identity reaches nothing real; one that names a real device asks
first, like a script's. A command on the virtual device's own
`button_N/set` presses the key, as on the device.

**The last will.** The connection has one only while "Announce" is on: the
firmware's (`buttonplus/<id>/status` = `offline`), retained only with "Send
retained". Without "Announce" there is no will, so a virtual device that
vanishes leaves no `offline` behind. Switching "Announce" makes the
connection again (the old one is ended first: the same client id twice
would be a take-over and the broker would publish the will), and the
firmware then sends its birth message and discovery under the new rule.
"Remove announcement" switches "Announce" off, clears every announcement
topic sent (the birth message included) and reconnects without the will.

Leaving `live publish` clears the switches and what was allowed.
Injected messages are local in every mode (the real broker does not get
them). The firmware's limits (8 subscriptions per broker, the size of a
message) hold in the live modes: the wasm applies them before a subscribe
or publish reaches the transport and to every message handed in.

**The log.** Newest first, 200 rows at most. The firmware's periodic and
bulk publishes are collapsed: one row per topic for `sensor` and `state`,
one row for all of `discovery`, each with a count and the latest payload,
updated where it stands (nothing moves while it counts) and expandable to
the payloads before. Filters: All / Script / Firmware, and In / Out.

`decidePublish()` (pure, `publish-policy.ts`) makes each decision;
`BrokerLink` (`broker-link.ts`) is the transport the Emulator gets: it
holds the in-page broker and, live, the connection; `MqttSession`
(`live-session.ts`) holds the mode, what is allowed, and the log. The
Emulator itself is created permissive (`live-publish`, all allowed) so that
the mode can change while a script runs; the link decides.

### The live connection

MQTT over WebSocket with MQTT.js (npm `mqtt`; `mqtt-live.ts` is the part
that is the emulator's): MQTT 3.1.1, client id = the virtual identity, a
clean session, keepalive 30 s, the user name and password of the broker
entry in `config.json`, a will only as above. MQTT.js makes no subscription
again after a reconnect (`resubscribe: false`): the firmware does when it
hears "connected", as on the device, so each is made once. Its own
reconnect timer is off; the pause between attempts doubles from 1 s to
30 s. The library (104 KB compressed) is fetched when a live mode is first
chosen, not with the emulator.

The broker needs a websockets listener. Mosquitto (`mosquitto.conf`):

    listener 9001
    protocol websockets

The address is a setting per broker in the MQTT tab, by default
`ws://<host of the broker url>:9001`; the browser keeps it per broker host.
A broker in Docker needs a fixed port mapping for the listener (for example
`-p 9001:9001`): with a dynamic mapping the host port changes at every
restart of the container and the address has to be set again. The IDE
served over http can use `ws://`; from an https page the browser blocks
`ws://` (the tab says so; use `wss://`).

What is shown when it does not connect: no WebSocket at the address
(refused, unreachable: most likely no websockets listener, with the two
lines above), a WebSocket that is not MQTT, the user name or password
refused, the client id refused, the https case, and "another client uses
this client id" (the virtual device running in another tab).

## Stages

1. **Engine, API, mock broker, tests, and the UI (below): done.**
2. **The firmware itself in the wasm: config `topics`, key default actions,
   discovery and availability (`origin: 'firmware'`, with a kind), text
   behaviour, clocks, the time zone, sensors: done** (the preview does not
   draw the resolved runs and the clock yet: see the hand-over in
   `preview-overlay.ts`).
3. **A WebSocket transport and the modes `live read-only` / `live publish`:
   done** (see "MQTT safety model").
4. Pixel-exact drawing with the firmware's LVGL in wasm, on the same records.
