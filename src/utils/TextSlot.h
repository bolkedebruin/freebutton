//
// TextSlot: a fixed-size, heap-free handoff of one text value from any task
// (MQTT tasks, the JS task) to the LVGL task.
//
// Why: rendering a label needs the LVGL lock, which the LVGL task holds while
// it renders and flushes (mini-panel flushes also toggle chip-select over
// I2C). An MQTT task that waits for that lock stops reading its socket, and
// during the retained-message flood after connect the unread data piles up in
// lwIP / WiFi RX buffers until the heap runs out. So writers only copy the
// text into the slot and return; the LVGL task renders it on its next tick,
// under the lock it already holds. Several writes before that tick coalesce
// into one render of the newest text.
//
// Synchronisation: a sequence lock.
//   * Writers are serialised by one global spinlock (an ESP-IDF critical
//     section on the device). Inside it they make seq odd, copy at most
//     kBufferSize bytes, and make seq even again. No heap, no LVGL lock, no
//     waiting beyond another writer's copy of at most 91 bytes.
//   * The single reader (the LVGL task) copies the buffer to its own stack
//     (at most 91 bytes) when seq is even and differs from the last applied
//     seq, then checks seq again. If a writer ran during the copy it copies
//     again (up to kMaxCopyAttempts); if it is still unstable the text stays
//     pending for the next tick. Only a stable copy is rendered, once.
//
// The reader must not render straight from the slot: lv_label_set_text()
// measures the text, allocates that many bytes from the LVGL pool and then
// copies it in a second pass. A writer that made the text longer in between
// would overflow that allocation and corrupt the pool.
//
// Pure C++ (std::atomic only) so it is unit tested natively.
//

#ifndef FREEBUTTON_TEXTSLOT_H
#define FREEBUTTON_TEXTSLOT_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#endif

namespace freebutton {

// Length of the longest prefix of s that is at most max bytes and does not
// end inside a UTF-8 sequence. Backs off at most 3 continuation bytes (the
// longest valid sequence is 4 bytes); for invalid input it cuts at max.
constexpr size_t utf8Fit(const std::string_view s, const size_t max) {
    if (s.size() <= max) {
        return s.size();
    }
    size_t n = max;
    for (int k = 0; k < 3 && n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u; ++k) {
        --n;
    }
    if (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) {
        return max;  // not valid UTF-8 here; nothing better to do
    }
    return n;
}

namespace detail {

// One lock for all slots: a write holds it for one memcpy of at most 59
// bytes, so contention is negligible and no slot pays for its own lock.
#if defined(ESP_PLATFORM)
inline portMUX_TYPE textSlotMux = portMUX_INITIALIZER_UNLOCKED;
struct TextSlotWriterLock {
    TextSlotWriterLock() { portENTER_CRITICAL(&textSlotMux); }
    ~TextSlotWriterLock() { portEXIT_CRITICAL(&textSlotMux); }
};
#else
inline std::atomic_flag textSlotFlag = ATOMIC_FLAG_INIT;
struct TextSlotWriterLock {
    TextSlotWriterLock() {
        while (textSlotFlag.test_and_set(std::memory_order_acquire)) {
        }
    }
    ~TextSlotWriterLock() { textSlotFlag.clear(std::memory_order_release); }
};
#endif

#if defined(FREEBUTTON_TEST)
// Native tests only: called between the reader's copy and its re-check, so a
// test can simulate a writer preempting the copy.
inline void (*textSlotAfterCopyHook)() = nullptr;
#endif

} // namespace detail

enum class TextSlotApply : uint8_t {
    Clean,    // nothing new since the last render
    Applied,  // rendered the newest stable text
    Busy,     // a writer was active; still pending, retry next tick
};

// Bytes is the whole footprint of the slot: sizeof(TextSlot<Bytes>) == Bytes.
// It holds up to kCapacity bytes of text.
template <size_t Bytes>
class TextSlot {
    static_assert(Bytes >= 8 && Bytes % 4 == 0, "TextSlot size must be a multiple of 4");

public:
    static constexpr size_t kBufferSize = Bytes - 5;          // incl. terminator
    static constexpr size_t kCapacity = kBufferSize - 1;      // text bytes
    static constexpr int kMaxCopyAttempts = 3;

    struct WriteResult {
        bool truncated;        // text was cut to kCapacity at a UTF-8 boundary
        bool firstTruncation;  // ... and it is the first time for this slot
    };

    TextSlot() = default;
    TextSlot(const TextSlot&) = delete;
    TextSlot& operator=(const TextSlot&) = delete;

    // Any task. Copies s (cut at a UTF-8 boundary if longer than kCapacity).
    WriteResult write(const std::string_view s) {
        const size_t n = utf8Fit(s, kCapacity);
        WriteResult r{n < s.size(), false};

        detail::TextSlotWriterLock lock;
        const uint16_t s0 = seq_.load(std::memory_order_relaxed);
        seq_.store(static_cast<uint16_t>(s0 + 1), std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(buf_, s.data(), n);
        buf_[n] = '\0';
        seq_.store(static_cast<uint16_t>(s0 + 2), std::memory_order_release);
        if (r.truncated && !warned_) {
            warned_ = true;
            r.firstTruncation = true;
        }
        return r;
    }

    // Reader (LVGL task) only. If there is text newer than the last render,
    // takes a stable copy of it on the stack and calls render(const char*)
    // once with that copy. The pointer is valid only during the call.
    template <class Render>
    TextSlotApply apply(Render&& render) {
        char copy[kBufferSize];
        for (int attempt = 0; attempt < kMaxCopyAttempts; ++attempt) {
            const uint16_t s1 = seq_.load(std::memory_order_acquire);
            if (s1 == applied_) {
                return TextSlotApply::Clean;
            }
            if (s1 & 1u) {
                continue;  // a writer is mid-copy (sub-microsecond)
            }
            std::memcpy(copy, buf_, kBufferSize);
#if defined(FREEBUTTON_TEST)
            if (detail::textSlotAfterCopyHook) {
                detail::textSlotAfterCopyHook();
            }
#endif
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) != s1) {
                continue;  // overwritten while copying
            }
            copy[kBufferSize - 1] = '\0';  // already true; keeps strlen bounded
            applied_ = s1;
            render(static_cast<const char*>(copy));
            return TextSlotApply::Applied;
        }
        return TextSlotApply::Busy;
    }

    // Reader side (LVGL lock held). Render the current text again on the next
    // apply, e.g. after the widget was redrawn with its configured defaults.
    // No-op while the slot was never written, so the defaults stay.
    void invalidate() {
        if (seq_.load(std::memory_order_acquire) != 0) {
            applied_ = kNeverApplied;
        }
    }

    bool hasText() const { return seq_.load(std::memory_order_acquire) != 0; }

    // Any task; does not consume (the LVGL task still renders the update).
    // Copies the newest stable text into out. False when the slot was never
    // written, or writers kept it busy for every attempt.
    bool read(char (&out)[kBufferSize]) const {
        for (int attempt = 0; attempt < kMaxCopyAttempts; ++attempt) {
            const uint16_t s1 = seq_.load(std::memory_order_acquire);
            if (s1 == 0) {
                return false;
            }
            if (s1 & 1u) {
                continue;
            }
            std::memcpy(out, buf_, kBufferSize);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == s1) {
                out[kBufferSize - 1] = '\0';
                return true;
            }
        }
        return false;
    }

private:
    static constexpr uint16_t kNeverApplied = 0xFFFFu;  // odd: never equals a published seq

    // 16-bit sequence: it would take 32768 writes during one ~59-byte copy
    // for a wrap to hide a concurrent write.
    std::atomic<uint16_t> seq_{0};
    uint16_t applied_ = 0;   // reader only
    bool warned_ = false;    // writers only, under the writer lock
    char buf_[kBufferSize] = {};
};

} // namespace freebutton

#endif // FREEBUTTON_TEXTSLOT_H
