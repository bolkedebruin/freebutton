//
// ButtonEventPlan - decide how a configured key is exposed to Home Assistant.
//
// Pure logic, no ESP/MQTT dependencies, so it is unit-tested on the host
// (test/test_button_event_plan). Both the discovery generators (what is
// announced) and Button::click()/longPress() (what is published) call
// planButtonEvents(), so the announcement can never drift from what the
// firmware actually sends.
//
// Canonical press topic: buttonplus/<deviceId>/button_<pos>/state, carrying
// the JSON payloads {"event_type":"click"|"longpress"|"release"}.
//
// Policy (option b): whatever topics the user configured, the firmware ALSO
// publishes click/longpress JSON to the canonical topic when the user's own
// config does not already do exactly that. This is additive: user-configured
// topics and payloads are published unchanged. The plan is per broker: it is
// evaluated for every broker with Home Assistant discovery ("hassdiscovery").
//

#ifndef FREEBUTTON_BUTTONEVENTPLAN_H
#define FREEBUTTON_BUTTONEVENTPLAN_H

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "types.h"

namespace freebutton::ha {

// One configured topic of a key, as Button stores it.
struct PressTopic {
    uint8_t brokerId;
    std::string_view topic;
    std::string_view payload;   // empty = firmware default JSON payload
    EventType eventType;
};

struct ButtonEventPlan {
    // An `event` entity and device triggers can be announced for this key.
    bool announceEvent = false;
    // The firmware must publish the default JSON to the canonical topic in
    // addition to the user's own publish, for click / long press.
    bool mirrorClick = false;
    bool mirrorLongPress = false;
    // Every event_type value that can appear on the canonical topic.
    std::vector<std::string> eventTypes;
    // Why announceEvent is false (empty otherwise).
    std::string problem;
};

std::string canonicalButtonStateTopic(std::string_view deviceId, int position);
std::string buttonCommandTopic(std::string_view deviceId, int position);

// Default payload for a press event type, "" for non-press types.
std::string_view defaultPayloadFor(EventType eventType);

// event_type of a JSON object payload like {"event_type":"click"}, or nullopt
// when the payload is not a JSON object with a non-empty string event_type.
std::optional<std::string> jsonEventType(std::string_view payload);

ButtonEventPlan planButtonEvents(std::span<const PressTopic> topics,
                                 uint8_t haBrokerId,
                                 std::string_view canonicalTopic);

} // namespace freebutton::ha

#endif //FREEBUTTON_BUTTONEVENTPLAN_H
