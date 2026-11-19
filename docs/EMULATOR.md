# Emulator: the script engine in the web IDE

The IDE's virtual device is the firmware in the page. While a script runs
its displays are the pixels the firmware's own LVGL drew; while nothing runs
it shows where the firmware puts each element of `config.json`, as an outline
with the element's name. A script runs on the firmware's engine; keys can be clicked, the displays and LEDs follow, MQTT
goes to the broker of `config.json` under the rules of "MQTT safety model"
(or, when chosen, to a broker in the page).

## Using the virtual device

- **Run** is in the toolbar, after Save and Deploy and a divider, in the
  virtual device's colour: Save and Deploy act on the real device, Run does
  not. It starts `scripts/main.js` with `config.json`: the editor's text for
  the file that is open, unsaved changes included, else the stored file.
  Nothing is sent to the device. While it runs the toolbar has
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
- **MQTT settings** are on that side too, below the values, and can be set
  while nothing runs (they hold for the next Run). The mode: Live publish
  (what a Run uses unless another was chosen), Live read-only or Offline;
  the browser remembers the choice. Per broker of `config.json`, under its
  `brokerid`: the WebSocket address, the user name and the password, each
  empty with `config.json`'s value as placeholder. Save keeps what was
  typed in this browser for that broker's host (two entries with one host
  share it) and, while a script runs, connects again with it; "Reset to
  default" forgets it. An empty field means `config.json`'s value. Next to
  each broker: the state of its connection and, when it does not connect,
  why, with what to do about it; the connection is tried again until it is
  there, and "Use offline" switches to the broker in the page (nothing
  falls back to it by itself). In Live publish: the switches "Send Home
  Assistant discovery", "Send retained" and "Publish the virtual device's
  own states" (all off after a reload), and "Remove announcement" once
  something was announced. The side scrolls inside the face; the page
  stepper is hidden while it is shown.
- **A script runs: the face is the firmware's displays.** The main display
  (320 x 240) and the display of every bar key (160 x 80) are canvases that
  show the pixels the firmware's LVGL drew in the page (`attachCanvas`, "The
  displays" below). The LEDs (front, and the one at the key's back as a glow
  at its outer edge) are drawn by the page from the device state. Without a
  known module layout (no device, no `info.connectors`) the modules are the
  ones the firmware made of the config.
- **Proportions.** The sizes follow the panels (`display-geometry.ts`, one
  place). The main display is a 2.8" panel (its diagonal is about 70 mm,
  measured by the owner): 320 pixels on 57.6 mm, a pitch of 0.180 mm. A
  key's display is a 0.96" panel with a pitch of 0.135 mm. The main display
  is drawn a CSS pixel per device pixel, a key's display at 0.135 / 0.180 =
  0.75 of that: 120 x 60 CSS px, in the middle of its half of the bar, with
  the LED and the dots at the key's outer edge. The whole half of the bar
  takes the press. The face is 416 px wide: the main display is 0.77 of
  the face's width, a proportion read from a photo of the device (the sizes
  in mm would give about 478 px, too wide for the pane). The pane's default
  width fits the face; in a narrower pane the whole face is scaled down (CSS
  `zoom`).
- **Square pixels or smoothing, per display.** A display keeps square pixels
  (`image-rendering: pixelated`) only when one of its device pixels is a
  whole number of screen pixels: the main display on a 1x or 2x screen at
  the face's own size. Otherwise the browser smooths (a scaled face, a 1.25x
  or 1.5x screen, and a key's display on most screens: 0.75 or 1.5 screen
  pixels per device pixel), as text with uneven pixel widths reads worse
  than slightly soft text.
- **Nothing runs: outlines with names.** As soon as the virtual device is on
  screen and a config.json is loaded, the page makes the device from it
  without a script (`Emulator.create`, no `run()`): the idle device. Its
  pixels are not shown. The face shows, on plain dark displays, a dashed
  outline for every element at the rectangle the firmware laid it out in
  (`emu.layout('main')`), with the element's name in it: the `name` of a
  display item, list or image; a display item without one shows
  `#<displayitemid>`, and without that its place in the file
  (`displayitems[2]`); the clock shows `clock`. A key's display is an outline
  with the key's `name`, or `Key <position>`; a position without a configured
  key stays empty. Labels, values, list items, icons and the clock's hands
  are not shown, and the LEDs are off: what the elements show is seen with
  Run. An element the firmware draws nothing of yet (no caption, no value)
  has an outline of the least height (14 px) where it will be. A new idle
  device is made after every change of config.json (the editor's text, 250
  ms after the last key; at once after an edit made on the device) and the
  old one is disposed; while the text is not valid JSON the last layout
  stays. The virtual device says nothing about that text, and nothing about
  unsaved changes: the editor marks a JSON error.
- **The page stepper.** While nothing runs it picks the page of config.json
  whose outlines are shown (the firmware changes page only when a script
  asks: the idle device gets the one-line script
  `fb.main.display.showPage(n)`). While a script runs the page is the
  script's, as on the device, which has no stepper: the stepper shows the
  page the script shows and its buttons are off ("The script decides the
  page while it runs"). The IDE never calls `showPage` on a running device:
  a script that keeps the page in a variable of its own would draw nothing
  on a page it did not choose. After Stop the page looked at before Run is
  shown again.
- **Loading.** The engine (`fb-emulator.wasm`, 322 KB compressed, and 20 KB
  of script) and the media are fetched when the virtual device is first
  drawn, not with the IDE's own code. Until the idle device is there, and
  whenever the firmware cannot start (no WebAssembly, no 2D canvas, the
  engine did not load, no valid config.json yet), the page lays out the same
  outlines itself from the config model (`idle-outline.ts`): the positions
  and widths are config.json's; the height of a display item is not in the
  config (the firmware sizes it by its content) and is estimated from the
  fonts of its styles. While it loads a small "Loading the display engine…"
  shows on the face; when it failed, one line under the face says why.
  Without a config.json the sample's outlines are shown and nothing is
  loaded. A script that runs in a browser without a 2D canvas is drawn by
  the page from the device state (labels and values, approximate text). The
  device serves the wasm under a fixed name with an ETag: a later page load
  asks once and gets "304 Not Modified".
- **Media.** One `MediaCache` for the page (`virtual-media.ts`): fonts and
  rasters are read from the connected device's media store, each bucket
  listed once (the fonts' listing is the one the IDE already makes), a file
  fetched once per ETag (the browser's cache serves `?v=<etag>` afterwards).
  An icon raster the device does not have yet (config.json not deployed, or
  no device) is rendered in the page from the SVG the `icons` table names,
  as an icon sync would upload it. An upload or delete of a font, and every
  raster an icon sync writes, reaches the cache (`MediaCache.set`), so the
  virtual device follows as the real one does. Without a device there are no
  font files: text is in the firmware's built-in fallback font, and a line
  under the face says so (not an error). For work on the IDE itself, the dev
  server (only) reads media from the URL prefix in localStorage
  `fb-dev-media` ("<prefix>fonts/medium.ttf").
- **Keys.** Press a key with the pointer, or Space / Enter on a focused key;
  how long it is held gives click, long press (0.8 s) and repeats, as on the
  device. The left and right half of the main display are its two keys.
- **Editing on the outlines.** While nothing runs, each outline on the main
  display takes the pointer and the focus, with a label such as "Display
  item temp, x 10, y 10". Hover brightens it; the editor cursor's entry has
  the purple outline; a click shows it in config.json; a drag or the arrow
  keys move it; right-click, the menu key, Shift+F10 or a long touch opens
  its properties; right-click on empty space offers "Add text item here".
  During a drag the outline moves with the pointer, with its position next
  to it; config.json is written on drop (one undo step) and the firmware
  then lays it out there (a new idle device, some milliseconds). The clock
  can be shown in config.json and has properties (size and position), but
  is not moved by a drag. A key's box is the key itself.
- **Does it fit the device.** The info side shows LVGL's memory pool as the
  shown device has it (the device's size): free of total with a bar, and the
  least that was free; and the texts the firmware left out or drew without
  their formats because the pool was low, in the warning colour when not
  zero ("The device would drop text here").
- **Save screenshot** (info side): the main display as a PNG at the device's
  resolution, made in the page; the real device's screenshot writes to its
  flash, this one writes nothing anywhere.
- **While a script runs** nothing can be moved or changed on the
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
- **MQTT tab** (after the first Run; the form needs a running script): the
  messages. A line says where MQTT goes (the mode and the connection);
  "Settings" in it shows the back of the virtual device, where the mode,
  the brokers and the switches are (see "MQTT settings" above). Per broker
  the script's subscriptions. Inject a message (topic, payload, retain) as
  if the broker sent it: local in every mode. The log lists, newest first,
  what was injected, what the script received and from where, and for every
  publish where it went: sent, would publish (read-only), held with the
  reason, or to the in-page broker (offline). A publish to a real device's
  topic asks first (Once / For this session / No). Loopback (offline only)
  returns a publish to the script's own subscriptions. Export saves the
  session as a JSON file; Import replays its incoming messages at their
  recorded times. "Sensors" sets the readings of the three default sensors.
- **Without a script too:** the virtual device is the firmware reading
  config.json: config `topics` fill items and key labels, a key press
  publishes its configured topics and the Home Assistant event, sensors
  publish their state every second, and (in a live mode that allows it) the
  device announces itself to Home Assistant.

## What is the firmware's code, what is TypeScript

`web/public/assets/fb-emulator.wasm` (764 KB, 322 KB gzipped; fetched at the
first use only) is built by `tools/js/build_emulator_wasm.sh` from the
firmware's own sources, unchanged, and LVGL 9.6.0 with the firmware's
`src/lv_conf.h`. That is the device itself, down to where hardware begins:

| Sources | What they decide |
|---|---|
| `lib/mquickjs` + generated stdlib | the engine, the built-ins, bytecode (wasm32 = 32-bit, as the ESP32) |
| `src/scripting/mquickjs_stdlib.c` (`js/*.c`), `*_binding.cpp`, `*_hardware.cpp` | every script API with its argument checks, error texts and limits; the `fb` tree; the 2 KB event ring between MQTT and the loop |
| `src/components/` (`ConfigManager`, `DisplayItem`, `Button`, `List`, `ImageItem`, `Clock`, `TopicHandler`, `ButtonEventPlan`, the `*Logic.h`) | reading config.json; config `topics`; text slots and their sizes; what a script restart restores; a key's press topics |
| `src/mqtt/` (`Broker`, `Registry`, `Component`, `HomeAssistantDiscovery`, `HaEntities`, `HaSlug`) | subscriptions and their batches, the birth message and last will, discovery and its pacing |
| `src/drivers/` (`PushButton`, `ButtonManager`, `ButtonLed`, `NeoPixel`, `Sensors`, `HardwareScanner`), `src/core/HardwareInitializer.cpp` | the module scan, click / long press / repeat, the default action of a press, LED state, the sensors and their 1 s sample |
| `src/ui/` (`ui_box.c`, `ui_text.c`, `ui_button.c`, `ui_style.c`, `ui_clock.c`, `MediaImage.cpp`, `fonts/fonts.c`, `fonts/ttf_check.c`, `text_spans.c`, `style_logic.c`, `fonts/font_logic.c`, `box_logic.h`, `media_image_check.c`, `PendingRender.cpp`, `MainPage.cpp`) | every widget: the display item box, a key's display, text in spans, the shared styles, the analog clock, image rasters, the TrueType fonts (tiny_ttf, with the firmware's glyph guard and fallbacks), text markup, the style lookup, pages |
| LVGL 9.6.0 (`.pio/libdeps/esp32/lvgl`, all of `src/`, with `src/lv_conf.h`) | layout, wrapping, clipping, the roller, drawing, the 61440-byte memory pool, timers and animations |

`tools/js/emulator/` is what stands in for the rest: `runner.c` (the script
engine's owner: context, start, restart, the `loop()` pass), `device.cpp`
(`setup()` and the firmware's tasks; the state records), `display.cpp` (the
LVGL display driver, in place of `src/drivers/LvglDisplay.cpp`: see "The
displays"), `media_emu.c` (the media store over the host's files, in place of
`src/media/media_esp.c`), `records.cpp` (JSON from LVGL objects),
`mqtt_client_shim.cpp` (esp-mqtt over the host's transport), `tz.c` (local
time from the POSIX TZ string), `seam_*.cpp` / `seam_*.c` (six firmware files
included as they are, with a wrapper around one function or an accessor to
their private tables: `mqtt_binding.cpp`, `button_hardware.cpp`,
`HomeAssistantDiscovery.cpp`, `fonts.c`, `ui_clock.c`, `ui_box.c`) and `shim/`
headers (Arduino, FreeRTOS, I2C, the port expander, the BME280, FastLED, the
filesystem, the display owner). `tools/js/emulator/lv_conf.h` is
`src/lv_conf.h` plus one override: `LV_ASSERT_HANDLER` traps instead of
`while(1);` (the firmware halts its LVGL task; a page must not hang).
Constants the firmware keeps where C cannot include them are read from their
files by the build script (key scan period, `CONFIG_MQTT_BUFFER_SIZE`,
`loop()`'s delay, the LVGL task's delay, `NUMBER_OF_LEDS`,
`CONFIG_MQTT_EVENT_QUEUE_SIZE`).

Two small changes were made in `src/` for this, without a change of
behaviour: the font and padding rules of a display item box and of a text
span moved from `ui_box.c` / `ui_text.c` into `src/ui/box_logic.h` (pure C,
`test/test_box_logic`), so that the emulator applies the same functions.

TypeScript (`web/src/js/emulator/`) holds results and the outside world:
`wasm.ts` (loader, marshalling), `host.ts` (`EmulatorHost`), `device-state.ts`
(`DeviceState`: the mirror of what the firmware shows, and the virtual
hardware), `media.ts` (`MediaCache`: the fonts and images the firmware
reads), `canvas.ts` (`attachCanvas`), `preview-overlay.ts`, `clock.ts`,
`index.ts` (`Emulator`), and the MQTT side (`mqtt.ts`, `publish-policy.ts`,
the live transport).

## The displays

Each display of the virtual device is drawn by the firmware's LVGL in the
wasm, also for the idle device (the page then uses its layout only and shows
none of its pixels): the main display (320x240) and one per bar key
(160x80), created as
`LvglDisplay::initialize` does (`LV_COLOR_FORMAT_RGB565_SWAPPED`, partial
render mode, a 60-row draw buffer for the main display and one 30-row buffer
shared by the key displays, the default dark theme). The flush converts each
band from the panel's RGB565 to RGBA in a frame buffer per display (307 KB +
51 KB per key display); a 5 or 6 bit channel is widened by repeating its top
bits, so every colour is one of the panel's 65536 and saturated colours stay
saturated. RGBA in the wasm, not RGB565 out of it: a canvas takes it as it
is, and the conversion runs once per flushed pixel instead of once per read.

```ts
const media = new MediaCache({ read: (bucket, key) => fetchBytes(bucket, key) });   // once per page
const emu = await Emulator.create({ config, files, media });   // the configured state is drawn
emu.displays();                        // [{ id: 'main', width: 320, height: 240, ... }, { id: 'key3', position: 3, ... }]
const detach = attachCanvas(emu, 'main', canvas);              // device pixels 1:1, dirty rectangles
emu.on('frame', ({ display, x, y, width, height }) => ...);
emu.readPixels('key3', { x, y, width, height });               // RGBA copy, no drawing
emu.screenshot('main');                // draws what is pending first, as the firmware's screenshot
emu.layout('main');                    // [{ ref, kind, id, x, y, width, height, page, visible }]
emu.on('layout', ({ display, rects }) => ...);
emu.stats();                           // lvglFree, lvglLowest, textSkipped, textCollapsed, wasmMemoryBytes, ...
```

- **One loop, no threads.** The firmware's LVGL task (pending renders, then
  `lv_timer_handler()`) is a step: it runs after every entry into the engine
  and when LVGL says a timer is due (`fbe_display_wait`: a refresh 33 ms
  after a change, an animation step, the clock's hands every 250 ms), never
  sooner than the task's 10 ms. `lv_tick` is the emulator's clock, latched
  once per step: within a step time stands still, so each LVGL timer runs at
  most once and each display is refreshed at most once (at most its whole
  area: 76800 pixels, 12800 per key display). An idle device has no timer.
- **Without `run()`** the displays show the configured state: `create()`
  draws the first frame before it resolves (the design-time preview). With a
  shared `MediaCache`, create and first frame take 3 to 8 ms for the two real
  device configs, so a new emulator per config edit is fast enough; there is
  no `reloadConfig`.
- **Media.** The firmware opens fonts and rasters through its media store
  API (`media_open`); `media_emu.c` copies a file once from the host into the
  wasm and keeps it (tiny_ttf reads the font in place, as from mapped flash).
  A `MediaSource` may answer with a promise: `create()` first starts the
  firmware once on the side (no broker, no events) to learn which files the
  config opens, and waits for them. A file that arrives or changes later
  (`MediaCache.set`) is handled as an upload to the device: fonts are read
  again 1.5 s after the last change, images at the next step. A font file
  that is missing or not a usable TrueType file gets the firmware's own
  fallback (LVGL's built-in 12 px font, `fonts[].loaded: false`). Without the
  `media` option there are no font files, and icon rasters are empty ones of
  the right size (`placeholderMedia`).
- **Memory.** LVGL's pool is the device's (builtin allocator, `LV_MEM_SIZE`
  61440): btn_a00001's config and script leave about 15 KB of it, as on the
  device, and the firmware's low-memory paths run (a text drawn in one
  format, then not at all: `textCollapsed`, `textSkipped`). stb_truetype's
  scratch comes from the general heap as on the device (`lv_tiny_ttf.c` is
  compiled with the same renames as `tools/scripts/render_build.py`), and the
  glyph guard reads `freeHeapBytes`. The wasm memory is 2.9 to 3.5 MB with the
  three repo fonts (1.7 MB) loaded.
- **Layout.** `emu.layout(display)` is `lv_obj_get_coords` of each widget
  after layout, with the `ref` of its config.json entry: outlines, hit tests
  and drag handles need no rule of their own (the outlines of the idle
  device are these rectangles).
- **Input.** Keys only, as before (`press` / `release` / `click`). The
  firmware has no touch or encoder input (no LVGL input device is created).

`display.cpp` replaces `src/drivers/LvglDisplay.cpp`. Reimplemented: display
creation and buffers, the shared key-display buffer hand-over, the theme, the
main screen, `init_fonts()`, `task()`, the pool sample once a second
(`getLvglMemory`), the screenshot (`screenshot()`: `lv_refr_now`, then the
frame buffer). Left out, hardware only: the SPI bus hold and DMA, the mutexes,
the panels' self-heal sweep, backlight and sleep, the boot clear, the
benchmark, the boot progress bar and the Wi-Fi QR, OTA, credentials and device
info modals.

## The seam

The wasm has no Emscripten loader; its 25 imports are the seam
(`tools/js/emulator/fbh.h` and `imports.txt`, checked by the build and by
`emulator-seam.test.ts`):

| `EmulatorHost` group | Imports | What the host is |
|---|---|---|
| `system`, `console`, `files` | `fbh_now_ms`, `fbh_interrupt`, `fbh_device_id`, `fbh_free_heap`, `fbh_console`, `fbh_log`, `fbh_script_error`, `fbh_file_read` | the clock, the console, `/config.json` and the files for `load()` |
| `hardware` | `fbh_i2c_probe`, `fbh_mcp_gpio`, `fbh_sensor_read`, `fbh_led` | the modules on the I2C buses, the key switches, sensor readings, the LED strip |
| `media` | `fbh_media_size`, `fbh_media_read` | the files of the media store: fonts, image rasters |
| `mqtt` | `fbh_mqtt_start`, `fbh_mqtt_publish`, `fbh_mqtt_subscribe`, `fbh_mqtt_unsubscribe` | the transport of each broker; a publish says who made it (`script` / `firmware`) and what it is (`press`, `discovery`, `availability`, `sensor`, `state`) |
| `display` | `fbh_frame`, `fbh_state` | the rectangle of a display LVGL just drew; one JSON record per display item, key, list, image, clock or layout that changed |
| – | `fbh_key_event` | a key event and whether the script prevented its default action |

**The state records.** After every pass `device.cpp` reads the firmware's
components and the LVGL objects they made (`records.cpp`: the labels of a
text with their fonts and colours, an image's source, style values, object
coordinates) and sends what changed. They describe what the pixels show, for
a page that wants text and structure (tests, tooltips, the HTML face). A display item:
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

`bash tools/js/build_emulator_wasm.sh` needs `emcc`, the firmware's JSON
libraries in `.pio/libdeps/native` (`~/.local/bin/pio pkg install -e native`)
and LVGL in `.pio/libdeps/esp32/lvgl` (`~/.local/bin/pio pkg install -e
esp32`; the build stops with that command when it is missing or not the
version `platformio.ini` pins). LVGL's 483 sources are compiled once per
configuration into an archive in `.pio/emulator_wasm_build` (about a minute);
later builds take seconds.
It writes `fb-emulator.wasm`, `fb-emulator.stamp.txt` and
`tools/js/emulator/sources.txt` (every project file a compilation read, from
the compilers' dependency lists); commit all three. The stamp is the stdlib
fingerprint plus a hash of each of those files (both `lv_conf.h` among them)
and the LVGL version.
`python3 tools/js/wasm_stdlib_fingerprint.py --check`, which CI already runs,
fails when the stamp no longer matches (it names the changed files): a change
in any firmware file the emulator compiles needs a rebuild of the wasm. The
version line of `include/config.h` is left out of the hash; `system.version()`
is the version of the wasm's build.

The emulator wasm also compiles (`fbe_compile`). It could replace
`mquickjs.wasm` once the runner has `set_target_rom_base`; until then both
are kept and `compiler.ts` is untouched.

## Limits and known differences from the device

Emulated by the firmware's code: config `topics`, the default action of a
key press, Home Assistant discovery and availability, text markup, styles,
fonts, text slot sizes, clocks, the time zone, the sensors' states, and since
stage 4 the pixels of every display. With the config, script and topic
values of the device's own screenshots (`devices/*/backup/**/screenshot-*.rgb565`),
btn_a00002's main display and both key displays and five of btn_a00001's six
key displays come out identical to the last pixel; the differences seen were
an SVG icon rasterized by another renderer than the IDE's, a key whose state
was not injected, and one glyph the device itself had dropped.

- Heap: 16 KB like the device, bytecode outside it. btn_a00001's script:
  `jsHeapFree` 5324 in the emulator, 5256 on the device.
- A runaway script: the device has no interrupt handler (the loop task
  hangs). The emulator stops the script after `interruptBudgetMs` (2 s) with
  the engine's uncatchable "interrupted", and reports it. An engine trap
  discards the instance; the next run starts a new one.
- Exceptions in timer / MQTT / key handlers are reported (`error` event); the
  device only logs some of them.
- Display: the pixels are LVGL's, but not the panel's: no backlight, no
  sleep, no panel gamma or viewing angle, no tearing; a frame appears at once
  (the device sends a main display refresh in 20-row bands over SPI).
- Display timing: the LVGL task runs when a timer is due instead of every
  10 ms, and time stands still within one step, so an animation has the same
  frames at the same ticks on every machine; on the device a slow refresh
  delays the next one.
- Two LVGL timers sleep while they have nothing to do, where the device lets
  them run: the font reload poll (every 250 ms on the device), and the
  refresh timer of a bar key without an entry in config.json (on the device
  it runs every 33 ms and logs "No draw buffer" each time).
- A failed LVGL assertion (`LV_ASSERT_HANDLER`) halts the device's LVGL task
  forever; here it is an engine trap ("panic"), and the next run starts a new
  engine.
- Screens the firmware draws outside config.json are not there: the boot
  progress bar, the Wi-Fi QR code, the OTA, credentials and device info
  modals.
- The face in the page: the LEDs are drawn by the page (a square in the
  LED's colour, a glow for the one at the back), not by the firmware. The
  boxes for editing are the page's (a place holder for an entry without
  pixels, a least height of 12 px). When the firmware cannot draw, the
  page's own preview is an approximation and says so.
- An idle device shown on another page than 1 has run a one-line script
  (`fb.main.display.showPage(n)`): the firmware has no other way to change
  page. Its displays are otherwise what the device shows at boot.
- A raster deleted from the media store stays drawn until a new device is
  made (the next config edit or Run); an upload or a change is drawn at
  once.
- Icons: the IDE renders an SVG to the raster the device stores; the
  emulator draws the raster it is given. Without the `media` option icons
  take their room and draw nothing, and all text is in the built-in 12 px
  font.
- The records' `align` / `valign` / `spansCut` of a text come from the
  firmware's parser run again on the raw text (LVGL keeps no such fields);
  the runs, fonts, colours, sizes and positions are read from LVGL.
- `device.cpp` still reads private members of the components
  (`#define private public`): which LVGL object belongs to which config
  entry, the text slots of items whose page is not shown (they have no
  widget), list rows, page numbers. LVGL does not know these.
- The general heap is a constant (`freeHeapBytes`, 40000): discovery never
  waits for it, and the font code's glyph guard (a glyph is skipped when the
  heap is too low to rasterize it) only acts when that option is set low.
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

**Run connects to the broker of `config.json`.** The mode of a Run is
`live publish`, with every switch below off, unless the user chose another
mode: the choice (made on the back of the virtual device, running or not) is
kept by the browser (`localStorage` `fb-mqtt-mode`) and holds over Stop, Run
again and a reload. A config without brokers is `offline`. Run again with
the same brokers keeps the connection; Stop or a script that does not start
ends it (a script that does not compile makes none); a config with other
brokers ends it and connects to the new ones. The firmware never hears
"connected" from the in-page broker before the real one: its link is made in
the session's mode, and the connection starts when the script runs.

A connection that fails is tried again, as a device does. Nothing falls
back to the in-page broker by itself: the error is shown with the broker,
and "Use offline" is the user's choice (it is remembered like any other).

| Mode | Connection | A publish of the script or the firmware |
|---|---|---|
| `live publish` | the real broker, as the virtual identity | is sent, with the four exceptions below |
| `live read-only` | the same | is not sent; logged as "would publish" |
| `offline` | the broker in the page | goes to the in-page broker; nothing leaves the page |

In `live publish`, by default:

1. **Retain is cleared.** "Send retained" keeps the flag. A retained message
   stays on the broker for a device that does not exist.
2. **Announcements are not sent**: a publish of kind `discovery` or
   `availability` (the firmware's birth message `status` = `online` at every
   connect, and its discovery configs, about fifty for btn_a00001), or a
   script's publish to `homeassistant/...` or `buttonplus/<id>/status`.
   Never by itself: `"hassdiscovery": true` in `config.json` makes the
   firmware try, and it is held like the rest. "Send Home Assistant
   discovery" lets them
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

**The last will.** The connection has one only while "Send Home Assistant
discovery" is on: the
firmware's (`buttonplus/<id>/status` = `offline`), retained only with "Send
retained". Without it there is no will, so a virtual device that
vanishes leaves no `offline` behind. Switching it makes the
connection again (the old one is ended first: the same client id twice
would be a take-over and the broker would publish the will), and the
firmware then sends its birth message and discovery under the new rule.
"Remove announcement" switches discovery off, clears every announcement
topic sent (the birth message included) and reconnects without the will.

The three switches and what was allowed for real devices are kept in memory
only: they can be set before Run and hold over Stop and Run again, and a
reload has them all off. Leaving `live publish` clears them.
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
the mode can change while a script runs; the link decides. What
`config.json` says of a broker, what the browser keeps over it and the
remembered mode are `web/src/js/virtual-broker.ts`: it has no imports, so
the back of the virtual device uses it without loading the emulator.

### The live connection

MQTT over WebSocket with MQTT.js (npm `mqtt`; `mqtt-live.ts` is the part
that is the emulator's): MQTT 3.1.1, client id = the virtual identity, a
clean session, keepalive 30 s, the user name and password of the broker
entry in `config.json` (or the ones the browser keeps for its host), a will
only as above. MQTT.js makes no subscription
again after a reconnect (`resubscribe: false`): the firmware does when it
hears "connected", as on the device, so each is made once. Its own
reconnect timer is off; the pause between attempts doubles from 1 s to
30 s. The library (104 KB compressed) is fetched at the first live
connection (the first Run of a config with brokers, unless Offline was
chosen), not with the emulator.

The broker needs a websockets listener. Mosquitto (`mosquitto.conf`):

    listener 9001
    protocol websockets

The address, per broker, in this order:

1. what was saved for the broker's host on the back of the virtual device
   (`localStorage` `freebutton.virtualBroker:<host>` = `{url, username,
   password}`; an address set with an earlier version of the IDE,
   `fb-mqtt-ws:<host>`, is still read);
2. the `url` of the broker entry when it is `ws://` or `wss://` already;
3. `ws://<host of the url>:<wsport>` when the entry has a `wsport` above 0;
4. `ws://<host of the url>:9001` (the port of Mosquitto's examples; Home
   Assistant's Mosquitto add-on listens on 1884: set `"wsport": 1884`).

The user name and the password are the entry's, unless that record has
others; an empty field of the record means the entry's. They are kept as
the device's access key is: in this browser's local storage, readable by
anything that runs in this page's origin. Saving another address, user name
or password while a script runs makes the connection again.
A broker in Docker needs a fixed port mapping for the listener (for example
`-p 9001:9001`): with a dynamic mapping the host port changes at every
restart of the container and the address has to be set again. The IDE
served over http can use `ws://`; from an https page the browser blocks
`ws://` (the back of the virtual device says so; use `wss://`).

What is shown, with the broker on the back of the virtual device, when it
does not connect: no WebSocket at the address (refused, unreachable: most
likely no websockets listener, with the two lines above for the port of the
address), a WebSocket that is not MQTT, the user name or password refused,
the client id refused, the https case, and "another client uses this client
id" (the virtual device running in another tab). An unreachable broker and a
connection that ended are tried again; a refusal (user name or password,
client id, the https case, an address that is none) is not, until the
settings change.

## Stages

1. **Engine, API, mock broker, tests, and the UI (below): done.**
2. **The firmware itself in the wasm: config `topics`, key default actions,
   discovery and availability (`origin: 'firmware'`, with a kind), text
   behaviour, clocks, the time zone, sensors: done.**
3. **A WebSocket transport and the modes `live read-only` / `live publish`:
   done** (see "MQTT safety model"). **Run connects by default (`live
   publish`), the mode is remembered, the connection settings per broker
   are on the back of the virtual device: done.**
4. **The firmware's LVGL in the wasm: every display as the device draws it,
   frames, layout rectangles, media, golden images: done.**
5. **The face in the page is those displays (canvases) while a script runs,
   and the firmware's layout as outlines with names while nothing runs;
   editing on the outlines; the media from the device: done** (see
   "Using the virtual device").
