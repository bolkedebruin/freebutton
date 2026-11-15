//
// ImageItem - see ImageItem.h.
//

#include "ImageItem.h"

#include "drivers/LvglDisplay.h"
#include "ui/LvglRoom.h"
#include "ui/MainPage.h"
#include "ui/MediaImage.h"

#include <esp_log.h>

#include <cstdlib>
#include <cstring>

extern "C" {
#include "ui/ui.h"
}

namespace freebutton {

namespace {

// LVGL pool one image object takes (lv_image_t, two local style
// properties), with margin.
constexpr size_t kRoomForImage = 512;
constexpr uint32_t kDefaultTint = 0xFFFFFF;

int clampCoord(const int v) {
    return v < -320 ? -320 : v > 640 ? 640 : v;
}

} // namespace

ImageItem::~ImageItem() {
    // Deletes LVGL objects: takes the (non-recursive) LVGL lock, so the
    // caller must not hold it (ConfigManager::clear()).
    auto& display = FreeButton::getLvglDisplay();
    display.lock();
    if (linked_) {
        unlinkPending();
        linked_ = false;
    }
    deleteImage();
    display.unlock();
    std::free(configWant_);
}

std::unique_ptr<ImageItem> ImageItem::fromJson(const ArduinoJson::JsonObject& config) {
    auto item = std::make_unique<ImageItem>();
    item->configure(config);
    return item;
}

void ImageItem::configure(const ArduinoJson::JsonObject& data) {
    x_ = static_cast<int16_t>(clampCoord(data["x"] | 0));
    y_ = static_cast<int16_t>(clampCoord(data["y"] | 0));
    width_ = static_cast<uint16_t>(icon::clampSize(data["width"] | 0, 0));
    height_ = static_cast<uint16_t>(icon::clampSize(data["height"] | 0, 0));
    page_ = data["page"] | page_;
    color_ = icon::parseColor(data["color"] | "");

    const char* iconName = data["icon"] | "";
    const char* src = data["src"] | "";
    if (iconName[0] != '\0') {
        if (width_ == 0 || height_ == 0) {
            ESP_LOGW(TAG, "Image with icon \"%s\" has no width / height: nothing to show", iconName);
        } else if (!setIcon(iconName)) {
            ESP_LOGW(TAG, "Image: icon \"%s\" is not a valid icon name, ignored", iconName);
        }
    } else if (src[0] != '\0' && !setSrc(src)) {
        ESP_LOGW(TAG, "Image: src key longer than %u bytes, ignored", static_cast<unsigned>(icon::kKeyMax));
    }
    configured_ = true;
}

void ImageItem::attach() {
    auto& display = FreeButton::getLvglDisplay();
    display.lock();
    if (!linked_) {
        linkPending();
        linked_ = true;
    }
    display.unlock();
    ui::requestPendingRender();  // shows the image if its page is shown
}

bool ImageItem::store(const char tag, const std::string_view value) {
    char want[icon::kWantMax + 1];
    if (!icon::encodeWant(want, sizeof(want), tag, value)) {
        return false;
    }
    if (configured_ && !scriptSet_) {
        keepConfigWant();
    }
    want_.write(want);
    ui::requestPendingRender();
    return true;
}

// Script code, before its first write: want_ still holds the config's.
void ImageItem::keepConfigWant() {
    scriptSet_ = true;
    char want[TextSlot<kWantSlotBytes>::kBufferSize];
    if (!want_.read(want) || want[0] == '\0') {
        return;  // the config shows nothing
    }
    const size_t n = std::strlen(want) + 1;
    configWant_ = static_cast<char*>(std::malloc(n));
    if (configWant_ == nullptr) {
        ESP_LOGE(TAG, "Image: no memory to keep its config image; a script restart will show nothing");
        return;
    }
    std::memcpy(configWant_, want, n);
}

void ImageItem::restoreConfigState() {
    if (!scriptSet_) {
        return;
    }
    want_.write(configWant_ != nullptr ? configWant_ : "");
    std::free(configWant_);
    configWant_ = nullptr;
    scriptSet_ = false;
    ui::requestPendingRender();
}

bool ImageItem::setIcon(const std::string_view name) {
    return store(icon::kWantIcon, name);
}

bool ImageItem::setSrc(const std::string_view key) {
    return store(icon::kWantSrc, key);
}

// LVGL task, LVGL lock held. Called on every render request of any widget:
// cheap when nothing changed (a page check, a generation load, a seq load).
bool ImageItem::applyPending() {
    if (!ui::visibleOnPage(page_, ui::shownPage())) {
        deleteImage();
        return false;
    }
    if (image_ == nullptr) {
        createImage();
        if (image_ == nullptr) {
            return false;  // no LVGL memory now; the next render request retries
        }
        want_.invalidate();
    }
    if (raster_.stale()) {
        want_.invalidate();  // the images bucket changed: resolve again
    }
    return want_.apply([this](const char* want) { show(want); }) == TextSlotApply::Busy;
}

// LVGL lock held, image exists.
void ImageItem::show(const char* want) {
    char key[icon::kKeyMax + 1];
    const char* k = nullptr;
    if (want[0] == icon::kWantIcon) {
        if (ui::iconRasterKey(key, sizeof(key), want + 1, width_, height_)) {
            k = key;
        }
    } else if (want[0] == icon::kWantSrc) {
        k = want + 1;
    }
    const lv_color_t tint = lv_color_hex(color_ != icon::kNoColor ? color_ : kDefaultTint);
    raster_.resolve(k, [this, tint](const lv_image_dsc_t* dsc) {
        ui_image_set_raster(image_, dsc, tint);
        lv_obj_set_hidden(image_, dsc == nullptr);
        return true;
    });
}

// LVGL lock held.
void ImageItem::createImage() {
    lv_obj_t* parent = FreeButton::getLvglDisplay().getMainScreen();
    if (parent == nullptr) {
        return;
    }
    if (!ui::lvglHasRoom(kRoomForImage)) {
        if (!buildFailed_) {
            ESP_LOGE(TAG, "Image: no LVGL memory to show it");
            buildFailed_ = true;
        }
        return;  // the next render request retries
    }
    buildFailed_ = false;
    lv_obj_t* img = lv_image_create(parent);
    if (img == nullptr) {
        ESP_LOGE(TAG, "Image: no LVGL memory for the image");
        return;
    }
    lv_obj_set_clickable(img, false);
    lv_obj_set_pos(img, x_, y_);
    lv_obj_set_hidden(img, true);  // until a raster is resolved
    image_ = img;
}

// LVGL lock held.
void ImageItem::deleteImage() {
    if (image_ != nullptr) {
        lv_obj_delete(image_);
        image_ = nullptr;
        raster_.release();  // after the object that showed it is gone
    }
}

} // namespace freebutton
