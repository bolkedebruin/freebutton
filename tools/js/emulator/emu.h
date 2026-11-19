//
// emu.h - what the emulator's own sources share (tools/js/emulator/*.c*).
//
#ifndef FB_EMULATOR_EMU_H
#define FB_EMULATOR_EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FBE_EXPORT(name) __attribute__((export_name(#name), used))

// --- runner.c -----------------------------------------------------------------
// A new entry into the engine starts: the runaway-script budget starts again.
void fbe_enter_engine(void);
// After a key handler ran: the glue leaves its exception pending.
void fbe_report_pending_exception(void);
// ScriptEngine::measureHeap() for the JS memory sensors: after a GC.
void fbe_js_heap(size_t* used, size_t* free_bytes);
// tz.c: the POSIX TZ string of the device.
void fbe_tz_set(const char* posix);

// --- device.cpp ----------------------------------------------------------------
// ConfigManager::restoreConfigState(), for a script restart.
void fbe_device_restore_config_state(void);
// What the firmware's other tasks and loop() do around the script in one
// pass (the MQTT task's queued events, Home Assistant commands and
// discovery, the LVGL task's pending renders), then the state records for
// the host. Returns nonzero when another pass is needed soon.
int fbe_device_pass(void);
// After any entry into the engine: the same without the loop() parts.
void fbe_device_after_entry(void);

// --- mqtt_client_shim.cpp ---------------------------------------------------------
// Delivers the queued client events (connected, SUBACK, wake-ups, messages).
void fbe_mqtt_pump(void);
// Who publishes now (fbh.h FBH_ORIGIN_*, FBH_PUBLISH_*); returns the previous kind.
int fbe_publish_scope(int origin, int kind);
void fbe_publish_scope_end(int previous_kind);

// --- seam_mqtt.cpp -----------------------------------------------------------------
// MqttBinding::processPending() of one loop() pass; nonzero when events are left.
int fbe_mqtt_process_pending(void);
// Whether a script subscription matches (the firmware's filter).
int fbe_mqtt_script_wants(int broker, const char* topic, size_t topic_len);
uint32_t fbe_mqtt_dropped(void);
uint32_t fbe_mqtt_queue_used(void);
uint32_t fbe_mqtt_queue_bytes(void);

// --- display.cpp --------------------------------------------------------------------
// Draws what is pending on every display now and reports the frames.
void fbe_display_refresh_now(void);
// --- media_emu.c -------------------------------------------------------------------
// The key of the media file that holds `data`, NULL when it is not media.
const char* fbe_media_key_of(const uint8_t* data);
uint32_t fbe_media_bytes(void);
// The host's files of a bucket changed (-1: of every bucket).
void fbe_media_forget(int bucket);
// One file changed: the next open asks the host again.
void fbe_media_forget_key(int bucket, const char* key);
// --- seam_fonts.c, seam_clock.c -------------------------------------------------------
// (fbe_font_info and fbe_clock_container, which take LVGL types: emu_ui.h)
size_t fbe_font_face_count(void);
bool fbe_font_face(size_t index, const char** name, const char** key, int32_t* id, int* fallback,
                   int* lineheight_x100, bool* usable);
size_t fbe_font_instance_count(void);
// The font reload poll runs only while a reload was asked for.
void fbe_fonts_poll_sleep(void);
// --- seam_discovery.cpp ---------------------------------------------------------------
// HomeAssistantDiscovery::poll(); nonzero while a discovery round is pending.
int fbe_discovery_poll(void);

#ifdef __cplusplus
}
#endif

#endif // FB_EMULATOR_EMU_H
