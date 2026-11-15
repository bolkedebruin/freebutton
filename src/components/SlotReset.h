//
// SlotReset: puts a TextSlot (utils/TextSlot.h) back to "never written", so
// the widget shows its configured default again. Used when the script engine
// restarts (ConfigManager::restoreConfigState): a text a script set must not
// outlive the script.
//
// The caller holds the LVGL lock: the slot's reader (the LVGL task, in
// applyPending()) is then not running, so the reader-only fields can be
// written. Writers on other tasks (MQTT) are kept out by the slots' writer
// lock, taken here.
//
// The slot is constructed again in place because TextSlot has no reset of its
// own. A read() on a third task (the config writer, GET /config) that runs at
// the same moment sees the sequence go to 0 and reports "never written" or
// retries; the write of the 16-bit sequence is one store on this CPU.
// A TextSlot::reset() that does the same under its own writer lock would be
// the cleaner home for this.
//
// Also here: the "last writer" bookkeeping that decides which slots are
// reset. A widget keeps one bit per slot in an atomic byte (its scriptWrote_):
// a script setter sets the bit after it stored the text
// (storeFromScript), a message on the widget's own config topic clears it
// (storeFromConfigTopic). Only a text from a script is dropped at the restart.
// Not exact when both write the same slot at the same moment: then the bit
// can name the wrong writer once.
//

#ifndef FREEBUTTON_SLOTRESET_H
#define FREEBUTTON_SLOTRESET_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string_view>

#if defined(ESP_PLATFORM)
#include <esp_log.h>
#endif

#include "../ui/PendingRender.h"
#include "../utils/TextSlot.h"

namespace freebutton {

// LVGL lock held (see above). After it: hasText() is false, read() fails,
// apply() renders nothing until the next write.
template <size_t Bytes>
void resetSlot(TextSlot<Bytes>& slot) {
    const detail::TextSlotWriterLock lock;
    slot.~TextSlot<Bytes>();
    new (&slot) TextSlot<Bytes>();
}

// Script engine restart, with 'wrote' the widget's bits taken by
// scriptWrote.exchange(0): resets the slot if a script wrote it last (bit).
// LVGL lock held. Returns true if it did: the widget then has to draw its
// configured text again.
template <size_t Bytes>
bool resetIfScriptWrote(TextSlot<Bytes>& slot, const uint8_t wrote, const uint8_t bit) {
    if ((wrote & bit) == 0) {
        return false;
    }
    resetSlot(slot);
    return true;
}

// The widget a slot belongs to, for the log line of a cut text.
struct SlotOwner {
    const char* kind;  // "Button", "DisplayItem": log tag and prefix
    unsigned long id;
};

// Store text for the LVGL task. Any task; no heap, no LVGL lock.
template <size_t Bytes>
void storeText(TextSlot<Bytes>& slot, const std::string_view text, const SlotOwner owner, const char* field) {
    if (slot.write(text).firstTruncation) {
#if defined(ESP_PLATFORM)
        ESP_LOGW(owner.kind, "%s %lu: %s of %u bytes cut to %u (logged once per slot)",
                 owner.kind, owner.id, field, static_cast<unsigned>(text.size()),
                 static_cast<unsigned>(TextSlot<Bytes>::kCapacity));
#else
        (void)owner;
        (void)field;
#endif
    }
    ui::requestPendingRender();
}

// A message on the widget's own config topic (MQTT task): store it and note
// that the text is no longer a script's, so it stays when the script engine
// restarts.
template <size_t Bytes>
void storeFromConfigTopic(TextSlot<Bytes>& slot, const std::string_view text, const SlotOwner owner,
                          const char* field, std::atomic<uint8_t>& scriptWrote, const uint8_t bit) {
    storeText(slot, text, owner, field);
    scriptWrote.fetch_and(static_cast<uint8_t>(~bit), std::memory_order_acq_rel);
}

// A script setter: store the text and note that a script wrote it last.
template <size_t Bytes>
void storeFromScript(TextSlot<Bytes>& slot, const std::string_view text, const SlotOwner owner,
                     const char* field, std::atomic<uint8_t>& scriptWrote, const uint8_t bit) {
    storeText(slot, text, owner, field);
    scriptWrote.fetch_or(bit, std::memory_order_acq_rel);
}

} // namespace freebutton

#endif // FREEBUTTON_SLOTRESET_H
