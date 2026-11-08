//
// HaEntities - see header.
//
// Keys are Home Assistant's abbreviations (homeassistant/components/mqtt/
// abbreviations.py): name, def_ent_id, uniq_id, dev{name, mf, mdl, sw, ids},
// stat_t, cmd_t, avty_t, pl_avail, pl_not_avail, dev_cla, evt_typ,
// unit_of_meas, schema, ret, rgb_*, bri_*, fx_*, and for device triggers
// atype, type, stype, t, pl. "~" is the base topic: Home Assistant replaces a
// leading "~" in every *topic value with it.
//

#include "HaEntities.h"

#include <cstdarg>
#include <cstdio>
#include <iterator>

#include "HaSlug.h"
#include "config.h"
#include "types.h"

namespace freebutton::ha {

namespace {

// Room for a slug of a long friendly name; longer ones are cut (HaSlug.h).
constexpr size_t kSlugCapacity = 96;

} // namespace

DiscoveryOut::DiscoveryOut(char* topic, const size_t topicCapacity, char* payload, const size_t payloadCapacity,
                           const std::string_view prefix, const DiscoveryDevice& device)
    : topic_(topic), topicCapacity_(topicCapacity), payload_(payload), json_(payload, payloadCapacity),
      prefix_(prefix), device_(device) {
    if (topicCapacity_ > 0) topic_[0] = '\0';
}

bool DiscoveryOut::writeTopic(const std::string_view component, const std::string_view objectId) {
    if (failed_ || kind_ != Kind::None || device_.identifier.empty() || topicCapacity_ == 0) {
        failed_ = true;  // failed before, a second entry in one message, or no device id
        return false;
    }
    const int n = std::snprintf(topic_, topicCapacity_, "%.*s/%.*s/%.*s/%.*s/config",
                                static_cast<int>(prefix_.size()), prefix_.data(),
                                static_cast<int>(component.size()), component.data(),
                                static_cast<int>(device_.identifier.size()), device_.identifier.data(),
                                static_cast<int>(objectId.size()), objectId.data());
    if (n < 0 || static_cast<size_t>(n) >= topicCapacity_) {
        failed_ = true;
        return false;
    }
    topicLength_ = static_cast<size_t>(n);
    return true;
}

void DiscoveryOut::writeDevice() {
    const auto& d = device_;
    json_.object("dev").str("name", d.name.empty() ? d.identifier : d.name);
    if (!d.manufacturer.empty()) json_.str("mf", d.manufacturer);
    if (!d.model.empty()) json_.str("mdl", d.model);
    if (!d.swVersion.empty()) json_.str("sw", d.swVersion);
    if (!d.identifier.empty()) json_.strArray("ids", &d.identifier, 1);
    json_.end();
}

JsonOut& DiscoveryOut::entity(const std::string_view component, const std::string_view objectId,
                              const std::string_view name, const std::string_view base,
                              const std::string_view uniqueIdSuffix) {
    if (!writeTopic(component, objectId)) {
        return json_;
    }
    kind_ = Kind::Config;
    if (!base.empty()) {
        json_.str("~", base);
    }
    json_.str("name", name);

    // Home Assistant expects <domain>.<object_id>; the object id part follows
    // the device's friendly name (like Zigbee2MQTT), e.g. "ButtonPlus Garage"
    // -> event.buttonplus_garage_button_7, falling back to the device id.
    // Applied to NEW entities only; uniq_id stays device-id based.
    char slug[kSlugCapacity];
    size_t slugLength = haSlugify(device_.name, slug, sizeof(slug));
    if (slugLength == 0) {
        slugLength = haSlugify(device_.identifier, slug, sizeof(slug));
    }
    json_.str("def_ent_id", component, ".", std::string_view(slug, slugLength), "_", objectId);
    json_.str("uniq_id", device_.identifier, "_", objectId, uniqueIdSuffix);
    writeDevice();
    return json_;
}

JsonOut& DiscoveryOut::trigger(const std::string_view objectId) {
    if (!writeTopic("device_automation", objectId)) {
        return json_;
    }
    kind_ = Kind::Config;
    json_.str("atype", "trigger");
    writeDevice();
    return json_;
}

void DiscoveryOut::remove(const std::string_view component, const std::string_view objectId) {
    if (writeTopic(component, objectId)) {
        kind_ = Kind::Remove;
    }
}

bool DiscoveryOut::finish(size_t& topicLength, size_t& payloadLength) {
    topicLength = 0;
    payloadLength = 0;
    if (failed_ || kind_ == Kind::None) {
        return false;
    }
    if (kind_ == Kind::Remove) {
        payload_[0] = '\0';
    } else {
        payloadLength = json_.finish();
        if (payloadLength == 0) {
            failed_ = true;
            return false;
        }
    }
    topicLength = topicLength_;
    return true;
}

// --- Entities ---------------------------------------------------------------

namespace {

// printf into a fixed buffer. A cut or empty result fails the entry: a
// silently shortened name / id / topic would announce the wrong entity.
[[gnu::format(printf, 4, 5)]]
std::string_view format(DiscoveryOut& out, char* buffer, const size_t capacity, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buffer, capacity, fmt, args);
    va_end(args);
    if (n <= 0 || static_cast<size_t>(n) >= capacity) {
        out.fail();
        return {};
    }
    return {buffer, static_cast<size_t>(n)};
}

// "~" for button entities: every topic they use is under buttonplus/<id>.
std::string_view deviceBase(DiscoveryOut& out, char* buffer, const size_t capacity) {
    const auto& id = out.device().identifier;
    return format(out, buffer, capacity, BASE_TOPIC "%.*s", static_cast<int>(id.size()), id.data());
}

void availability(JsonOut& json) {
    // Matches MqttBroker: LWT "offline" and birth "online", both retained.
    json.str("avty_t", "~/status").str("pl_avail", "online").str("pl_not_avail", "offline");
}

} // namespace

void writeButtonEvent(DiscoveryOut& out, const int pos, const bool exists,
                      const std::string_view* eventTypes, const size_t eventTypeCount) {
    char objectId[16];
    const auto id = format(out, objectId, sizeof(objectId), "button_%d", pos);
    if (id.empty()) return;
    if (!exists) {
        out.remove("event", id);
        return;
    }
    char name[16];
    char baseText[64];
    const auto label = format(out, name, sizeof(name), "Button %d", pos);
    const auto base = deviceBase(out, baseText, sizeof(baseText));
    if (label.empty() || base.empty()) return;
    auto& json = out.entity("event", id, label, base, "_event");
    json.str("stat_t", "~/", id, "/state").str("dev_cla", "button");
    if (eventTypeCount > 0) {
        json.strArray("evt_typ", eventTypes, eventTypeCount);
    }
    availability(json);
}

void writeButtonCommand(DiscoveryOut& out, const int pos, const bool exists) {
    char objectId[16];
    const auto id = format(out, objectId, sizeof(objectId), "button_%d", pos);
    if (id.empty()) return;
    if (!exists) {
        out.remove("button", id);
        return;
    }
    char name[16];
    char baseText[64];
    const auto label = format(out, name, sizeof(name), "Button %d", pos);
    const auto base = deviceBase(out, baseText, sizeof(baseText));
    if (label.empty() || base.empty()) return;
    auto& json = out.entity("button", id, label, base);
    json.str("cmd_t", "~/", id, "/set");
    availability(json);
}

void writeButtonTrigger(DiscoveryOut& out, const int pos, const bool longPress, const bool exists) {
    char objectId[24];
    const auto id = format(out, objectId, sizeof(objectId), "button_%d_%s", pos, longPress ? "long" : "click");
    if (id.empty()) return;
    if (!exists) {
        out.remove("device_automation", id);
        return;
    }
    char subtype[16];
    const auto key = format(out, subtype, sizeof(subtype), "button_%d", pos);
    if (key.empty()) return;
    // Single topic: written in full, no "~".
    out.trigger(id)
        .str("type", longPress ? "button_long_press" : "button_short_press")
        .str("stype", key)
        .str("t", BASE_TOPIC, out.device().identifier, "/", key, "/state")
        .str("pl", longPress ? LONG_PRESS_PAYLOAD : CLICK_PAYLOAD);
}

void writeLight(DiscoveryOut& out, const int pos, const int ledNumber, const std::string_view positionName,
                const std::string_view topicBase) {
    if (topicBase.empty() || positionName.empty()) {
        out.fail();
        return;
    }
    char objectId[24];
    char name[48];
    char baseText[96];
    const auto id = format(out, objectId, sizeof(objectId), "led_%d_%d", pos, ledNumber);
    const auto label = format(out, name, sizeof(name), "Button %d %.*s LED", pos,
                              static_cast<int>(positionName.size()), positionName.data());
    const auto base = format(out, baseText, sizeof(baseText), "%.*s/led",
                             static_cast<int>(topicBase.size()), topicBase.data());
    if (id.empty() || label.empty() || base.empty()) return;
    static constexpr std::string_view kEffects[] = {"none", "glow", "flash"};

    out.entity("light", id, label, base)
        .str("stat_t", "~/state")
        .str("cmd_t", "~/set")
        .str("schema", "json")
        .str("ret", "true")  // string, as in earlier firmware (HA coerces it)
        .str("rgb_stat_t", "~/color/state")
        .str("rgb_cmd_t", "~/color/set")
        .str("rgb_cmd_tpl", R"({{ {"r": red, "g": green, "b": blue} | tojson }})")
        .str("rgb_val_tpl", "{{ value_json.r }},{{ value_json.g }},{{ value_json.b }}")
        .str("bri_stat_t", "~/brightness/state")
        .str("bri_cmd_t", "~/brightness/set")
        .str("bri_scl", "255")  // string, as in earlier firmware (HA coerces it)
        .str("fx_stat_t", "~/effect/state")
        .str("fx_cmd_t", "~/effect/set")
        .strArray("fx_list", kEffects, std::size(kEffects));
}

void writeSensor(DiscoveryOut& out, const std::string_view objectId, const std::string_view name,
                 const std::string_view deviceClass, const std::string_view unit, const std::string_view topicName) {
    if (objectId.empty() || topicName.empty()) {
        out.fail();
        return;
    }
    auto& json = out.entity("sensor", objectId, name);
    if (!deviceClass.empty()) json.str("dev_cla", deviceClass);
    if (!unit.empty()) json.str("unit_of_meas", unit);
    // Single topic: written in full, no "~".
    json.str("stat_t", BASE_TOPIC, out.device().identifier, "/sensor/", topicName, "/state");
}

} // namespace freebutton::ha
