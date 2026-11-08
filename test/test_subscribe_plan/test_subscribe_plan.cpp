//
// Native unit tests for the SUBSCRIBE batch planner used by the batched,
// SUBACK-driven resubscribe in MqttBroker.
//
#include <unity.h>
#include <string>
#include <vector>

#include "../../src/mqtt/SubscribePlan.h"

using namespace freebutton::mqtt;

void setUp() {}
void tearDown() {}

namespace {

struct Entry {
    std::string topic;
    bool sent = false;
};

// Runs the planner once over entries, marks taken/skipped entries as sent,
// returns the taken indices.
std::vector<size_t> nextBatch(std::vector<Entry>& entries, const size_t buffer, const size_t maxTopics,
                              std::vector<size_t>* skipped = nullptr) {
    std::vector<size_t> taken;
    const size_t n = planSubscribeBatch(
        entries.size(), buffer, maxTopics, [&](size_t i) { return !entries[i].sent; },
        [&](size_t i) { return entries[i].topic.size(); },
        [&](size_t k, size_t i) {
            TEST_ASSERT_EQUAL_size_t(taken.size(), k);
            taken.push_back(i);
        },
        [&](size_t i) {
            entries[i].sent = true;
            if (skipped) skipped->push_back(i);
        });
    TEST_ASSERT_EQUAL_size_t(taken.size(), n);
    for (const size_t i : taken) entries[i].sent = true;
    return taken;
}

size_t encodedSize(const std::vector<Entry>& entries, const std::vector<size_t>& batch) {
    size_t total = kSubscribeOverheadBytes;
    for (const size_t i : batch) total += subscribeTopicCost(entries[i].topic.size());
    return total;
}

std::vector<Entry> deviceTopics(const size_t count) {
    // Shaped like the btn_a00001 topics: "buttonplus/btn_a00001/button/N/long_press" (41 chars).
    std::vector<Entry> entries;
    for (size_t i = 0; i < count; ++i) {
        entries.push_back({"buttonplus/btn_a00001/button/" + std::to_string(i % 10) + "/long_press"});
    }
    return entries;
}

} // namespace

void test_cost_matches_esp_mqtt_layout() {
    // 2 B length + filter + 1 B QoS
    TEST_ASSERT_EQUAL_size_t(44, subscribeTopicCost(41));
    TEST_ASSERT_EQUAL_size_t(7, kSubscribeOverheadBytes);
    // Exactly filling the buffer fits (esp-mqtt checks length + n > buffer_length).
    TEST_ASSERT_TRUE(subscribeTopicFits(512 - 7 - 3, 512));
    TEST_ASSERT_FALSE(subscribeTopicFits(512 - 7 - 3 + 1, 512));
    TEST_ASSERT_FALSE(subscribeTopicFits(0, 512));
}

void test_splits_48_topics_into_batches_of_8() {
    auto entries = deviceTopics(48);
    size_t batches = 0;
    size_t total = 0;
    size_t expectNext = 0;
    for (;;) {
        const auto batch = nextBatch(entries, 512, 8);
        if (batch.empty()) break;
        ++batches;
        TEST_ASSERT_LESS_OR_EQUAL_size_t(8, batch.size());
        TEST_ASSERT_LESS_OR_EQUAL_size_t(512, encodedSize(entries, batch));
        for (const size_t i : batch) TEST_ASSERT_EQUAL_size_t(expectNext++, i);  // order preserved
        total += batch.size();
    }
    TEST_ASSERT_EQUAL_size_t(6, batches);
    TEST_ASSERT_EQUAL_size_t(48, total);
}

void test_long_topics_split_by_buffer_size() {
    // 100-char topics cost 103 B: (512 - 7) / 103 = 4 per packet.
    std::vector<Entry> entries;
    for (int i = 0; i < 10; ++i) entries.push_back({std::string(100, 'a' + i)});
    const auto first = nextBatch(entries, 512, 8);
    TEST_ASSERT_EQUAL_size_t(4, first.size());
    TEST_ASSERT_LESS_OR_EQUAL_size_t(512, encodedSize(entries, first));
    TEST_ASSERT_EQUAL_size_t(4, nextBatch(entries, 512, 8).size());
    TEST_ASSERT_EQUAL_size_t(2, nextBatch(entries, 512, 8).size());
    TEST_ASSERT_EQUAL_size_t(0, nextBatch(entries, 512, 8).size());
}

void test_stops_at_first_topic_that_does_not_fit() {
    // 300 + 250: the second does not fit after the first, so it starts the next
    // batch even though a later short topic would have fitted.
    std::vector<Entry> entries = {{std::string(300, 'a')}, {std::string(250, 'b')}, {"short"}};
    auto batch = nextBatch(entries, 512, 8);
    TEST_ASSERT_EQUAL_size_t(1, batch.size());
    TEST_ASSERT_EQUAL_size_t(0, batch[0]);
    batch = nextBatch(entries, 512, 8);
    TEST_ASSERT_EQUAL_size_t(2, batch.size());
    TEST_ASSERT_EQUAL_size_t(1, batch[0]);
    TEST_ASSERT_EQUAL_size_t(2, batch[1]);
}

void test_skips_oversized_and_empty_topics() {
    std::vector<Entry> entries = {{"a/b"}, {std::string(600, 'x')}, {""}, {"c/d"}};
    std::vector<size_t> skipped;
    const auto batch = nextBatch(entries, 512, 8, &skipped);
    TEST_ASSERT_EQUAL_size_t(2, batch.size());
    TEST_ASSERT_EQUAL_size_t(0, batch[0]);
    TEST_ASSERT_EQUAL_size_t(3, batch[1]);
    TEST_ASSERT_EQUAL_size_t(2, skipped.size());
    TEST_ASSERT_EQUAL_size_t(1, skipped[0]);
    TEST_ASSERT_EQUAL_size_t(2, skipped[1]);
    TEST_ASSERT_EQUAL_size_t(0, nextBatch(entries, 512, 8).size());
}

void test_only_pending_entries_are_taken() {
    // Entries already sent (e.g. subscribed directly) are not resent; entries
    // appended mid-sequence are picked up by the next batch.
    auto entries = deviceTopics(5);
    entries[1].sent = true;
    entries[3].sent = true;
    auto batch = nextBatch(entries, 512, 8);
    TEST_ASSERT_EQUAL_size_t(3, batch.size());
    TEST_ASSERT_EQUAL_size_t(0, batch[0]);
    TEST_ASSERT_EQUAL_size_t(2, batch[1]);
    TEST_ASSERT_EQUAL_size_t(4, batch[2]);

    entries.push_back({"late/topic"});
    batch = nextBatch(entries, 512, 8);
    TEST_ASSERT_EQUAL_size_t(1, batch.size());
    TEST_ASSERT_EQUAL_size_t(5, batch[0]);
}

void test_empty_list() {
    std::vector<Entry> entries;
    TEST_ASSERT_EQUAL_size_t(0, nextBatch(entries, 512, 8).size());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cost_matches_esp_mqtt_layout);
    RUN_TEST(test_splits_48_topics_into_batches_of_8);
    RUN_TEST(test_long_topics_split_by_buffer_size);
    RUN_TEST(test_stops_at_first_topic_that_does_not_fit);
    RUN_TEST(test_skips_oversized_and_empty_topics);
    RUN_TEST(test_only_pending_entries_are_taken);
    RUN_TEST(test_empty_list);
    return UNITY_END();
}
