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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "types.h"

namespace freebutton::ha {

// One configured topic of a key, as Button stores it.
struct PressTopic {
    uint8_t brokerId;
    std::string_view topic;
    std::string_view payload;   // empty = firmware default JSON payload
    EventType eventType;
};

// Event types of a plan, stored inline: the plan is built on the Home
// Assistant discovery task, which must not touch the heap.
class EventTypeList {
public:
    // click + longpress + one per configured CLICK / LONG_PRESS / RELEASE topic.
    static constexpr size_t kMaxTypes = 5;
    static constexpr size_t kMaxLength = 32;

    // Adds `type` unless already present. false: too long or full.
    bool add(std::string_view type);
    [[nodiscard]] size_t size() const { return count_; }
    [[nodiscard]] bool empty() const { return count_ == 0; }
    [[nodiscard]] std::string_view operator[](const size_t i) const { return {items_[i], lengths_[i]}; }

    class Iterator {
    public:
        Iterator(const EventTypeList* list, const size_t i) : list_(list), i_(i) {}
        std::string_view operator*() const { return (*list_)[i_]; }
        Iterator& operator++() { ++i_; return *this; }
        bool operator!=(const Iterator& other) const { return i_ != other.i_; }
    private:
        const EventTypeList* list_;
        size_t i_;
    };
    [[nodiscard]] Iterator begin() const { return {this, 0}; }
    [[nodiscard]] Iterator end() const { return {this, count_}; }

private:
    char items_[kMaxTypes][kMaxLength + 1] = {};
    uint8_t lengths_[kMaxTypes] = {};
    size_t count_ = 0;
};

// Why a key is not announced; stored inline, cut when longer.
class PlanProblem {
public:
    void set(std::string_view payload, std::string_view canonicalTopic);
    void setMessage(std::string_view text);
    [[nodiscard]] bool empty() const { return text_[0] == '\0'; }
    [[nodiscard]] const char* c_str() const { return text_; }
private:
    char text_[160] = {};
};

struct ButtonEventPlan {
    // An `event` entity and device triggers can be announced for this key.
    bool announceEvent = false;
    // The firmware must publish the default JSON to the canonical topic in
    // addition to the user's own publish, for click / long press.
    bool mirrorClick = false;
    bool mirrorLongPress = false;
    // Every event_type value that can appear on the canonical topic.
    EventTypeList eventTypes;
    // Why announceEvent is false (empty otherwise).
    PlanProblem problem;
};

std::string canonicalButtonStateTopic(std::string_view deviceId, int position);
// Heap-free variant: NUL-terminated into `buffer`; length, 0 if it does not fit.
size_t canonicalButtonStateTopic(char* buffer, size_t capacity, std::string_view deviceId, int position);
std::string buttonCommandTopic(std::string_view deviceId, int position);

// Default payload for a press event type, "" for non-press types.
std::string_view defaultPayloadFor(EventType eventType);

// event_type of a JSON object payload like {"event_type":"click"}, or nullopt
// when the payload is not a JSON object with a non-empty string event_type.
std::optional<std::string> jsonEventType(std::string_view payload);

// Heap-free variant (strict JSON; content after the object is ignored, as
// ArduinoJson does). Writes the unescaped event_type NUL-terminated into
// `out`. false: no such event_type, or it does not fit in capacity - 1.
bool jsonEventType(std::string_view payload, char* out, size_t capacity, size_t& length);

// Uses no dynamic memory (safe on the discovery path).
ButtonEventPlan planButtonEvents(std::span<const PressTopic> topics,
                                 uint8_t haBrokerId,
                                 std::string_view canonicalTopic);

// A key that cannot be announced at all (e.g. its canonical topic does not
// fit): announceEvent = false, nothing mirrored, `reason` as the problem.
ButtonEventPlan unannouncedPlan(std::string_view reason);

} // namespace freebutton::ha

#endif //FREEBUTTON_BUTTONEVENTPLAN_H
