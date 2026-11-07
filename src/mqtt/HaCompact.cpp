//
// HaCompact - see header.
//

#include "HaCompact.h"

#include <array>
#include <utility>

namespace freebutton::ha {

namespace {

using Pair = std::pair<std::string_view, std::string_view>;  // full name, abbreviation

// Subset of Home Assistant's ABBREVIATIONS that this firmware emits. Every
// entry was checked against homeassistant/components/mqtt/abbreviations.py.
constexpr std::array kKeys = {
    Pair{"availability_topic", "avty_t"},
    Pair{"payload_available", "pl_avail"},
    Pair{"payload_not_available", "pl_not_avail"},
    Pair{"brightness_command_topic", "bri_cmd_t"},
    Pair{"brightness_state_topic", "bri_stat_t"},
    Pair{"brightness_scale", "bri_scl"},
    Pair{"command_topic", "cmd_t"},
    Pair{"state_topic", "stat_t"},
    Pair{"effect_command_topic", "fx_cmd_t"},
    Pair{"effect_state_topic", "fx_stat_t"},
    Pair{"effect_list", "fx_list"},
    Pair{"rgb_command_template", "rgb_cmd_tpl"},
    Pair{"rgb_command_topic", "rgb_cmd_t"},
    Pair{"rgb_state_topic", "rgb_stat_t"},
    Pair{"rgb_value_template", "rgb_val_tpl"},
    Pair{"retain", "ret"},
    Pair{"unique_id", "uniq_id"},
    Pair{"device_class", "dev_cla"},
    Pair{"unit_of_measurement", "unit_of_meas"},
    Pair{"value_template", "val_tpl"},
    Pair{"event_types", "evt_typ"},
    Pair{"default_entity_id", "def_ent_id"},
    Pair{"device", "dev"},
};

// Subset of Home Assistant's DEVICE_ABBREVIATIONS.
constexpr std::array kDeviceKeys = {
    Pair{"identifiers", "ids"},
    Pair{"manufacturer", "mf"},
    Pair{"model", "mdl"},
    Pair{"sw_version", "sw"},
};

template <size_t N>
std::string_view lookup(const std::array<Pair, N>& table, const std::string_view key) {
    for (const auto& [full, abbr] : table) {
        if (full == key) return abbr;
    }
    return key;
}

bool endsWith(const std::string_view s, const std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

} // namespace

std::string_view abbreviateKey(const std::string_view key) {
    return lookup(kKeys, key);
}

std::string_view abbreviateDeviceKey(const std::string_view key) {
    return lookup(kDeviceKeys, key);
}

std::string commonTopicBase(const JsonDocument& doc) {
    std::string common;
    bool first = true;
    for (const JsonPairConst kv : doc.as<JsonObjectConst>()) {
        if (!endsWith(kv.key().c_str(), "topic") || !kv.value().is<const char*>()) continue;
        const std::string_view value = kv.value().as<const char*>();
        // Directory part of this topic: everything before its last '/'.
        const size_t slash = value.rfind('/');
        if (slash == std::string_view::npos || slash == 0) return {};
        const std::string_view dir = value.substr(0, slash);
        if (first) {
            common.assign(dir);
            first = false;
            continue;
        }
        size_t n = 0;
        while (n < common.size() && n < dir.size() && common[n] == dir[n]) ++n;
        // The prefix must end on a whole segment in BOTH strings, i.e. at the
        // end of the string or just before a '/'. Otherwise cut back to the
        // previous '/' ("a/button7_front" vs "a/button7_wall" -> "a").
        const auto atBoundary = [n](const std::string_view s) { return n == s.size() || s[n] == '/'; };
        if (!atBoundary(common) || !atBoundary(dir)) {
            const size_t cut = (n == 0) ? std::string::npos : common.rfind('/', n - 1);
            n = (cut == std::string::npos) ? 0 : cut;
        }
        common.resize(n);
        if (common.empty()) return {};
    }
    return common;
}

JsonDocument compactDiscovery(const JsonDocument& doc) {
    JsonDocument out;
    const std::string baseTopic = commonTopicBase(doc);
    const std::string prefix = baseTopic.empty() ? std::string() : baseTopic + "/";
    bool usedBase = false;

    for (const JsonPairConst kv : doc.as<JsonObjectConst>()) {
        const std::string_view key = kv.key().c_str();
        const std::string abbr(abbreviateKey(key));

        if (key == "device" && kv.value().is<JsonObjectConst>()) {
            const JsonObject dev = out[abbr].to<JsonObject>();
            for (const JsonPairConst dkv : kv.value().as<JsonObjectConst>()) {
                dev[std::string(abbreviateDeviceKey(dkv.key().c_str()))] = dkv.value();
            }
            continue;
        }

        // Home Assistant substitutes "~" only in keys ending with "topic"
        // (checked after expanding abbreviations).
        if (!prefix.empty() && endsWith(key, "topic") && kv.value().is<const char*>()) {
            const std::string_view value = kv.value().as<const char*>();
            if (value.size() > prefix.size() && value.substr(0, prefix.size()) == prefix) {
                out[abbr] = "~/" + std::string(value.substr(prefix.size()));
                usedBase = true;
                continue;
            }
        }

        out[abbr] = kv.value();
    }

    if (usedBase) {
        out["~"] = baseTopic;
    }
    return out;
}

} // namespace freebutton::ha
