//
// Native unit tests for MessageRing: the heap-free handoff of MQTT messages
// from the MQTT tasks to the JavaScript task (src/utils/MessageRing.h).
//
#include <unity.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "../../src/utils/MessageRing.h"

using freebutton::MessageRing;

void setUp() {}
void tearDown() {}

using Ring64 = MessageRing<64>;

static std::string str(const std::string_view v) { return std::string(v); }

void test_empty_ring() {
    Ring64 ring;
    Ring64::Record r;
    TEST_ASSERT_TRUE(ring.empty());
    TEST_ASSERT_FALSE(ring.front(r));
    ring.pop();  // no-op
    TEST_ASSERT_TRUE(ring.empty());
    TEST_ASSERT_EQUAL(0, ring.used());
}

void test_fifo_order_and_fields() {
    Ring64 ring;
    TEST_ASSERT_TRUE(ring.push(1, 0, "a/b", "12"));
    TEST_ASSERT_TRUE(ring.push(2, 1, "c", ""));
    TEST_ASSERT_TRUE(ring.push(3, 1, "", ""));
    TEST_ASSERT_EQUAL(6 + 5 + 6 + 1 + 6, ring.used());

    Ring64::Record r;
    TEST_ASSERT_TRUE(ring.front(r));
    TEST_ASSERT_EQUAL(1, r.kind);
    TEST_ASSERT_EQUAL(0, r.channel);
    TEST_ASSERT_EQUAL_STRING("a/b", str(r.topic).c_str());
    TEST_ASSERT_EQUAL_STRING("12", str(r.payload).c_str());
    ring.pop();

    TEST_ASSERT_TRUE(ring.front(r));
    TEST_ASSERT_EQUAL(2, r.kind);
    TEST_ASSERT_EQUAL(1, r.channel);
    TEST_ASSERT_EQUAL_STRING("c", str(r.topic).c_str());
    TEST_ASSERT_EQUAL(0, r.payload.size());
    ring.pop();

    TEST_ASSERT_TRUE(ring.front(r));
    TEST_ASSERT_EQUAL(3, r.kind);
    TEST_ASSERT_EQUAL(0, r.topic.size());
    ring.pop();

    TEST_ASSERT_TRUE(ring.empty());
    TEST_ASSERT_FALSE(ring.front(r));
}

void test_front_without_pop_is_stable() {
    Ring64 ring;
    TEST_ASSERT_TRUE(ring.push(1, 0, "t", "p"));
    Ring64::Record a, b;
    TEST_ASSERT_TRUE(ring.front(a));
    TEST_ASSERT_TRUE(ring.front(b));
    TEST_ASSERT_EQUAL_PTR(a.topic.data(), b.topic.data());
}

void test_full_ring_rejects_and_recovers() {
    Ring64 ring;
    // 4 records of 16 bytes fill 64 exactly.
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(ring.push(1, 0, "topic", "12345"));
    }
    TEST_ASSERT_EQUAL(64, ring.used());
    TEST_ASSERT_FALSE(ring.push(1, 0, "", ""));

    Ring64::Record r;
    TEST_ASSERT_TRUE(ring.front(r));
    ring.pop();
    TEST_ASSERT_TRUE(ring.push(1, 0, "x", "123456789"));  // 16 bytes, at offset 0 again
    for (int i = 0; i < 4; ++i) {
        TEST_ASSERT_TRUE(ring.front(r));
        ring.pop();
    }
    TEST_ASSERT_TRUE(ring.empty());
}

void test_wrap_gap_is_skipped() {
    Ring64 ring;
    // 40 + 16 = 56 bytes used, 8 left at the end.
    TEST_ASSERT_TRUE(ring.push(1, 0, "0123456789", "012345678901234567890123"));  // 6+10+24 = 40
    TEST_ASSERT_TRUE(ring.push(2, 0, "topic", "12345"));                          // 16
    Ring64::Record r;
    TEST_ASSERT_TRUE(ring.front(r));
    TEST_ASSERT_EQUAL(1, r.kind);
    ring.pop();  // 40 bytes free at the start

    // 20-byte record: 8 bytes at the end are too few, so it goes to offset 0
    // (gap of 8) and stays contiguous.
    TEST_ASSERT_TRUE(ring.push(3, 7, "wrapped", "0123456"));  // 6+7+7 = 20
    TEST_ASSERT_EQUAL(16 + 8 + 20, ring.used());

    TEST_ASSERT_TRUE(ring.front(r));
    TEST_ASSERT_EQUAL(2, r.kind);
    ring.pop();
    TEST_ASSERT_TRUE(ring.front(r));
    TEST_ASSERT_EQUAL(3, r.kind);
    TEST_ASSERT_EQUAL(7, r.channel);
    TEST_ASSERT_EQUAL_STRING("wrapped", str(r.topic).c_str());
    TEST_ASSERT_EQUAL_STRING("0123456", str(r.payload).c_str());
    ring.pop();
    TEST_ASSERT_TRUE(ring.empty());
    TEST_ASSERT_EQUAL(0, ring.used());
}

void test_wrap_needs_room_at_start() {
    Ring64 ring;
    TEST_ASSERT_TRUE(ring.push(1, 0, "0123456789", "012345678901234567890123"));  // 40 at 0
    TEST_ASSERT_TRUE(ring.push(2, 0, "topic", "12345"));                          // 16 at 40
    // 8 bytes left at the end, start is occupied: a 12-byte record fits
    // nowhere.
    TEST_ASSERT_FALSE(ring.push(3, 0, "ab", "cdef"));
    // An 8-byte record fits exactly at the end.
    TEST_ASSERT_TRUE(ring.push(4, 0, "ab", ""));
    TEST_ASSERT_EQUAL(64, ring.used());
}

void test_largest_record_fits_in_empty_ring() {
    Ring64 ring;
    const std::string topic(10, 't');
    const std::string payload(64 - 6 - 10, 'p');
    TEST_ASSERT_TRUE(ring.push(1, 0, "x", "y"));
    Ring64::Record r;
    TEST_ASSERT_TRUE(ring.front(r));
    ring.pop();  // empty again, head not at 0
    TEST_ASSERT_TRUE(ring.push(1, 0, topic, payload));
    TEST_ASSERT_TRUE(ring.front(r));
    TEST_ASSERT_EQUAL(topic.size(), r.topic.size());
    TEST_ASSERT_EQUAL(payload.size(), r.payload.size());
    ring.pop();
    TEST_ASSERT_FALSE(ring.push(1, 0, topic, payload + "!"));  // larger than the ring
}

void test_reserved_kind_rejected() {
    Ring64 ring;
    TEST_ASSERT_FALSE(ring.push(Ring64::kWrapMarker, 0, "t", "p"));
    TEST_ASSERT_TRUE(ring.empty());
}

void test_long_run_keeps_order() {
    MessageRing<256> ring;
    MessageRing<256>::Record r;
    unsigned produced = 0, consumed = 0;
    char topic[32], payload[64];
    for (int round = 0; round < 20000; ++round) {
        // Variable sizes so records wrap at many different offsets.
        const int n = 1 + round % 3;
        for (int i = 0; i < n; ++i) {
            const int tl = snprintf(topic, sizeof(topic), "t/%u", produced);
            const int pl = snprintf(payload, sizeof(payload), "%0*u", 1 + static_cast<int>(produced % 40), produced);
            if (!ring.push(static_cast<uint8_t>(produced % 200), 0, std::string_view(topic, tl),
                           std::string_view(payload, pl))) {
                break;
            }
            ++produced;
        }
        if (round % 2 == 0) {
            continue;
        }
        while (ring.front(r)) {
            const int tl = snprintf(topic, sizeof(topic), "t/%u", consumed);
            TEST_ASSERT_EQUAL(consumed % 200, r.kind);
            TEST_ASSERT_EQUAL(static_cast<size_t>(tl), r.topic.size());
            TEST_ASSERT_EQUAL_MEMORY(topic, r.topic.data(), tl);
            ring.pop();
            ++consumed;
        }
    }
    TEST_ASSERT_GREATER_THAN(10000, consumed);
}

// The documented contract: the consumer reads a record in place without the
// lock while producers push, and pops it under the lock afterwards.
void test_concurrent_producers_consumer_reads_in_place() {
    static MessageRing<512> ring;
    std::mutex lock;
    std::atomic<bool> done{false};
    constexpr unsigned kPerProducer = 20000;
    std::atomic<unsigned> dropped{0};

    auto producer = [&](const uint8_t channel) {
        char payload[48];
        for (unsigned i = 0; i < kPerProducer; ++i) {
            const int pl = snprintf(payload, sizeof(payload), "%u:%0*u", i, static_cast<int>(i % 30), i);
            std::lock_guard<std::mutex> guard(lock);
            if (!ring.push(1, channel, "topic", std::string_view(payload, pl))) {
                ++dropped;
            }
        }
    };

    unsigned received[2] = {0, 0};
    long lastSeq[2] = {-1, -1};
    std::thread a(producer, 0), b(producer, 1);
    std::thread consumer([&] {
        MessageRing<512>::Record r;
        for (;;) {
            bool have;
            {
                std::lock_guard<std::mutex> guard(lock);
                have = ring.front(r);
            }
            if (!have) {
                if (done.load()) {
                    std::lock_guard<std::mutex> guard(lock);
                    if (ring.empty()) break;
                    continue;
                }
                std::this_thread::yield();
                continue;
            }
            // Outside the lock: the record must stay intact.
            unsigned seq = 0;
            size_t i = 0;
            while (i < r.payload.size() && r.payload[i] != ':') {
                seq = seq * 10 + (r.payload[i] - '0');
                ++i;
            }
            char expect[48];
            const int el = snprintf(expect, sizeof(expect), "%u:%0*u", seq, static_cast<int>(seq % 30), seq);
            TEST_ASSERT_EQUAL(static_cast<size_t>(el), r.payload.size());
            TEST_ASSERT_EQUAL_MEMORY(expect, r.payload.data(), el);
            TEST_ASSERT_LESS_THAN(2, r.channel);
            TEST_ASSERT_GREATER_THAN(lastSeq[r.channel], static_cast<long>(seq));  // per-producer order
            lastSeq[r.channel] = seq;
            ++received[r.channel];
            {
                std::lock_guard<std::mutex> guard(lock);
                ring.pop();
            }
        }
    });
    a.join();
    b.join();
    done.store(true);
    consumer.join();
    TEST_ASSERT_EQUAL(2 * kPerProducer, received[0] + received[1] + dropped.load());
    TEST_ASSERT_GREATER_THAN(0, received[0]);
    TEST_ASSERT_GREATER_THAN(0, received[1]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_ring);
    RUN_TEST(test_fifo_order_and_fields);
    RUN_TEST(test_front_without_pop_is_stable);
    RUN_TEST(test_full_ring_rejects_and_recovers);
    RUN_TEST(test_wrap_gap_is_skipped);
    RUN_TEST(test_wrap_needs_room_at_start);
    RUN_TEST(test_largest_record_fits_in_empty_ring);
    RUN_TEST(test_reserved_kind_rejected);
    RUN_TEST(test_long_run_keeps_order);
    RUN_TEST(test_concurrent_producers_consumer_reads_in_place);
    return UNITY_END();
}
