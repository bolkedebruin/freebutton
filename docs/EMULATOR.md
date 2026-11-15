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
- **MQTT tab** (after the first Run; the form needs a running script). The brokers of
  `config.json` are brokers in the page. Per broker: the script's
  subscriptions. Inject a message (topic, payload, retain) as if another
  client published it. The log lists what was injected, what the script
  received and what it published, newest first, with what happened to it
  ("not sent: offline"). Loopback returns a publish to the script's own
  subscriptions. Export saves the session as a JSON file; Import replays its
  incoming messages at their recorded times. "Sensors" sets the readings of
  the three default sensors.
- **Not there yet:** `load()` of other files, config `topics` and the key's
  default MQTT actions (stage 2), a real broker (stage 3).

## What is the firmware's code, what is TypeScript

`web/public/assets/fb-emulator.wasm` (206 KB, 89 KB gzipped) is built by
`tools/js/build_emulator_wasm.sh` from the firmware's own sources, unchanged:

| Source | What it decides |
|---|---|
| `lib/mquickjs` + generated stdlib | the engine, the built-ins, bytecode (wasm32 = 32-bit, as the ESP32) |
| `src/scripting/mquickjs_stdlib.c` (`js/*.c`) | every script API: argument checks, error texts, timers (16), subscription table (8 per broker, 2 brokers), topic matching |
| `src/scripting/device_hardware.cpp` | the `fb` tree from the module topology |
| `src/scripting/mqtt_binding.cpp`, `src/mqtt/Registry.cpp`, `Component.cpp`, `utils/MessageRing.h` | the 2 KB event ring between MQTT and the loop, 8 events per pass, the drop counter |
| `src/scripting/sensor_binding.cpp` | sensor info, the 0.1 change threshold of `onChange` |
| `src/drivers/PushButton.cpp` | click / long press (40 ticks) / repeat (25 ticks) from the key scan |
| `components/IconLogic.h`, `ListLogic.h`, `include/config.h` | icon name and image key rules, list limits and selection, `FIRMWARE`, `MODEL`, `BUTTON_COUNT` |

`tools/js/emulator/` adds the runner (`runner.c`: `ScriptEngine`'s context,
start, restart and `loop()` pass), the seam (`seam.cpp`, `seam_mqtt.cpp`) and
`shim/` headers for what is not there (FreeRTOS, esp-mqtt, LVGL, Arduino,
`ConfigManager`, the hardware scanner). Four constants the firmware keeps
where C cannot include them are read from their files by the build script:
key scan period (`ButtonManager.h`, 20 ms), `CONFIG_MQTT_BUFFER_SIZE` (512),
`loop()`'s `delay(10)`, `NUMBER_OF_LEDS`.

TypeScript (`web/src/js/emulator/`) only holds state and time:
`wasm.ts` (loader, marshalling), `host.ts` (`EmulatorHost`), `device-state.ts`
(`DeviceState`), `mqtt.ts` (`MqttTransport`, `MockBroker`), `publish-policy.ts`,
`clock.ts`, `preview-overlay.ts`, `index.ts` (`Emulator`).

## The seam

The wasm has no Emscripten loader; its 61 imports are the seam
(`tools/js/emulator/imports.txt`, checked by the build and by
`emulator-seam.test.ts`). `EmulatorHost` has one group per header:

| Group | Header | Under its C name (TypeScript) | Rule first, in the wasm |
|---|---|---|---|
| `keys` | `button_hardware.h` | set/get label and top label, set/restore font, find by name | count, callback registry, dispatch, `set_icon` (icon name) |
| `leds` | `led_hardware.h` | `set_color` | – |
| `display` | `displayitem_binding.h` | create, remove, find, count, `get_info` (struct), all ids, label / value / unit, font, position, page, show page | `set_icon` |
| `lists` | `list_binding.h` | find, set/restore font | count, selected, move, select, items builder + commit |
| `images` | `image_binding.h` | find | exists, `set_icon`, `set_src` |
| `fonts` | `ui/fonts/fonts.h` | `fonts_find`, `fonts_find_id` | – |
| `mqtt` | `mqtt_binding.h` | – | all of it; the host is `MqttBroker`'s client: publish, subscribe, unsubscribe |
| `sensors` | `sensor_binding.h` | – | all of it; the host gives the sample |
| `system` | `system_binding.h` | – | version, model; the host gives id, name, uptime, free heap, clock |
| `files` | `file_hardware.h` | `file_hw_load` (for `load()`) | – |
| `topology` | `device_hardware.h` | – | connector types from the host |
| `console` | – | `print()` text, log lines, script errors | – |

Structs written through pointers are laid out in `layout.ts`; a test compares
them with `fbe_layout()` (`offsetof` / `sizeof` as the wasm was compiled).

**Adapter.** `DeviceState` takes key, list and image entries, names,
connectors and the font table from `config-model.ts` / `font-config.ts`
(imported, not changed). Added in `device-state.ts`, because the preview model
leaves them out: display item ids as the firmware assigns them, `boxtype`,
`round`, the raw `page` (default 1), both spellings of a list in file order.

## Build and CI

`bash tools/js/build_emulator_wasm.sh` (needs `emcc`) writes
`fb-emulator.wasm` and `fb-emulator.stamp.txt`; commit both. The stamp is the
stdlib fingerprint plus a hash of every source compiled in.
`python3 tools/js/wasm_stdlib_fingerprint.py --check`, which CI already runs,
now also fails when the stamp no longer matches the sources (it names the
changed files). The version line of `include/config.h` is left out of the
hash; `system.version()` is the version of the wasm's build.

The emulator wasm also compiles (`fbe_compile`). It could replace
`mquickjs.wasm` once the runner has `set_target_rom_base`; until then both
are kept and `compiler.ts` is untouched.

## Limits and known differences from the device

- Heap: 16 KB like the device, bytecode outside it. btn_a00001's script:
  `jsHeapFree` 5324 in the emulator, 5256 on the device.
- A runaway script: the device has no interrupt handler (the loop task
  hangs). The emulator stops the script after `interruptBudgetMs` (2 s) with
  the engine's uncatchable "interrupted", and reports it. An engine trap
  discards the instance; the next run starts a new one.
- Exceptions in timer / MQTT / key handlers are reported (`error` event); the
  device only logs some of them.
- Config `topics` on items and keys (the firmware's own subscriptions), the
  default action of a key press (configured topics, Home Assistant event),
  Home Assistant discovery and clocks are not emulated. A `key` event says
  whether the script prevented the default.
- Local time is UTC (no `core.timezone`); `system.freeHeap()` is a constant;
  text slot sizes (a key label is 26 bytes on the device) are not applied.
- Sensors: three BME280 values by default; `setSensor()` is heard at the next
  1 s sample. Font lookup uses the IDE's `font-config.ts`, not `font_logic.c`.
  Bytecode compiled for a device (`romBase` set) cannot be run: pass source.

## MQTT safety model

Identity: `virt_btn_<6 hex>`, random per emulator; `Emulator.create` refuses
anything else, so `system.id()` and every topic built from it are virtual.
Every publish goes through `decidePublish()` (pure, `publish-policy.ts`):

- `offline` (stage 1, default): only `MockBroker` transports are accepted.
- `live-readonly`: every publish is blocked and logged as "would publish".
- `live-publish`: the real device's id in a topic is rewritten to the
  virtual one; discovery and availability topics are blocked unless
  `allowDiscovery`; a topic outside the virtual identity needs an
  `allowTopics` entry; retain is cleared unless `allowRetain`.

## Stages

1. **Engine, API, mock broker, tests, and the UI (below): done.**
2. The firmware's default behaviour: config `topics`, key default actions,
   discovery (through the policy, `origin: 'firmware'`).
3. A WebSocket `MqttTransport` (client id = the virtual identity, clean
   session, no last will); modes `live-readonly` / `live-publish`.
4. Pixel-exact drawing with the firmware's LVGL in wasm, on the same records.
