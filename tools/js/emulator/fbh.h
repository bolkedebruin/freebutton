//
// fbh.h - what fb-emulator.wasm imports from its host, besides the seam
// functions of the firmware headers that the host implements under their own
// name (displayitem_*, button_hw_set_*, led_hw_*, ...: see docs/EMULATOR.md).
//
// The host is web/src/js/emulator/wasm.ts. Every function here is a wasm
// import of module "env". Strings are pointer + length into the wasm memory
// unless the name says otherwise; the host copies what it keeps.
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
// Milliseconds since the Unix epoch on the emulator's clock (Date.now(),
// setTimeout, performance.now()).
FBH_IMPORT(fbh_now_ms) double fbh_now_ms(void);
// print() / console.log() text, as the firmware sends it to the IDE console.
FBH_IMPORT(fbh_console) void fbh_console(const char* text, size_t len);
// A firmware log line (ESP_LOGx: level 'E' 'W' 'I') or engine output ('J').
FBH_IMPORT(fbh_log) void fbh_log(int level, const char* tag, const char* text, size_t len);
// Polled by the engine while script code runs: nonzero stops the script.
FBH_IMPORT(fbh_interrupt) int fbh_interrupt(void);

// --- module topology (device_hardware.cpp) ----------------------------------
// 0 = empty, 1 = bar, 2 = main display; -1 = no such connector.
FBH_IMPORT(fbh_connector_type) int fbh_connector_type(int connector);
FBH_IMPORT(fbh_main_connector) int fbh_main_connector(void);

// --- keys -------------------------------------------------------------------
// After the firmware's rules ran (seam.cpp): the icon name is valid or empty.
FBH_IMPORT(fbh_key_set_icon) int fbh_key_set_icon(int position, const char* name, size_t len);
// A key event went through the script (ButtonManager's lambda): event is the
// firmware's EventType (0 click, 1 long press, 2 release); prevented = the
// handler returned false, so the firmware publishes nothing for this press.
FBH_IMPORT(fbh_key_event) void fbh_key_event(int position, int event, int prevented);

// --- display items / images: after the icon rules ----------------------------
FBH_IMPORT(fbh_item_set_icon) int fbh_item_set_icon(unsigned long id, const char* name, size_t len);
FBH_IMPORT(fbh_image_exists) int fbh_image_exists(int index);
// kind: 'i' icon, 's' images-bucket key (IconLogic.h kWantIcon / kWantSrc).
FBH_IMPORT(fbh_image_set) void fbh_image_set(int index, int kind, const char* value, size_t len);

// --- lists: the selection rules run in seam.cpp (ListLogic.h) -----------------
// Number of items, -1 when there is no list at index.
FBH_IMPORT(fbh_list_count) int fbh_list_count(int index);
FBH_IMPORT(fbh_list_get_selected) int fbh_list_get_selected(int index);
FBH_IMPORT(fbh_list_set_selected) void fbh_list_set_selected(int index, int selected);
// options: the items as one string, separated by '\n' (as the firmware keeps
// them); count items; selected is already clamped to them.
FBH_IMPORT(fbh_list_set_items) void fbh_list_set_items(int index, const char* options, size_t len, int count, int selected);

// --- MQTT: what MqttBroker hands to its client -------------------------------
// 0 = sent to the transport, -1 = failed.
FBH_IMPORT(fbh_mqtt_publish) int fbh_mqtt_publish(int broker, const char* topic, size_t topic_len,
                                                  const char* payload, size_t payload_len, int qos, int retain);
FBH_IMPORT(fbh_mqtt_subscribe) void fbh_mqtt_subscribe(int broker, const char* topic, size_t topic_len, int qos);
FBH_IMPORT(fbh_mqtt_unsubscribe) void fbh_mqtt_unsubscribe(int broker, const char* topic, size_t topic_len);

// --- sensors, system, restart -------------------------------------------------
// The device id ("virt_btn_xxxxxx") and name, written into buf (at most cap
// bytes, no terminator); returns the number of bytes. The id is read once,
// when the module starts.
FBH_IMPORT(fbh_device_id) size_t fbh_device_id(char* buf, size_t cap);
FBH_IMPORT(fbh_device_name) size_t fbh_device_name(char* buf, size_t cap);
FBH_IMPORT(fbh_sensor_sample) float fbh_sensor_sample(int id);
FBH_IMPORT(fbh_uptime_seconds) uint32_t fbh_uptime_seconds(void);
FBH_IMPORT(fbh_free_heap) uint32_t fbh_free_heap(void);
// ConfigManager::restoreConfigState(): everything a script changed goes back
// to config.json (docs/SCRIPT-RESTART.md).
FBH_IMPORT(fbh_restore_config_state) void fbh_restore_config_state(void);

#ifdef __cplusplus
}
#endif

#endif // FB_EMULATOR_FBH_H
