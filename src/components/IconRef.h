//
// IconRef - the raster an lv_image shows, and the icon state of a button or
// display item (config "icon", MQTT ICON / DISPLAY_ICON, JS setIcon).
//
// Rasters come from the media store's images bucket, mapped from flash
// (ui/MediaImage.h): acquiring one costs no heap for its pixels, and LVGL
// draws straight from the mapping. Every acquire is released exactly once,
// after the image object no longer shows it (or was deleted).
//
// Re-resolve: the S3 hooks call mediaImageNotifyChanged() on any change to
// the images bucket; it bumps mediaImageGeneration() and requests a pending
// render, so every PendingRender item's applyPending() runs. An item whose
// RasterRef was resolved at an older generation re-resolves (stale()), which
// picks up a raster that was added, replaced or deleted. Items without an
// icon do one atomic load and nothing else.
//
// Threads: RasterRef is LVGL-task only (LVGL lock held). IconState's name
// slot is written by any task (MQTT, JS) and applied by the LVGL task, like
// a label (utils/TextSlot.h).
//

#ifndef FREEBUTTON_ICONREF_H
#define FREEBUTTON_ICONREF_H

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string_view>

#include "IconLogic.h"
#include "ui/MediaImage.h"
#include "utils/TextSlot.h"

#include <lvgl.h>

namespace freebutton {

class RasterRef {
public:
    RasterRef() = default;
    RasterRef(const RasterRef&) = delete;
    RasterRef& operator=(const RasterRef&) = delete;

    // The images bucket changed since the last resolve().
    bool stale() const { return generation_ != ui::mediaImageGeneration(); }

    const lv_image_dsc_t* dsc() const { return dsc_; }

    // Acquires key (nullptr: nothing) and hands it to show(dsc), which puts it
    // on the image object (nullptr: shows nothing) and returns whether the
    // object now shows it. Then releases what was shown before.
    template <class Show>
    void resolve(const char* key, Show&& show) {
        generation_ = ui::mediaImageGeneration();  // before acquiring: a bump meanwhile re-resolves
        const lv_image_dsc_t* next = key != nullptr ? ui::mediaImageAcquire(key) : nullptr;
        const lv_image_dsc_t* prev = dsc_;
        if (!show(next) && next != nullptr) {
            ui::mediaImageRelease(next);
            next = nullptr;
        }
        dsc_ = next;
        if (prev != nullptr) {
            ui::mediaImageRelease(prev);
        }
    }

    // The image object was deleted, or set to show nothing.
    void release() {
        if (dsc_ != nullptr) {
            ui::mediaImageRelease(dsc_);
            dsc_ = nullptr;
        }
    }

private:
    const lv_image_dsc_t* dsc_ = nullptr;
    uint32_t generation_ = 0;
};

// The icon of a button or display item. Allocated only for items that have
// (or are given) an icon: an item without one costs a pointer.
struct IconState {
    // 34 text bytes; an icon name is at most 31.
    static constexpr size_t kSlotBytes = 40;
    static_assert(TextSlot<kSlotBytes>::kCapacity >= icon::kNameMax);

    TextSlot<kSlotBytes> name;  // "" = no icon
    RasterRef raster;           // LVGL task
    uint16_t size;              // square, px
    uint32_t color;             // A8 tint, icon::kNoColor = the item's text colour
    bool roomWarned = false;    // LVGL task: no LVGL memory for the image, logged once
    // Length of the config "icon" name kept behind this struct (0: the config
    // has no icon). Written once by IconHolder::ensure(), before the state is
    // published.
    uint8_t configNameLen = 0;

    IconState(const int sizePx, const uint32_t tint) : size(static_cast<uint16_t>(sizePx)), color(tint) {}

    // The config "icon" name ("" without one). The name slot is run-time
    // state (MQTT, scripts write over it), so the configured name is kept
    // apart: in the same allocation, right behind the struct, only for items
    // whose config has an icon (its length + 1 bytes). Read when the script
    // engine restarts.
    const char* configName() const {
        return configNameLen != 0 ? reinterpret_cast<const char*>(this + 1) : "";
    }

    // LVGL task, LVGL lock held. Renders the newest name (or the same one
    // again after a change to the images bucket) through
    // show(const lv_image_dsc_t*) -> bool (see RasterRef::resolve). Returns
    // true when a writer kept the slot busy (retry next tick).
    template <class Show>
    bool apply(Show&& show) {
        if (raster.stale()) {
            name.invalidate();
        }
        return name.apply([this, &show](const char* text) {
                   char key[64];
                   const bool want = text[0] != '\0' && ui::iconRasterKey(key, sizeof(key), text, size, size);
                   raster.resolve(want ? key : nullptr, show);
               }) == TextSlotApply::Busy;
    }
};

// Owner of an item's IconState. The pointer is published once (config load,
// or the first JS setIcon) and stays until the item is destroyed, so readers
// on any task only need an acquire load.
class IconHolder {
public:
    IconHolder() = default;
    IconHolder(const IconHolder&) = delete;
    IconHolder& operator=(const IconHolder&) = delete;
    // The owner released the raster (LVGL lock) before; no writer remains.
    ~IconHolder() {
        if (IconState* s = state_.load(std::memory_order_acquire)) {
            s->~IconState();
            std::free(s);
        }
    }

    IconState* get() const { return state_.load(std::memory_order_acquire); }

    // Config load or script code. Creates the state on first use; nullptr when
    // the heap is out of memory (malloc, never a throwing / aborting new).
    // configName: the config "icon" name (a valid icon name, or empty), kept
    // for IconState::configName(); used only when the state is created here.
    IconState* ensure(const int sizePx, const uint32_t tint, const std::string_view configName = {}) {
        if (IconState* s = get()) {
            return s;
        }
        const size_t nameLen = configName.size() <= icon::kNameMax ? configName.size() : 0;
        void* mem = std::malloc(sizeof(IconState) + (nameLen != 0 ? nameLen + 1 : 0));
        if (mem == nullptr) {
            return nullptr;
        }
        auto* fresh = new (mem) IconState(sizePx, tint);
        if (nameLen != 0) {
            char* tail = reinterpret_cast<char*>(fresh + 1);
            std::memcpy(tail, configName.data(), nameLen);
            tail[nameLen] = '\0';
            fresh->configNameLen = static_cast<uint8_t>(nameLen);
        }
        IconState* expected = nullptr;
        if (!state_.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel)) {
            fresh->~IconState();
            std::free(mem);
            return expected;
        }
        return fresh;
    }

private:
    std::atomic<IconState*> state_{nullptr};
};

} // namespace freebutton

#endif // FREEBUTTON_ICONREF_H
