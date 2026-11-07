//
// ButtonEventPlan - see header.
//

#include "ButtonEventPlan.h"

#include <algorithm>
#include <ArduinoJson.h>

#include "config.h"

namespace freebutton::ha {

std::string canonicalButtonStateTopic(std::string_view deviceId, const int position) {
    return std::string(PUBLICMQTTID) + "/" + std::string(deviceId) + "/button_" + std::to_string(position) + "/state";
}

std::string buttonCommandTopic(std::string_view deviceId, const int position) {
    return std::string(PUBLICMQTTID) + "/" + std::string(deviceId) + "/button_" + std::to_string(position) + "/set";
}

std::string_view defaultPayloadFor(const EventType eventType) {
    switch (eventType) {
        case CLICK:      return CLICK_PAYLOAD;
        case LONG_PRESS: return LONG_PRESS_PAYLOAD;
        case RELEASE:    return RELEASE_PAYLOAD;
        default:         return {};
    }
}

std::optional<std::string> jsonEventType(std::string_view payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload.data(), payload.size()) != DeserializationError::Ok) {
        return std::nullopt;
    }
    if (!doc.is<JsonObject>() || !doc["event_type"].is<const char*>()) {
        return std::nullopt;
    }
    std::string value = doc["event_type"].as<std::string>();
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

ButtonEventPlan planButtonEvents(std::span<const PressTopic> topics,
                                 const uint8_t haBrokerId,
                                 const std::string_view canonicalTopic) {
    ButtonEventPlan plan;
    plan.announceEvent = true;

    auto addType = [&plan](std::string type) {
        if (std::ranges::find(plan.eventTypes, type) == plan.eventTypes.end()) {
            plan.eventTypes.push_back(std::move(type));
        }
    };

    // Button::publishToTopic publishes only the FIRST topic configured for an
    // event type, so only that one is relevant.
    auto firstFor = [&topics](const EventType eventType) -> const PressTopic* {
        const auto it = std::ranges::find_if(topics, [eventType](const PressTopic& t) {
            return t.eventType == eventType;
        });
        return it != topics.end() ? &*it : nullptr;
    };

    auto onCanonical = [&](const PressTopic& t) {
        return t.brokerId == haBrokerId && t.topic == canonicalTopic;
    };

    auto effectivePayload = [](const PressTopic& t) {
        return t.payload.empty() ? defaultPayloadFor(t.eventType) : t.payload;
    };

    // Click / long press always reach the canonical topic: either the user's
    // config already publishes the exact default payload there, or we mirror.
    const PressTopic* click = firstFor(CLICK);
    const PressTopic* longPress = firstFor(LONG_PRESS);
    plan.mirrorClick = !(click && onCanonical(*click) && effectivePayload(*click) == CLICK_PAYLOAD);
    plan.mirrorLongPress = !(longPress && onCanonical(*longPress) && effectivePayload(*longPress) == LONG_PRESS_PAYLOAD);
    addType("click");
    addType("longpress");

    // Anything the user's own config puts on the canonical topic must be a
    // JSON event too, and its event_type must be listed.
    for (const EventType eventType : {CLICK, LONG_PRESS, RELEASE}) {
        const PressTopic* t = firstFor(eventType);
        if (!t || !onCanonical(*t)) {
            continue;
        }
        const auto payload = effectivePayload(*t);
        if (auto type = jsonEventType(payload)) {
            addType(std::move(*type));
        } else {
            plan.announceEvent = false;
            plan.problem = "configured payload '" + std::string(payload) +
                           "' on " + std::string(canonicalTopic) +
                           " is not a JSON object with an event_type";
        }
    }

    if (!plan.announceEvent) {
        plan.mirrorClick = false;
        plan.mirrorLongPress = false;
    }
    return plan;
}

} // namespace freebutton::ha
