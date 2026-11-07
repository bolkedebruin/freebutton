//
// HaCompact - shrink Home Assistant MQTT discovery payloads.
//
// Rewrites a discovery config to Home Assistant's documented abbreviations
// (homeassistant/components/mqtt/abbreviations.py, e.g. state_topic -> stat_t)
// and moves the common topic prefix into the "~" base topic. Home Assistant
// expands both on receipt, so the entity is identical; the payload is roughly
// half the size, which keeps the discovery burst on connect within the small
// lwIP/MQTT buffers of this firmware.
//
// Pure (ArduinoJson only), unit-tested on the host in test/test_ha_compact.
//

#ifndef FREEBUTTON_HACOMPACT_H
#define FREEBUTTON_HACOMPACT_H

#include <string>
#include <string_view>
#include <ArduinoJson.h>

namespace freebutton::ha {

// Abbreviation for a top-level discovery key, or the key itself if none.
std::string_view abbreviateKey(std::string_view key);

// Abbreviation for a key inside the "device" object, or the key itself.
std::string_view abbreviateDeviceKey(std::string_view key);

// Longest common "/"-boundary prefix of all *topic values in `doc`
// (e.g. "buttonplus/btn_a00002/button7_wall/led"), "" if there is none.
std::string commonTopicBase(const JsonDocument& doc);

// Compact `doc` (a full-name discovery config) into a new document:
// abbreviated keys, and *topic values that start with commonTopicBase(doc)
// rewritten to "~/..." with "~" set to that base.
JsonDocument compactDiscovery(const JsonDocument& doc);

} // namespace freebutton::ha

#endif //FREEBUTTON_HACOMPACT_H
