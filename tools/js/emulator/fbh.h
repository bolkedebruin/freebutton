//
// fbh.h - what fb-emulator.wasm imports from its host (wasm module "env").
//
// The device above this line is the firmware's own code: the
// script bindings, ConfigManager and the components, the MQTT brokers, Home
// Assistant discovery, the hardware scan. What is left for the host is the
// outside world:
//
//   - time, console, log, files
//   - the virtual hardware: which I2C addresses answer, the port expanders'
//     pins (module signature, key switches), sensor readings, the LED strip
//   - the MQTT transport of each broker
//   - the media store's files (fonts, image rasters)
//   - the displays: LVGL draws them in the wasm (display.cpp); the host is
//     told which part of a frame buffer changed (fbh_frame) and gets one
//     record per widget with what it shows and where (fbh_state, JSON;
//     records.cpp writes them)
//
// The host is web/src/js/emulator/wasm.ts. Strings are pointer + length into
// the wasm memory unless the name says otherwise; the host copies what it
// keeps.
//
#ifndef FB_EMULATOR_FBH_H
#define FB_EMULATOR_FBH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FBH_IMPORT(name) __attribute__((import_module("env"), import_name(#name)))

// --- time, console, runaway scripts -----------------------------------------
// Milliseconds since the Unix epoch on the emulator's clock.
FBH_IMPORT(fbh_now_ms) double fbh_now_ms(void);
// print() / console.log() text, as the firmware sends it to the IDE console.
FBH_IMPORT(fbh_console) void fbh_console(const char* text, size_t len);
// A firmware log line (ESP_LOGx: level 'E' 'W' 'I') or engine output ('J').
FBH_IMPORT(fbh_log) void fbh_log(int level, const char* tag, const char* text, size_t len);
// Polled by the engine while script code runs: nonzero stops the script.
FBH_IMPORT(fbh_interrupt) int fbh_interrupt(void);

// --- identity, heap, files ----------------------------------------------------
// The device id ("virt_btn_xxxxxx") written into buf (at most cap bytes, no
// terminator); returns the number of bytes. Read once, when the module starts.
FBH_IMPORT(fbh_device_id) size_t fbh_device_id(char* buf, size_t cap);
// Free bytes of the general heap (system.freeHeap(), the free-memory sensor).
FBH_IMPORT(fbh_free_heap) uint32_t fbh_free_heap(void);
// A file of the device's filesystem ("/config.json", files for load()): a
// block from fbe_alloc() with its bytes, *len set; 0 when there is no such
// file. The caller frees the block.
FBH_IMPORT(fbh_file_read) uint8_t* fbh_file_read(const char* path, size_t path_len, uint32_t* len);
// The media store (fonts, images; src/media/media_store.h buckets 0 fonts,
// 1 images, 2 render). The size of a file in bytes, -1 when the bucket has no
// such key; then its bytes, copied to dst (1 = done). The wasm keeps the copy.
FBH_IMPORT(fbh_media_size) int32_t fbh_media_size(int bucket, const char* key, size_t key_len);
FBH_IMPORT(fbh_media_read) int fbh_media_read(int bucket, const char* key, size_t key_len, uint8_t* dst, uint32_t size);
// --- the virtual hardware --------------------------------------------------------
// Does a device acknowledge `address` on I2C bus `bus` (0 = sensors, 1 = the
// connectors' port expanders, 0x20 + connector)?
FBH_IMPORT(fbh_i2c_probe) int fbh_i2c_probe(int bus, int address);
// The eight pins of the port expander at `address`: bit 1 low = a main
// display module (the scanner's signature); the key switch pins
// (include/pins.h MCP_SWITCH_LEFT / _RIGHT) read 0 while that key is held.
FBH_IMPORT(fbh_mcp_gpio) int fbh_mcp_gpio(int address);
// A sensor reading. what: 0 BME280 temperature (degrees C), 1 pressure (Pa),
// 2 humidity (%), 3 / 4 the STS35 at 0x4A / 0x4B (degrees C).
FBH_IMPORT(fbh_sensor_read) float fbh_sensor_read(int what);
// A pixel of the LED strip, as the firmware wrote it.
FBH_IMPORT(fbh_led) void fbh_led(int index, int r, int g, int b);

// --- the display -------------------------------------------------------------------
// A display's frame buffer changed in this rectangle (display: 0 = the main
// display, 1 + i = key display i; fbe_display_pixels has the pixels).
FBH_IMPORT(fbh_frame) void fbh_frame(int display, int x, int y, int width, int height);
// One record of what the device shows, as JSON (records.cpp documents each).
enum {
    FBH_STATE_DEVICE = 0,     // name, timezone, brokers, fonts, the resolved styles
    FBH_STATE_ITEM = 1,       // a display item
    FBH_STATE_ITEM_GONE = 2,  // {"id": n}: removed
    FBH_STATE_KEY = 3,        // a key and its display
    FBH_STATE_LIST = 4,
    FBH_STATE_IMAGE = 5,
    FBH_STATE_CLOCK = 6,
    FBH_STATE_PAGE = 7,       // {"page": n}: the shown page of the main display
    FBH_STATE_SENSOR = 8,     // a sensor of the device
    FBH_STATE_LAYOUT = 9,     // where the widgets of one display are (lv_obj_get_coords)
};
FBH_IMPORT(fbh_state) void fbh_state(int kind, const char* json, size_t len);

// --- keys ---------------------------------------------------------------------------
// A key event went through the script (button_hardware.cpp): event is the
// firmware's EventType (0 click, 1 long press, 2 release); prevented = the
// handler returned false, so the firmware publishes nothing for this press.
FBH_IMPORT(fbh_key_event) void fbh_key_event(int position, int event, int prevented);

// --- MQTT: what esp-mqtt does for a broker ---------------------------------------------
// Who publishes: a script (mqtt.publish) or the firmware by itself, and what.
enum { FBH_ORIGIN_SCRIPT = 0, FBH_ORIGIN_FIRMWARE = 1 };
enum {
    FBH_PUBLISH_SCRIPT = 0,
    FBH_PUBLISH_KEY = 1,           // a key press: configured topics, the Home Assistant event topic
    FBH_PUBLISH_DISCOVERY = 2,     // Home Assistant discovery (homeassistant/...)
    FBH_PUBLISH_AVAILABILITY = 3,  // <device>/status "online" (birth)
    FBH_PUBLISH_SENSOR = 4,        // a sensor's state
    FBH_PUBLISH_STATE = 5,         // other state the firmware reports (LEDs)
};
// The client starts: connect with this last will. The host answers with
// fbe_mqtt_connected() when the transport is up.
FBH_IMPORT(fbh_mqtt_start)
void fbh_mqtt_start(int broker, const char* uri, size_t uri_len, const char* will_topic, size_t will_topic_len,
                    const char* will_payload, size_t will_payload_len, int will_qos, int will_retain);
// (Imported only when something stops a broker; the firmware does not.)
FBH_IMPORT(fbh_mqtt_stop) void fbh_mqtt_stop(int broker);
// 0 = handed to the transport, -1 = failed.
FBH_IMPORT(fbh_mqtt_publish)
int fbh_mqtt_publish(int broker, const char* topic, size_t topic_len, const char* payload, size_t payload_len,
                     int qos, int retain, int origin, int kind);
FBH_IMPORT(fbh_mqtt_subscribe) void fbh_mqtt_subscribe(int broker, const char* topic, size_t topic_len, int qos);
FBH_IMPORT(fbh_mqtt_unsubscribe) void fbh_mqtt_unsubscribe(int broker, const char* topic, size_t topic_len);

#ifdef __cplusplus
}
#endif

#endif // FB_EMULATOR_FBH_H
