//
// HaEntities - the Home Assistant MQTT discovery configs this firmware
// announces, written straight into fixed buffers (no heap).
//
// Payloads use Home Assistant's abbreviated keys (abbreviations.py) and the
// "~" base topic directly; test/test_ha_discovery expands them the way Home
// Assistant does and checks they equal the full configs.
//
// Pure (no ESP/MQTT dependencies): unit-tested on the host.
//

#ifndef FREEBUTTON_HAENTITIES_H
#define FREEBUTTON_HAENTITIES_H

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "HaJson.h"

namespace freebutton::ha {

struct DiscoveryDevice {
    std::string_view name;          // friendly name, "" = use identifier
    std::string_view manufacturer;
    std::string_view model;
    std::string_view swVersion;
    std::string_view identifier;    // device id, e.g. "btn_a00001"
};

// One discovery message under construction: its config topic and payload,
// both in caller-owned buffers (the discovery task's stack).
class DiscoveryOut {
public:
    DiscoveryOut(char* topic, size_t topicCapacity, char* payload, size_t payloadCapacity,
                 std::string_view prefix, const DiscoveryDevice& device);

    // Entity config on <prefix>/<component>/<identifier>/<objectId>/config.
    // Writes "~" (when base is not empty), name, def_ent_id
    // (<component>.<slug(name or identifier)>_<objectId>), uniq_id
    // (<identifier>_<objectId><uniqueIdSuffix>) and dev. The caller adds the
    // entity's own fields to the returned object.
    JsonOut& entity(std::string_view component, std::string_view objectId, std::string_view name,
                    std::string_view base = {}, std::string_view uniqueIdSuffix = {});

    // Device trigger on <prefix>/device_automation/<identifier>/<objectId>/config,
    // with atype and dev written.
    JsonOut& trigger(std::string_view objectId);

    // The entity must not exist: empty retained payload on its config topic.
    void remove(std::string_view component, std::string_view objectId);

    // After the entry was written. false: nothing to publish, or a buffer was
    // too small or the entry failed (see failed()). payloadLength 0 = remove.
    bool finish(size_t& topicLength, size_t& payloadLength);
    [[nodiscard]] bool failed() const { return failed_; }
    // The entry cannot be written correctly (a name or topic would be cut,
    // a base topic is missing): nothing is published for it.
    void fail() { failed_ = true; }

    [[nodiscard]] const DiscoveryDevice& device() const { return device_; }
    [[nodiscard]] const char* topic() const { return topic_; }
    [[nodiscard]] const char* payload() const { return payload_; }

private:
    bool writeTopic(std::string_view component, std::string_view objectId);
    void writeDevice();

    enum class Kind : uint8_t { None, Config, Remove };

    char* topic_;
    size_t topicCapacity_;
    size_t topicLength_ = 0;
    char* payload_;
    JsonOut json_;
    std::string_view prefix_;
    const DiscoveryDevice& device_;
    Kind kind_ = Kind::None;
    bool failed_ = false;
};

// Something that announces entities. Discovery follows the hardware, so the
// entries are generated when they are published, never stored.
class DiscoverySource {
public:
    virtual ~DiscoverySource() = default;
    // Number of entries; stable while the hardware does not change.
    [[nodiscard]] virtual size_t discoveryCount() const = 0;
    // Write entry `index` (< discoveryCount()) for a broker into `out`:
    // a config or a remove. false = no message for this entry.
    virtual bool writeDiscovery(size_t index, uint8_t brokerId, DiscoveryOut& out) const = 0;
};

// --- The entities -----------------------------------------------------------
// `exists == false` writes the remove (empty payload) for the same topic.

// event.<..>_button_<pos>: physical presses on buttonplus/<id>/button_<pos>/state.
void writeButtonEvent(DiscoveryOut& out, int pos, bool exists,
                      const std::string_view* eventTypes, size_t eventTypeCount);

// button.<..>_button_<pos>: a press from Home Assistant on buttonplus/<id>/button_<pos>/set.
void writeButtonCommand(DiscoveryOut& out, int pos, bool exists);

// Device trigger button_<pos>_click / button_<pos>_long on the press topic.
void writeButtonTrigger(DiscoveryOut& out, int pos, bool longPress, bool exists);

// light.<..>_led_<pos>_<ledNumber>. topicBase: the LED's topic root,
// e.g. "buttonplus/btn_a00001/button7_wall"; positionName "front" / "wall".
void writeLight(DiscoveryOut& out, int pos, int ledNumber, std::string_view positionName,
                std::string_view topicBase);

// sensor.<..>_<objectId>, state on buttonplus/<id>/sensor/<topicName>/state.
void writeSensor(DiscoveryOut& out, std::string_view objectId, std::string_view name,
                 std::string_view deviceClass, std::string_view unit, std::string_view topicName);

} // namespace freebutton::ha

#endif //FREEBUTTON_HAENTITIES_H
