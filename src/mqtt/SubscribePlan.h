//
// Batch planner for MQTT SUBSCRIBE packets. Pure, header-only, no ESP-IDF
// dependencies, so the native tests can include it.
//
// esp-mqtt builds a SUBSCRIBE in its single out-buffer (CONFIG_MQTT_BUFFER_SIZE
// unless buffer.out_size is set) and fails the call when it does not fit
// (mqtt_msg.c, mqtt_msg_subscribe). The layout it writes is:
//
//   5 bytes reserved for the fixed header (MQTT_MAX_FIXED_HEADER_SIZE)
//   2 bytes packet identifier
//   per topic filter: 2 bytes length + filter bytes + 1 byte requested QoS
//
// and every write is checked as `length + n > buffer_length`, so a packet whose
// total equals the buffer size still fits.
//
#ifndef FREEBUTTON_SUBSCRIBEPLAN_H
#define FREEBUTTON_SUBSCRIBEPLAN_H

#include <cstddef>

namespace freebutton::mqtt {

// Fixed header reserve + packet identifier.
inline constexpr std::size_t kSubscribeOverheadBytes = 5 + 2;

// Bytes one topic filter takes inside a SUBSCRIBE.
constexpr std::size_t subscribeTopicCost(const std::size_t topicLen) {
    return 2 + topicLen + 1;
}

// True when a SUBSCRIBE carrying only this filter fits the out-buffer.
constexpr bool subscribeTopicFits(const std::size_t topicLen, const std::size_t bufferBytes) {
    return topicLen > 0 && kSubscribeOverheadBytes + subscribeTopicCost(topicLen) <= bufferBytes;
}

// Picks the next SUBSCRIBE batch out of entries [0, count), in order.
//
//   isPending(i) -> bool          entry i still has to be subscribed
//   topicLen(i)  -> size_t        length of entry i's topic filter
//   take(k, i)                    entry i is the k-th topic of the batch
//   skip(i)                       entry i can never be sent (empty, or does
//                                 not fit the buffer on its own); the caller
//                                 must stop treating it as pending
//
// Stops at maxTopics, or at the first pending entry that would overflow
// bufferBytes (that entry starts the next batch, so order is preserved).
// Returns the number of topics taken; 0 means nothing is pending.
template <typename IsPending, typename TopicLen, typename Take, typename Skip>
std::size_t planSubscribeBatch(const std::size_t count, const std::size_t bufferBytes, const std::size_t maxTopics,
                               IsPending&& isPending, TopicLen&& topicLen, Take&& take, Skip&& skip) {
    std::size_t taken = 0;
    std::size_t used = kSubscribeOverheadBytes;
    for (std::size_t i = 0; i < count && taken < maxTopics; ++i) {
        if (!isPending(i)) {
            continue;
        }
        const std::size_t len = topicLen(i);
        if (!subscribeTopicFits(len, bufferBytes)) {
            skip(i);
            continue;
        }
        const std::size_t cost = subscribeTopicCost(len);
        if (used + cost > bufferBytes) {
            break;
        }
        take(taken, i);
        used += cost;
        ++taken;
    }
    return taken;
}

} // namespace freebutton::mqtt

#endif // FREEBUTTON_SUBSCRIBEPLAN_H
