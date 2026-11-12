// Raster images from the media store's `images` bucket (MediaImage.h).
//
// A fixed table of slots, one per distinct mapped raster version: no heap.
// Slots are shared by media store handle, not by key: media_open() returns
// the same handle for a key whose current version is already open, and a
// new one after the key was replaced. So an item that re-acquires after a
// generation change gets the new raster, while items still showing the old
// one keep their (still mapped) old slot until they release it.
//
// Threading: mediaImageAcquire / mediaImageRelease run on the LVGL task only
// (the table is not locked); mediaImageGeneration / mediaImageNotifyChanged
// from any task.
#include "MediaImage.h"

#include <atomic>
#include <cstring>

#include <esp_log.h>
#include <src/misc/cache/instance/lv_image_cache.h>  // lv_image_cache_drop (not in lvgl.h)

#include "../media/media_store.h"
#include "PendingRender.h"
#include "media_image_check.h"

static_assert(MEDIA_IMAGE_MAGIC == LV_IMAGE_HEADER_MAGIC, "LVGL image magic");
static_assert(MEDIA_IMAGE_CF_A8 == LV_COLOR_FORMAT_A8, "LVGL colour format A8");
static_assert(MEDIA_IMAGE_CF_RGB565 == LV_COLOR_FORMAT_RGB565, "LVGL colour format RGB565");
static_assert(MEDIA_IMAGE_CF_RGB565A8 == LV_COLOR_FORMAT_RGB565A8, "LVGL colour format RGB565A8");
static_assert(sizeof(lv_image_header_t) == MEDIA_IMAGE_HEADER_SIZE, "lv_image_header_t is the 12-byte file header");
static_assert(freebutton::ui::kIconNameMax == MEDIA_IMAGE_NAME_MAX, "icon name limit");

namespace freebutton::ui {
namespace {

const char* TAG = "media_image";

// Distinct rasters mapped at once (every item showing the same version
// shares one). The media store's open table (MEDIA_MAX_OPEN) is shared with
// the fonts, so a raster can also fail with MEDIA_ERR_BUSY before this fills.
constexpr int kSlots = 24;

struct Slot {
    lv_image_dsc_t dsc;  // first: the pointer handed out
    uint32_t handle;     // media_file_t.handle, 0 = free
    uint16_t refs;
};

Slot slots[kSlots];

std::atomic<uint32_t> generation{1};

// "Warned once per key" without heap: hashes of the keys warned about since
// the last change to the bucket (a change may fix or break a key again).
constexpr int kWarned = 8;
uint32_t warned[kWarned];
int warnedNext = 0;
uint32_t warnedGen = 0;

uint32_t keyHash(const char* key) {
    uint32_t h = 2166136261u;  // FNV-1a
    for (; *key; ++key) h = (h ^ static_cast<uint8_t>(*key)) * 16777619u;
    return h | 1u;  // never 0 (a free entry)
}

// True the first time per key (and generation): log at warning level.
bool firstWarning(const char* key) {
    const uint32_t gen = generation.load(std::memory_order_relaxed);
    if (gen != warnedGen) {
        memset(warned, 0, sizeof warned);
        warnedNext = 0;
        warnedGen = gen;
    }
    const uint32_t h = keyHash(key);
    for (uint32_t w : warned) {
        if (w == h) return false;
    }
    warned[warnedNext] = h;
    warnedNext = (warnedNext + 1) % kWarned;
    return true;
}

#define MI_WARN(key, fmt, ...)                                              \
    do {                                                                    \
        if (firstWarning(key)) ESP_LOGW(TAG, fmt, ##__VA_ARGS__);           \
        else ESP_LOGD(TAG, fmt, ##__VA_ARGS__);                             \
    } while (0)

const char* mediaErrStr(media_err_t e) {
    switch (e) {
        case MEDIA_ERR_UNAVAILABLE: return "no media store";
        case MEDIA_ERR_NOT_FOUND: return "not found";
        case MEDIA_ERR_INVALID: return "invalid key";
        case MEDIA_ERR_BUSY: return "too many open media files";
        case MEDIA_ERR_IO: return "flash error";
        default: return "error";
    }
}

}  // namespace

bool iconRasterKey(char* out, size_t cap, const char* name, uint16_t w, uint16_t h) {
    return media_image_icon_key(out, cap, name, w, h);
}

const lv_image_dsc_t* mediaImageAcquire(const char* key) {
    if (!key || !*key) return nullptr;
    media_file_t f;
    const media_err_t e = media_open(MEDIA_BUCKET_IMAGES, key, &f);
    if (e != MEDIA_OK) {
        MI_WARN(key, "images/%s: %s", key, mediaErrStr(e));
        return nullptr;
    }
    int freeSlot = -1;
    for (int i = 0; i < kSlots; ++i) {
        if (slots[i].handle == f.handle && slots[i].refs < UINT16_MAX) {
            ++slots[i].refs;
            media_close(&f);  // the slot keeps its own reference
            return &slots[i].dsc;
        }
        if (freeSlot < 0 && slots[i].handle == 0) freeSlot = i;
    }
    media_image_info_t info;
    const media_image_err_t ce = media_image_check(f.data, f.size, &info);
    if (ce != MEDIA_IMAGE_OK) {
        MI_WARN(key, "images/%s: not an image raster (%s)", key, media_image_err_str(ce));
        media_close(&f);
        return nullptr;
    }
    if (freeSlot < 0) {
        MI_WARN(key, "images/%s: more than %d rasters in use", key, kSlots);
        media_close(&f);
        return nullptr;
    }
    Slot& s = slots[freeSlot];
    memset(&s.dsc, 0, sizeof s.dsc);
    s.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s.dsc.header.cf = info.cf;
    s.dsc.header.flags = 0;
    s.dsc.header.w = info.w;
    s.dsc.header.h = info.h;
    s.dsc.header.stride = info.stride;
    s.dsc.data_size = info.data_size;
    s.dsc.data = f.data + info.data_offset;
    s.handle = f.handle;
    s.refs = 1;
    return &s.dsc;
}

void mediaImageRelease(const lv_image_dsc_t* dsc) {
    if (!dsc) return;
    for (Slot& s : slots) {
        if (&s.dsc != dsc) continue;
        if (s.refs == 0) return;  // double release
        if (--s.refs) return;
        // A later acquire may reuse this descriptor address for another
        // raster: drop anything LVGL cached for it (no-op with the image
        // caches off, LV_CACHE_DEF_SIZE 0).
        lv_image_cache_drop(&s.dsc);
        media_file_t f{};
        f.data = static_cast<const uint8_t*>(s.dsc.data) - MEDIA_IMAGE_HEADER_SIZE;
        f.size = s.dsc.data_size + MEDIA_IMAGE_HEADER_SIZE;
        f.handle = s.handle;
        media_close(&f);
        memset(&s, 0, sizeof s);
        return;
    }
}

uint32_t mediaImageGeneration() {
    return generation.load(std::memory_order_acquire);
}

void mediaImageNotifyChanged() {
    generation.fetch_add(1, std::memory_order_acq_rel);
    requestPendingRender();
}

}  // namespace freebutton::ui
