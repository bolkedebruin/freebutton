//
// Config named-style references (config.json "style" / "topstyle" on keys,
// "style" / "labelstyle" on display items, "style" / "focusstyle" on
// lists): the name of a "style.styles" entry, resolved at config load to a
// ref (index + 1, one byte, 0 = none; ui/ui_style.h). The table is loaded
// before any item (ConfigManager::loadStyle). An item keeps the ref, never
// the name and never a copy of the style.
//

#ifndef FREEBUTTON_STYLEREF_H
#define FREEBUTTON_STYLEREF_H

#include <cstdint>
#include <cstring>
#include <string_view>

#include "ArduinoJson.h"
#include <esp_log.h>

extern "C" {
#include "ui/ui_style.h"
}

namespace freebutton::styleref {

// Ref for a config value. 0 when absent (null), "" or not usable, which is
// logged (the item then looks as without the field): not a string, no such
// style, or a style that sets bg_* on a part that has no background (a key
// or a display item; `el` is the part's element).
inline uint8_t parse(const ArduinoJson::JsonVariantConst v, const char* tag, const char* field,
                     const style_element_t el) {
    if (v.isNull()) {
        return 0;
    }
    if (!v.is<const char*>()) {
        ESP_LOGW(tag, "\"%s\" is not a style name, ignored", field);
        return 0;
    }
    const char* name = v.as<const char*>();
    if (name == nullptr || name[0] == '\0') {
        return 0;
    }
    const uint8_t ref = ui_style_named_find(name, std::strlen(name));
    const style_props_t* props = ui_style_named_props(ref);
    if (props == nullptr) {
        ESP_LOGW(tag, "\"%s\": unknown style \"%.32s\", ignored", field, name);
        return 0;
    }
    if (!style_logic_named_fits(props, el)) {
        ESP_LOGW(tag, "\"%s\": style \"%.32s\" sets a background, which only a list has; ignored", field, name);
        return 0;
    }
    return ref;
}

// Writes `key`: the style's name, for the config writer; nothing for ref 0.
template <class ScopedObject>
void write(ScopedObject& obj, const char* key, const uint8_t ref) {
    size_t len = 0;
    if (const char* name = ui_style_named_name(ref, &len)) {
        obj.Add(key, std::string_view(name, len));
    }
}

} // namespace freebutton::styleref

#endif // FREEBUTTON_STYLEREF_H
