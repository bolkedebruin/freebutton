//
// MessageRing: a bounded FIFO of variable-size records (kind, channel, topic,
// payload) in one fixed byte array. No heap.
//
// Used to hand MQTT messages from the MQTT tasks to the task that runs
// JavaScript (src/scripting/mqtt_binding.cpp): the MQTT task copies the
// message out of esp-mqtt's receive buffer into the ring and returns at once;
// it never waits for script code. When the ring is full, push() fails and the
// caller drops the message (and counts it).
//
// Each record is contiguous in the array, so the consumer reads topic and
// payload in place (front() returns views into the array). A record that does
// not fit before the end of the array starts at offset 0; the rest of the
// array is skipped with a one-byte wrap marker.
//
// Synchronisation: none inside. The owner serialises push(), front() and
// pop() with one lock. The consumer may keep using the views from front()
// after it releases the lock, until it calls pop(): push() only writes into
// free space, and a record is free only after pop().
//
// Pure C++ (no FreeRTOS) so it is unit tested natively.
//

#ifndef FREEBUTTON_MESSAGERING_H
#define FREEBUTTON_MESSAGERING_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace freebutton {

template <size_t Bytes>
class MessageRing {
    static_assert(Bytes >= 16 && (Bytes & (Bytes - 1)) == 0, "Bytes must be a power of two");

public:
    // Header: kind, channel, topic length (u16 LE), payload length (u16 LE).
    static constexpr size_t kHeaderBytes = 6;
    // Reserved kind: marks the unused end of the array.
    static constexpr uint8_t kWrapMarker = 0xFF;
    static constexpr size_t kCapacity = Bytes;

    struct Record {
        uint8_t kind = 0;
        uint8_t channel = 0;
        std::string_view topic;
        std::string_view payload;
    };

    // Bytes one record occupies (excluding a wrap gap before it).
    static constexpr size_t recordBytes(const size_t topicLen, const size_t payloadLen) {
        return kHeaderBytes + topicLen + payloadLen;
    }

    // Copy a record in. False when it does not fit in the free space (the
    // caller drops it), when it is larger than the array, or kind is the
    // reserved wrap marker.
    bool push(const uint8_t kind, const uint8_t channel, const std::string_view topic,
              const std::string_view payload) {
        if (kind == kWrapMarker || topic.size() > 0xFFFF || payload.size() > 0xFFFF) {
            return false;
        }
        const size_t size = recordBytes(topic.size(), payload.size());
        if (size > Bytes) {
            return false;
        }
        if (head_ == tail_) {
            // Empty and no record is held by the consumer: start at offset 0
            // so that any record up to Bytes fits.
            head_ = tail_ = 0;
        }
        size_t index = head_ & kMask;
        const size_t toEnd = Bytes - index;
        const size_t gap = toEnd < size ? toEnd : 0;
        if (Bytes - used() < gap + size) {
            return false;
        }
        if (gap != 0) {
            buf_[index] = kWrapMarker;
            head_ += static_cast<uint32_t>(gap);
            index = 0;
        }
        uint8_t* p = buf_ + index;
        p[0] = kind;
        p[1] = channel;
        p[2] = static_cast<uint8_t>(topic.size() & 0xFF);
        p[3] = static_cast<uint8_t>(topic.size() >> 8);
        p[4] = static_cast<uint8_t>(payload.size() & 0xFF);
        p[5] = static_cast<uint8_t>(payload.size() >> 8);
        if (!topic.empty()) {
            std::memcpy(p + kHeaderBytes, topic.data(), topic.size());
        }
        if (!payload.empty()) {
            std::memcpy(p + kHeaderBytes + topic.size(), payload.data(), payload.size());
        }
        head_ += static_cast<uint32_t>(size);
        return true;
    }

    // The oldest record, read in place. False when empty. The views stay
    // valid until pop().
    bool front(Record& out) {
        while (head_ != tail_) {
            const size_t index = tail_ & kMask;
            const uint8_t* p = buf_ + index;
            if (p[0] == kWrapMarker) {
                tail_ += static_cast<uint32_t>(Bytes - index);
                continue;
            }
            const size_t topicLen = p[2] | (static_cast<size_t>(p[3]) << 8);
            const size_t payloadLen = p[4] | (static_cast<size_t>(p[5]) << 8);
            const char* text = reinterpret_cast<const char*>(p + kHeaderBytes);
            out.kind = p[0];
            out.channel = p[1];
            out.topic = std::string_view(text, topicLen);
            out.payload = std::string_view(text + topicLen, payloadLen);
            return true;
        }
        return false;
    }

    // Release the record front() returned. No-op when empty.
    void pop() {
        Record record;
        if (front(record)) {
            tail_ += static_cast<uint32_t>(recordBytes(record.topic.size(), record.payload.size()));
        }
    }

    bool empty() const { return head_ == tail_; }

    // Bytes in use, including wrap gaps.
    size_t used() const { return static_cast<size_t>(head_ - tail_); }

private:
    static constexpr size_t kMask = Bytes - 1;

    uint8_t buf_[Bytes] = {};
    // Free-running byte counters; index = counter & kMask. Bytes divides
    // 2^32, so the counters may wrap.
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
};

} // namespace freebutton

#endif // FREEBUTTON_MESSAGERING_H
