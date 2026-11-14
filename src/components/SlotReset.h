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

#ifndef FREEBUTTON_SLOTRESET_H
#define FREEBUTTON_SLOTRESET_H

#include <cstddef>
#include <new>

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

} // namespace freebutton

#endif // FREEBUTTON_SLOTRESET_H
