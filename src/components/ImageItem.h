//
// ImageItem - a raster image on the main display (config.json "images"):
// an icon from the config's "icons" (the raster the web IDE made at the
// item's width x height) or any key of the media store's images bucket.
//
//   {"name": "weather", "page": 1, "x": 10, "y": 20, "width": 48,
//    "height": 48, "icon": "rain", "color": "#ffd700"}
//   {"name": "logo", "x": 0, "y": 0, "width": 320, "height": 60,
//    "src": "logo.bin"}
//
// x / y are pixels of the 320x240 main display (top-left of the raster);
// width / height are pixels too: they select the icon raster
// ("ico-<icon>-<w>x<h>.bin"). The raster is drawn at its own size, never
// scaled. "color" tints A8 (single-colour) rasters; default white.
//
// Scripts drive it by name: fb.main.image.setIcon("weather", "sun"),
// fb.main.image.setSrc("logo", "logo2.bin").
//
// Memory: the pixels stay in flash (ui/MediaImage.h). The lv_image exists
// only while the item's page is shown, like display items and lists.
//
// Threads: scripts (JS task) store what to show in a TextSlot; the LVGL task
// shows it in applyPending() (PendingRender) and re-resolves after a change
// to the images bucket (see IconRef.h).
//

#ifndef FREEBUTTON_IMAGEITEM_H
#define FREEBUTTON_IMAGEITEM_H

#include "ui/MainPage.h"
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>

#include "ArduinoJson.h"
#include "IconLogic.h"
#include "IconRef.h"
#include "json/JsonWriter.h"
#include "ui/PendingRender.h"
#include "utils/TextSlot.h"

#include <lvgl.h>

namespace freebutton {

class ImageItem final : public ui::PendingRender {
public:
    ImageItem() = default;
    ~ImageItem();

    static std::unique_ptr<ImageItem> fromJson(const ArduinoJson::JsonObject& config);

    // Links the item for rendering (LVGL lock taken here). Call once, after
    // configuration; the image appears when the page is shown.
    void attach();

    // --- any task (scripts) ----------------------------------------------------
    // "" shows nothing. false: not a valid icon name / key too long.
    bool setIcon(std::string_view name);
    bool setSrc(std::string_view key);

    int getPage() const { return page_; }

    template<typename OutputStream>
    void toJson(json::JsonWriter<OutputStream>::ScopedObject& obj) const;

    // PendingRender (LVGL task, LVGL lock held).
    bool applyPending() override;
    bool shownNow() const override { return ui::visibleOnPage(page_, ui::shownPage()); }

private:
    static constexpr auto TAG = "ImageItem";
    // A tag and a key (icon::kWantMax) and the terminator.
    static constexpr size_t kWantSlotBytes = 104;
    static_assert(TextSlot<kWantSlotBytes>::kCapacity >= icon::kWantMax);

    void configure(const ArduinoJson::JsonObject& data);
    bool store(char tag, std::string_view value);
    void createImage();   // LVGL lock held
    void deleteImage();   // LVGL lock held
    void show(const char* want);  // LVGL lock held, image exists

    // Configuration (written before attach(), read-only afterwards).
    int16_t x_ = 0;
    int16_t y_ = 0;
    uint16_t width_ = 0;
    uint16_t height_ = 0;
    int page_ = 1;
    uint32_t color_ = icon::kNoColor;

    // What to show (icon::encodeWant): runtime state, seeded from config.
    TextSlot<kWantSlotBytes> want_;

    RasterRef raster_;            // LVGL lock held
    lv_obj_t* image_ = nullptr;   // LVGL lock held
    bool linked_ = false;         // LVGL lock held
    bool buildFailed_ = false;    // LVGL lock held; logged once
};

template<typename OutputStream>
void ImageItem::toJson(json::JsonWriter<OutputStream>::ScopedObject& obj) const {
    obj.Add("page", page_);
    obj.Add("x", static_cast<int>(x_));
    obj.Add("y", static_cast<int>(y_));
    obj.Add("width", static_cast<int>(width_));
    obj.Add("height", static_cast<int>(height_));
    char want[TextSlot<kWantSlotBytes>::kBufferSize];
    if (want_.read(want) && want[0] != '\0') {
        obj.Add(want[0] == icon::kWantIcon ? "icon" : "src", static_cast<const char*>(want + 1));
    }
    if (color_ != icon::kNoColor) {
        char hex[8];
        snprintf(hex, sizeof(hex), "#%06lx", static_cast<unsigned long>(color_));
        obj.Add("color", static_cast<const char*>(hex));
    }
}

} // namespace freebutton

#endif // FREEBUTTON_IMAGEITEM_H
