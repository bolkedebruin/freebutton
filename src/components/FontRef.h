//
// Config font references (config.json "font", "labelfont", "topfont"): a
// font "name" or "id" of the "style.fonts" table (or a system font name:
// montserrat, montserrat_semibold), resolved to a face index at config
// load. The registry is configured before any item (ConfigManager), and
// lookups only read the face table (fonts.h: any task). These item fields
// are the local layer over the element styles (docs/STYLE.md).
//
// Item / JS "fontsize": 1..5 = the legacy steps, FONT_PX_MIN..FONT_PX_MAX =
// px, 0 = the element style's (style_logic_item_px()).
//

#ifndef FREEBUTTON_FONTREF_H
#define FREEBUTTON_FONTREF_H

#include <cstdint>
#include <cstring>

#include "ArduinoJson.h"
#include <esp_log.h>

extern "C" {
#include "ui/fonts/fonts.h"
}

namespace freebutton::fontref {

// Face for a config value: a string name or an integer id. FONT_FACE_NONE
// when absent (null), "" or unknown (unknown is logged: the item keeps the
// style's font).
inline int parse(const ArduinoJson::JsonVariantConst v, const char* tag, const char* field) {
    if (v.isNull()) {
        return FONT_FACE_NONE;
    }
    int face = FONT_FACE_NONE;
    if (v.is<const char*>()) {
        const char* name = v.as<const char*>();
        if (name == nullptr || name[0] == '\0') {
            return FONT_FACE_NONE;
        }
        face = fonts_find(name, std::strlen(name));
        if (face == FONT_FACE_NONE) {
            ESP_LOGW(tag, "\"%s\": unknown font \"%.32s\", the style's font is used", field, name);
        }
    } else if (v.is<int32_t>()) {
        const int32_t id = v.as<int32_t>();
        face = fonts_find_id(id);
        if (face == FONT_FACE_NONE) {
            ESP_LOGW(tag, "\"%s\": unknown font id %ld, the style's font is used", field, static_cast<long>(id));
        }
    } else {
        ESP_LOGW(tag, "\"%s\" is neither a font name nor an id, the style's font is used", field);
    }
    return face;
}

// A fontsize as JS / config give it: 1..FONT_PX_MAX.
inline bool validSize(const int size) {
    return size >= 1 && size <= FONT_PX_MAX;
}

// Face + fontsize in one atomic word (runtime font set by scripts).
inline uint32_t pack(const int face, const int size) {
    return (static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(face))) << 16)
           | static_cast<uint16_t>(size);
}
inline int face(const uint32_t packed) { return static_cast<int16_t>(packed >> 16); }
inline int size(const uint32_t packed) { return static_cast<int>(packed & 0xFFFF); }

} // namespace freebutton::fontref

#endif // FREEBUTTON_FONTREF_H
