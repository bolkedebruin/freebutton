//
// seam_mqtt.cpp - the script side of MQTT, for fb-emulator.wasm.
//
// src/scripting/mqtt_binding.cpp (the 2 KB event ring between the MQTT task
// and the loop task, the drop counter, processPending) is the firmware's,
// included below unchanged. Two things are added around it: the queue and
// its counters are reported for emu.stats(), and a publish that comes from
// mqtt.publish() is marked as the script's, so that the host can tell it
// from what the firmware publishes by itself.
//
#include <Arduino.h>
#include <esp_log.h>

#include "emu.h"
#include "fbh.h"

// The firmware's function keeps its body; the name scripts reach is the
// wrapper at the end of this file.
#define mqtt_binding_publish fbe_firmware_mqtt_binding_publish
#include "scripting/mqtt_binding.cpp"
#undef mqtt_binding_publish

extern "C" {

int mqtt_binding_publish(const uint8_t brokerId, const char* topic, const char* payload, const int qos,
                         const int retain) {
    const int scope = fbe_publish_scope(FBH_ORIGIN_SCRIPT, FBH_PUBLISH_SCRIPT);
    const int result = fbe_firmware_mqtt_binding_publish(brokerId, topic, payload, qos, retain);
    fbe_publish_scope_end(scope);
    return result;
}

int fbe_mqtt_process_pending(void) {
    MqttBinding::processPending();
    return g_queue.empty() ? 0 : 1;
}

int fbe_mqtt_script_wants(const int broker, const char* topic, const size_t topic_len) {
    const mqtt_js_message_filter_t filter = g_js_message_filter.load();
    return filter && filter(static_cast<uint8_t>(broker), topic, topic_len) ? 1 : 0;
}

uint32_t fbe_mqtt_dropped(void) { return g_dropped.load(); }
uint32_t fbe_mqtt_queue_used(void) { return static_cast<uint32_t>(g_queue.used()); }
uint32_t fbe_mqtt_queue_bytes(void) { return static_cast<uint32_t>(kQueueBytes); }

} // extern "C"
