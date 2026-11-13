//
// IconLogic - pure rules for icons and images in config.json (no LVGL, no
// heap), shared by Button, DisplayItem and ImageItem; unit tested natively
// (test/test_icon_logic).
//
// The device never sees an SVG: the web IDE rasterizes every icon a config
// uses at every size it asks for and uploads "ico-<name>-<w>x<h>.bin" to the
// media store's images bucket (ui/MediaImage.h, docs/ICONS.md).
//
// Config:
//   button       "icon": "<name>", "iconsize": 48 (square)
//   display item "icon": "<name>", "iconsize": 32, "iconcolor": "#rrggbb"
//   "images"[]   {"name","page","x","y","width","height",
//                 "icon": "<name>" | "src": "<key>.bin", "color": "#rrggbb"}
// A colour only tints A8 rasters (single-colour artwork).
//

#ifndef FREEBUTTON_ICONLOGIC_H
#define FREEBUTTON_ICONLOGIC_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "ListLogic.h"

namespace freebutton::icon {

// Longest icon name ([A-Za-z0-9_-]); the same as ui::kIconNameMax.
inline constexpr size_t kNameMax = 31;
// Longest images-bucket key an image item may show ("src").
inline constexpr size_t kKeyMax = 96;

inline constexpr int kButtonDefaultSize = 48;
inline constexpr int kDisplayDefaultSize = 32;
// Rasters are drawn 1:1 (no scaling): nothing larger than the main display.
inline constexpr int kMaxSize = 320;

// No colour configured: the item's own default applies.
inline constexpr uint32_t kNoColor = 0xFFFFFFFFu;

// [A-Za-z0-9_-]{1,31}.
constexpr bool validName(const std::string_view name) {
    if (name.empty() || name.size() > kNameMax) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

// A configured size; fallback when absent (<= 0) and clamped to kMaxSize.
constexpr int clampSize(const int size, const int fallback) {
    if (size <= 0) {
        return fallback;
    }
    return size > kMaxSize ? kMaxSize : size;
}

// "#rrggbb" / "rrggbb" to 0xRRGGBB, kNoColor when absent or malformed.
inline uint32_t parseColor(const char* hex) {
    if (hex == nullptr || *hex == '\0') {
        return kNoColor;
    }
    return static_cast<uint32_t>(list::parseColor(hex, kNoColor));
}

// What an image item shows, as one string so that a writer changes the
// kind and the value together (one TextSlot write):
//   ""            nothing
//   "i<name>"     the icon raster for the item's width x height
//   "s<key>"      that key of the images bucket
inline constexpr char kWantIcon = 'i';
inline constexpr char kWantSrc = 's';
// Longest encoded value: the tag and a key.
inline constexpr size_t kWantMax = 1 + kKeyMax;

// Encodes into out (cap >= kWantMax + 1). Empty value: "" (shows nothing).
// False (out untouched) when the value is too long, or not a valid icon name.
inline bool encodeWant(char* out, const size_t cap, const char tag, const std::string_view value) {
    if (value.empty()) {
        if (cap == 0) {
            return false;
        }
        out[0] = '\0';
        return true;
    }
    const size_t max = tag == kWantIcon ? kNameMax : kKeyMax;
    if (value.size() > max || value.size() + 2 > cap) {
        return false;
    }
    if (tag == kWantIcon && !validName(value)) {
        return false;
    }
    if (tag == kWantSrc && value.find('\0') != std::string_view::npos) {
        return false;
    }
    out[0] = tag;
    std::memcpy(out + 1, value.data(), value.size());
    out[value.size() + 1] = '\0';
    return true;
}

} // namespace freebutton::icon

#endif // FREEBUTTON_ICONLOGIC_H
