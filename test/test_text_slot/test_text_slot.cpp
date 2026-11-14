//
// Native unit tests for TextSlot: the heap-free handoff of display text from
// MQTT / JS tasks to the LVGL task (src/utils/TextSlot.h).
//
#include <unity.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "../../src/utils/TextSlot.h"
#include "../../src/components/SlotReset.h"

using freebutton::TextSlot;
using freebutton::TextSlotApply;
using freebutton::utf8Fit;

void setUp() {}
void tearDown() {}

// U+F075E (MDI volume icon): F3 B0 9D 9E. U+00E9 (e acute): C3 A9.
// U+20AC (euro sign): E2 82 AC.
static const std::string kIcon = "\xF3\xB0\x9D\x9E";
static const std::string kEacute = "\xC3\xA9";
static const std::string kEuro = "\xE2\x82\xAC";

// --- footprint ------------------------------------------------------------

void test_footprint_is_template_size() {
    TEST_ASSERT_EQUAL(64, sizeof(TextSlot<64>));
    TEST_ASSERT_EQUAL(32, sizeof(TextSlot<32>));
    TEST_ASSERT_EQUAL(58, TextSlot<64>::kCapacity);
    TEST_ASSERT_EQUAL(26, TextSlot<32>::kCapacity);
}

// --- utf8Fit --------------------------------------------------------------

void test_fit_short_and_exact() {
    TEST_ASSERT_EQUAL(0, utf8Fit("", 4));
    TEST_ASSERT_EQUAL(3, utf8Fit("abc", 4));
    TEST_ASSERT_EQUAL(4, utf8Fit("abcd", 4));
    TEST_ASSERT_EQUAL(4, utf8Fit("abcdef", 4));
}

void test_fit_never_splits_a_sequence() {
    // "ab" + 4-byte icon + "c": every cut inside the icon falls back to 2.
    const std::string s = "ab" + kIcon + "c";
    TEST_ASSERT_EQUAL(2, utf8Fit(s, 2));
    TEST_ASSERT_EQUAL(2, utf8Fit(s, 3));
    TEST_ASSERT_EQUAL(2, utf8Fit(s, 4));
    TEST_ASSERT_EQUAL(2, utf8Fit(s, 5));
    TEST_ASSERT_EQUAL(6, utf8Fit(s, 6));
    TEST_ASSERT_EQUAL(7, utf8Fit(s, 7));

    const std::string e = "x" + kEacute + kEuro;  // 1 + 2 + 3 bytes
    TEST_ASSERT_EQUAL(1, utf8Fit(e, 1));
    TEST_ASSERT_EQUAL(1, utf8Fit(e, 2));
    TEST_ASSERT_EQUAL(3, utf8Fit(e, 3));
    TEST_ASSERT_EQUAL(3, utf8Fit(e, 4));
    TEST_ASSERT_EQUAL(3, utf8Fit(e, 5));
    TEST_ASSERT_EQUAL(6, utf8Fit(e, 6));
}

void test_fit_invalid_utf8_cuts_at_max() {
    // A run of continuation bytes without a lead byte: no boundary to find.
    const std::string bad(10, '\x80');
    TEST_ASSERT_EQUAL(6, utf8Fit(bad, 6));
}

// --- write / apply ----------------------------------------------------------

struct Recorder {
    int renders = 0;
    std::string last;
    void operator()(const char* t) {
        ++renders;
        last = t;
    }
};

void test_empty_slot_renders_nothing() {
    TextSlot<32> slot;
    Recorder r;
    TEST_ASSERT_FALSE(slot.hasText());
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Clean), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL(0, r.renders);
}

void test_read_does_not_consume() {
    TextSlot<32> slot;
    char out[TextSlot<32>::kBufferSize];
    TEST_ASSERT_FALSE(slot.read(out));  // never written
    slot.write("45%");
    TEST_ASSERT_TRUE(slot.read(out));
    TEST_ASSERT_EQUAL_STRING("45%", out);
    TEST_ASSERT_TRUE(slot.read(out));    // reading again is fine
    Recorder r;                          // the renderer still gets the update
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Applied), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL_STRING("45%", r.last.c_str());
    TEST_ASSERT_TRUE(slot.read(out));    // and read still works after a render
    TEST_ASSERT_EQUAL_STRING("45%", out);
    slot.write("");
    TEST_ASSERT_TRUE(slot.read(out));    // an empty runtime label is still runtime text
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_write_then_apply_once() {
    TextSlot<32> slot;
    Recorder r;
    const auto w = slot.write("Vol: 15%");
    TEST_ASSERT_FALSE(w.truncated);
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Applied), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL_STRING("Vol: 15%", r.last.c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Clean), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL(1, r.renders);
}

void test_burst_coalesces_into_one_render() {
    TextSlot<64> slot;
    Recorder r;
    slot.write("7 Nov 2026");
    slot.write("\\cec9006" + kIcon);
    slot.write("Bohemian Rhapsody");
    slot.apply(r);
    TEST_ASSERT_EQUAL(1, r.renders);
    TEST_ASSERT_EQUAL_STRING("Bohemian Rhapsody", r.last.c_str());
}

void test_empty_payload_is_rendered() {
    // An empty retained value clears the label; it must not look "unset".
    TextSlot<32> slot;
    Recorder r;
    slot.write("x");
    slot.apply(r);
    slot.write("");
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Applied), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL_STRING("", r.last.c_str());
}

void test_truncation_at_boundary_and_logged_once() {
    TextSlot<32> slot;  // 26 bytes of text
    Recorder r;
    // 24 ASCII + icon = 28 bytes: the icon does not fit and is dropped whole.
    const std::string longText = std::string(24, 'a') + kIcon;
    const auto w1 = slot.write(longText);
    TEST_ASSERT_TRUE(w1.truncated);
    TEST_ASSERT_TRUE(w1.firstTruncation);
    slot.apply(r);
    TEST_ASSERT_EQUAL_STRING(std::string(24, 'a').c_str(), r.last.c_str());

    const auto w2 = slot.write(longText);
    TEST_ASSERT_TRUE(w2.truncated);
    TEST_ASSERT_FALSE(w2.firstTruncation);

    const auto w3 = slot.write(std::string(26, 'b'));  // exactly full
    TEST_ASSERT_FALSE(w3.truncated);
    slot.apply(r);
    TEST_ASSERT_EQUAL(26, r.last.size());
}

void test_invalidate_rerenders_only_written_slots() {
    TextSlot<32> slot;
    Recorder r;
    slot.invalidate();  // never written: keep the configured default
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Clean), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL(0, r.renders);

    slot.write("Raam");
    slot.apply(r);
    slot.invalidate();  // widget redrawn with defaults: show "Raam" again
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Applied), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL(2, r.renders);
    TEST_ASSERT_EQUAL_STRING("Raam", r.last.c_str());
}

// Script engine restart (components/SlotReset.h): a reset slot is "never
// written" again, so the configured default stays, and takes new text after.
void test_reset_slot_is_never_written_again() {
    TextSlot<32> slot;
    Recorder r;
    slot.write("Raam");
    slot.apply(r);
    TEST_ASSERT_EQUAL(1, r.renders);

    freebutton::resetSlot(slot);
    char out[TextSlot<32>::kBufferSize];
    TEST_ASSERT_FALSE(slot.hasText());
    TEST_ASSERT_FALSE(slot.read(out));
    slot.invalidate();  // the widget was redrawn with its defaults
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Clean), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL(1, r.renders);

    slot.write("Deur");
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Applied), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL(2, r.renders);
    TEST_ASSERT_EQUAL_STRING("Deur", r.last.c_str());

    freebutton::resetSlot(slot);  // twice in a row, and on a reset slot
    freebutton::resetSlot(slot);
    TEST_ASSERT_FALSE(slot.hasText());
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Clean), static_cast<int>(slot.apply(r)));
}

// The renderer gets a private copy: a writer that changes (and lengthens) the
// slot while the render runs must not change the text being rendered, as
// lv_label_set_text() measures and copies in two passes. The newer text is
// rendered on the next apply.
void test_render_sees_stable_copy_while_slot_changes() {
    TextSlot<32> slot;
    slot.write("old");
    int renders = 0;
    std::string seenBefore;
    std::string seenAfter;
    const auto result = slot.apply([&](const char* t) {
        ++renders;
        seenBefore = t;
        slot.write("a much longer new text");  // the MQTT task preempts the render
        seenAfter = t;
    });
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Applied), static_cast<int>(result));
    TEST_ASSERT_EQUAL(1, renders);
    TEST_ASSERT_EQUAL_STRING("old", seenBefore.c_str());
    TEST_ASSERT_EQUAL_STRING("old", seenAfter.c_str());

    Recorder r;
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Applied), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL_STRING("a much longer new text", r.last.c_str());
}

static TextSlot<32>* g_hookSlot = nullptr;
static int g_hookWrites = 0;
static int g_hookLimit = 0;

static void writeDuringCopy() {
    if (g_hookWrites < g_hookLimit) {
        ++g_hookWrites;
        g_hookSlot->write("v" + std::to_string(g_hookWrites));
    }
}

// A write that lands during the reader's copy: the copy is retried and the
// newest text is rendered once.
void test_write_during_copy_retries_and_renders_once() {
    TextSlot<32> slot;
    slot.write("v0");
    g_hookSlot = &slot;
    g_hookWrites = 0;
    g_hookLimit = 1;
    freebutton::detail::textSlotAfterCopyHook = writeDuringCopy;
    Recorder r;
    const auto result = slot.apply(r);
    freebutton::detail::textSlotAfterCopyHook = nullptr;
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Applied), static_cast<int>(result));
    TEST_ASSERT_EQUAL(1, r.renders);
    TEST_ASSERT_EQUAL_STRING("v1", r.last.c_str());
}

// Every copy overwritten: after kMaxCopyAttempts the reader renders nothing,
// reports Busy, and the text is still pending for the next tick.
void test_continuous_writes_report_busy_and_stay_pending() {
    TextSlot<32> slot;
    slot.write("v0");
    g_hookSlot = &slot;
    g_hookWrites = 0;
    g_hookLimit = TextSlot<32>::kMaxCopyAttempts;
    freebutton::detail::textSlotAfterCopyHook = writeDuringCopy;
    Recorder r;
    const auto result = slot.apply(r);
    freebutton::detail::textSlotAfterCopyHook = nullptr;
    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Busy), static_cast<int>(result));
    TEST_ASSERT_EQUAL(0, r.renders);

    TEST_ASSERT_EQUAL(static_cast<int>(TextSlotApply::Applied), static_cast<int>(slot.apply(r)));
    TEST_ASSERT_EQUAL(1, r.renders);
    TEST_ASSERT_EQUAL_STRING(("v" + std::to_string(TextSlot<32>::kMaxCopyAttempts)).c_str(),
                             r.last.c_str());
}

// Two threads: a writer hammering the slot with uniform strings
// ("kkkk", length varies), the reader applying. Every render must get an
// untorn text (one repeated character) that stays inside the buffer and does
// not change while the render runs (measured twice, as lv_label_set_text
// does).
void test_concurrent_writer_never_yields_torn_render() {
    TextSlot<64> slot;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        unsigned i = 0;
        char buf[TextSlot<64>::kCapacity + 1];
        while (!stop.load(std::memory_order_relaxed)) {
            const char c = static_cast<char>('a' + (i % 26));
            const size_t len = 1 + (i * 7) % TextSlot<64>::kCapacity;
            std::memset(buf, c, len);
            slot.write(std::string_view(buf, len));
            ++i;
        }
    });

    // Collect failures and assert after join: a Unity failure inside the loop
    // would leave the writer thread running.
    int renders = 0;
    int overruns = 0;
    int torn = 0;
    int changed = 0;
    for (int n = 0; n < 200000; ++n) {
        slot.apply([&](const char* t) {
            ++renders;
            const size_t len = strnlen(t, TextSlot<64>::kBufferSize);
            if (len > TextSlot<64>::kCapacity) {
                ++overruns;
            }
            if (len == 0) {
                ++torn;
            }
            for (size_t k = 1; k < len; ++k) {
                if (t[k] != t[0]) {
                    ++torn;
                    break;
                }
            }
            std::this_thread::yield();  // give the writer a chance to run mid-render
            if (strnlen(t, TextSlot<64>::kBufferSize) != len) {
                ++changed;
            }
        });
    }
    stop.store(true);
    writer.join();
    TEST_ASSERT_EQUAL(0, overruns);
    TEST_ASSERT_EQUAL(0, torn);
    TEST_ASSERT_EQUAL(0, changed);
    TEST_ASSERT_TRUE(renders > 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_footprint_is_template_size);
    RUN_TEST(test_fit_short_and_exact);
    RUN_TEST(test_fit_never_splits_a_sequence);
    RUN_TEST(test_fit_invalid_utf8_cuts_at_max);
    RUN_TEST(test_empty_slot_renders_nothing);
    RUN_TEST(test_read_does_not_consume);
    RUN_TEST(test_write_then_apply_once);
    RUN_TEST(test_burst_coalesces_into_one_render);
    RUN_TEST(test_empty_payload_is_rendered);
    RUN_TEST(test_truncation_at_boundary_and_logged_once);
    RUN_TEST(test_invalidate_rerenders_only_written_slots);
    RUN_TEST(test_reset_slot_is_never_written_again);
    RUN_TEST(test_render_sees_stable_copy_while_slot_changes);
    RUN_TEST(test_write_during_copy_retries_and_renders_once);
    RUN_TEST(test_continuous_writes_report_busy_and_stay_pending);
    RUN_TEST(test_concurrent_writer_never_yields_torn_render);
    return UNITY_END();
}
