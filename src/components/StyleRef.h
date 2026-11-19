//
// The "style" field of a button, a display item or a list (config.json):
// a string of up to four style names separated by spaces ("big alarm"),
// resolved at config load to a style list (ui/ui_style.h: one ref byte per
// name, in a word). The styles are loaded before any item
// (ConfigManager::loadStyles). An item keeps the refs, never a name and
// never a copy of a style.
//

#ifndef FREEBUTTON_STYLEREF_H
#define FREEBUTTON_STYLEREF_H

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "ArduinoJson.h"
#include <esp_log.h>

extern "C" {
#include "ui/ui_style.h"
}

namespace freebutton::styleref {

// The list for a config value. A name that cannot be used is logged and
// left out, the others stay: a reserved name ("button", "displayitem",
// "list": an item has its kind's style anyway), a name that is no style
// name, a style "styles" does not define, a fifth name. A name given twice
// counts once, at its last place. 0 when absent, "" or not a string.
inline style_list_t parse(const ArduinoJson::JsonVariantConst v, const char* tag) {
    if (v.isNull()) {
        return 0;
    }
    if (!v.is<const char*>()) {
        ESP_LOGW(tag, "\"style\" is not a string of style names, ignored");
        return 0;
    }
    const char* cursor = v.as<const char*>();
    const char* name = nullptr;
    size_t len = 0;
    style_list_t list = 0;
    while (style_logic_list_next(&cursor, &name, &len)) {
        const int shown = static_cast<int>(len > 32 ? 32 : len);
        if (style_logic_kind(name, len) >= 0) {
            ESP_LOGW(tag, "\"style\": \"%.*s\" is the style of a kind and cannot be named by an item, left out",
                     shown, name);
            continue;
        }
        if (!style_logic_name_valid(name, len)) {
            ESP_LOGW(tag, "\"style\": \"%.*s\" is not a style name, left out", shown, name);
            continue;
        }
        const uint8_t ref = ui_style_find(name, len);
        if (ref == 0) {
            ESP_LOGW(tag, "\"style\": unknown style \"%.*s\", left out", shown, name);
            continue;
        }
        if (!style_logic_list_add(&list, ref)) {
            ESP_LOGW(tag, "\"style\": more than %d styles, \"%.*s\" left out", STYLE_ITEM_MAX, shown, name);
        }
    }
    return list;
}

// A number as style_logic_set_number takes it for a property of `scale`
// (style_logic_prop_scale): 1 = an integer, as it is; 100 = a number of at
// most two decimals, times 100. false: not such a number.
// The value may have the precision of a float only: ArduinoJson keeps a
// short decimal of config.json as a float, so 1.2 arrives here as
// 1.2000000476837158 (x 100: off by 5e-6). A third decimal is off by 0.1.
inline bool scaled(const double value, const int scale, int32_t& out) {
    const double x = value * scale;
    if (!(x > -1000000.0 && x < 1000000.0)) {
        return false;  // also NaN
    }
    const long rounded = std::lround(x);
    if (std::fabs(x - static_cast<double>(rounded)) > 1e-4) {
        return false;  // a fraction, or more than two decimals
    }
    out = static_cast<int32_t>(rounded);
    return true;
}

// An item keeps its configured list as four bytes (written at config load
// only) next to the current one, an atomic word.
inline void store(uint8_t refs[STYLE_ITEM_MAX], const style_list_t list) {
    for (size_t i = 0; i < STYLE_ITEM_MAX; i++) {
        refs[i] = style_logic_list_ref(list, i);
    }
}

inline style_list_t load(const uint8_t refs[STYLE_ITEM_MAX]) {
    style_list_t list = 0;
    for (size_t i = 0; i < STYLE_ITEM_MAX; i++) {
        list |= static_cast<style_list_t>(refs[i]) << (8 * i);
    }
    return list;
}

// A ref a script side passes as an int: 0 for anything that is no ref.
inline uint8_t toRef(const int ref) {
    return ref >= 1 && ref <= STYLE_NAMED_MAX ? static_cast<uint8_t>(ref) : 0;
}

// addStyle / removeStyle of an item (style_logic_list_add / _remove) on its
// current list. Script code runs on the loop task and on the button task,
// so the word is exchanged only when no other task changed it in between;
// otherwise the rule is applied again to the newer word.
//  add:    false, and nothing changed, when it would be a fifth style.
//  *changed: the list is another one now (the caller then has it drawn).
inline bool add(std::atomic<style_list_t>& current, const uint8_t ref, bool* changed) {
    style_list_t now = current.load(std::memory_order_acquire);
    style_list_t next;
    do {
        next = now;
        if (!style_logic_list_add(&next, ref)) {
            *changed = false;
            return false;
        }
    } while (next != now && !current.compare_exchange_weak(now, next, std::memory_order_acq_rel));
    *changed = next != now;
    return true;
}

inline void remove(std::atomic<style_list_t>& current, const uint8_t ref, bool* changed) {
    style_list_t now = current.load(std::memory_order_acquire);
    style_list_t next;
    do {
        next = now;
        style_logic_list_remove(&next, ref);
    } while (next != now && !current.compare_exchange_weak(now, next, std::memory_order_acq_rel));
    *changed = next != now;
}

// Writes "style": the names joined by one space, for the config writer;
// nothing for an empty list.
template <class ScopedObject>
void write(ScopedObject& obj, const uint8_t refs[STYLE_ITEM_MAX]) {
    char text[UI_STYLE_LIST_TEXT_MAX];
    const size_t len = ui_style_list_text(load(refs), text, sizeof(text));
    if (len != 0) {
        obj.Add("style", std::string_view(text, len));
    }
}

} // namespace freebutton::styleref

#endif // FREEBUTTON_STYLEREF_H
