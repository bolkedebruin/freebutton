//
// ConfigManager - Efficient management of multiple buttons and display items
// Created by Bolke de Bruin on 19/11/2025.
//

#include "ConfigManager.h"
#include "ConfigLogic.h"
#include "json/FilteredReaderHandler.h"
#include <algorithm>
#include <esp_log.h>
#include "LittleFS.h"
#include "drivers/BusAwareFileSystem.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include "Clock.h"
#include "drivers/LvglDisplay.h"
#include "ui/ui_style.h"

extern "C" {
#include "ui/fonts/fonts.h"
}
#include "ui/MainPage.h"
#include "ui/PendingRender.h"
#include "drivers/ButtonLed.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace freebutton {

namespace {

// Created on first use (setup(), before any other task touches the list).
SemaphoreHandle_t displayItemsMutex() {
    static StaticSemaphore_t buffer;
    static SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutexStatic(&buffer);
    return handle;
}

// One entry of config.json "fonts" -> def. false (logged) when it cannot be
// one: not an object, no usable name or src, an id that is not an integer.
// The registry checks the rest (name rules, duplicates, chains).
bool parseFontDef(const JsonVariantConst entry, font_def_t& def) {
    static constexpr auto TAG = "ConfigManager";
    std::memset(&def, 0, sizeof(def));
    if (!entry.is<JsonObjectConst>()) {
        ESP_LOGW(TAG, "fonts: an entry is not an object, skipped");
        return false;
    }
    const char* name = entry["name"] | "";
    if (name[0] == '\0' || std::strlen(name) > FONT_NAME_MAX) {
        ESP_LOGW(TAG, "fonts: entry \"%.32s\" needs a name of 1..%d characters, skipped", name, FONT_NAME_MAX);
        return false;
    }
    std::strcpy(def.name, name);

    const auto id = entry["id"];
    if (!id.isNull()) {
        if (!id.is<int32_t>()) {
            ESP_LOGW(TAG, "fonts: \"%s\": the id is not an integer, skipped", name);
            return false;
        }
        def.id = id.as<int32_t>();
    }

    const char* src = entry["src"] | "";
    if (std::strlen(src) > FONT_SRC_MAX) {
        ESP_LOGW(TAG, "fonts: \"%s\": src longer than %d bytes, skipped", name, FONT_SRC_MAX);
        return false;
    }
    std::strcpy(def.src, src);

    const auto fallback = entry["fallback"];
    if (fallback.isNull()) {
        def.fallback_kind = FONT_FALLBACK_DEFAULT;
    } else if (fallback.is<const char*>()) {
        const char* f = fallback.as<const char*>();
        if (f == nullptr || f[0] == '\0') {
            def.fallback_kind = FONT_FALLBACK_NONE;
        } else {
            def.fallback_kind = FONT_FALLBACK_NAME;
            if (std::strlen(f) <= FONT_NAME_MAX) {
                std::strcpy(def.fallback_name, f);
            }  // else "": no such font, the registry cuts the chain (warned)
        }
    } else if (fallback.is<int32_t>()) {
        def.fallback_kind = FONT_FALLBACK_ID;
        def.fallback_id = fallback.as<int32_t>();
    } else {
        ESP_LOGW(TAG, "fonts: \"%s\": fallback is neither a name nor an id, ignored (no fallback)", name);
        def.fallback_kind = FONT_FALLBACK_DEFAULT;
    }

    const auto lineheight = entry["lineheight"];
    if (!lineheight.isNull()) {
        const float em = lineheight.as<float>();
        const long x100 = std::lround(em * 100.0f);
        if (!lineheight.is<float>() || x100 < 80 || x100 > 200) {
            ESP_LOGW(TAG, "fonts: \"%s\": lineheight must be 0.8..2.0 (em), ignored", name);
        } else {
            def.lineheight_x100 = static_cast<uint16_t>(x100);
        }
    }
    return true;
}

// style_logic font-family resolver: the registry (configured first).
int resolveStyleFont(const char* name, const size_t len, const bool is_id, const int32_t id, void*) {
    return is_id ? fonts_find_id(id) : fonts_find(name, len);
}

void warnStyleProp(const style_prop_status_t st, const char* style, const char* part, const char* prop) {
    static constexpr auto TAG = "ConfigManager";
    const char* dot = part != nullptr ? "." : "";
    const char* partName = part != nullptr ? part : "";
    switch (st) {
        case STYLE_PROP_OK:
            break;
        case STYLE_PROP_UNKNOWN:
            ESP_LOGW(TAG, "styles.%s%s%s: unknown property \"%.32s\", ignored", style, dot, partName, prop);
            break;
        case STYLE_PROP_UNKNOWN_FONT:
            ESP_LOGW(TAG, "styles.%s%s%s.%s: unknown font, ignored", style, dot, partName, prop);
            break;
        default:
            ESP_LOGW(TAG, "styles.%s%s%s.%s: bad value, ignored", style, dot, partName, prop);
            break;
    }
}

// One block ({"color": ..., "font-size": ...}) into p. `part`: the name of
// the sub-block, or nullptr for the style's main block, whose sub-block
// keys ("title", "unit", "selected", "icon") are not properties.
void parseStyleProps(const JsonObjectConst block, style_props_t& p, const char* style, const char* part) {
    for (const auto kv : block) {
        const char* prop = kv.key().c_str();
        if (part == nullptr && style_logic_part(prop) >= 0) {
            continue;
        }
        const auto v = kv.value();
        style_prop_status_t st = STYLE_PROP_BAD_VALUE;
        int32_t number = 0;
        if (v.is<const char*>()) {
            st = style_logic_set_string(&p, prop, v.as<const char*>(), resolveStyleFont, nullptr);
        } else if (style_logic_prop_scale(prop) == 0) {
            // not a number property: unknown, or it takes a string
            st = style_logic_set_number(&p, prop, 0, resolveStyleFont, nullptr);
        } else if ((v.is<double>() || v.is<int32_t>()) &&
                   styleref::scaled(v.as<double>(), style_logic_prop_scale(prop), number)) {
            st = style_logic_set_number(&p, prop, number, resolveStyleFont, nullptr);
        }
        warnStyleProp(st, style, part, prop);
    }
}

// An "icon" block: its colour into `icon`.
void parseStyleIcon(const JsonObjectConst block, uint32_t& icon, const char* style) {
    for (const auto kv : block) {
        const auto v = kv.value();
        warnStyleProp(v.is<const char*>() ? style_logic_icon_set_string(&icon, kv.key().c_str(), v.as<const char*>())
                                          : style_logic_icon_set_string(&icon, kv.key().c_str(), nullptr),
                      style, "icon", kv.key().c_str());
    }
}

const char* const kPartNames[STYLE_PART_COUNT + 1] = {nullptr, "title", "unit", "selected", "icon"};

// The sub-block `part` of a style, when it is an object with something in it.
JsonObjectConst styleSubBlock(const JsonObjectConst block, const int part) {
    const auto sub = block[kPartNames[part]];
    return sub.is<JsonObjectConst>() ? sub.as<JsonObjectConst>() : JsonObjectConst();
}

// Which blocks of properties a style has: main when it has a key that is
// not a sub-block, and each of "title" / "unit" / "selected" that is an
// object with at least one key. Used to size the table and to fill it, so
// that both agree.
size_t styleParts(const JsonObjectConst block, bool has[STYLE_PART_COUNT]) {
    size_t count = 0;
    has[STYLE_PART_MAIN] = false;
    for (const auto kv : block) {
        if (style_logic_part(kv.key().c_str()) < 0) {
            has[STYLE_PART_MAIN] = true;
            break;
        }
    }
    count += has[STYLE_PART_MAIN] ? 1 : 0;
    for (int part = STYLE_PART_TITLE; part < STYLE_PART_COUNT; part++) {
        has[part] = styleSubBlock(block, part).size() > 0;
        count += has[part] ? 1 : 0;
    }
    return count;
}

// A reserved style ("button", "displayitem", "list") into the sheet that is
// resolved into the kind's elements. A sub-block the kind does not have is
// logged and ignored.
void parseKindStyle(const JsonObjectConst block, const style_kind_t kind, const char* name, style_sheet_t& sheet) {
    static constexpr auto TAG = "ConfigManager";
    parseStyleProps(block, sheet.element[style_logic_element(kind, STYLE_PART_MAIN)], name, nullptr);
    for (int part = STYLE_PART_TITLE; part <= STYLE_PART_ICON; part++) {
        const auto sub = block[kPartNames[part]];
        if (sub.isNull()) {
            continue;
        }
        if (!sub.is<JsonObjectConst>()) {
            ESP_LOGW(TAG, "styles.%s.%s is not an object, ignored", name, kPartNames[part]);
            continue;
        }
        if (part == STYLE_PART_ICON && kind != STYLE_KIND_LIST) {
            parseStyleIcon(sub.as<JsonObjectConst>(), sheet.icon[kind], name);
            continue;
        }
        const int el = part == STYLE_PART_ICON ? -1 : style_logic_element(kind, static_cast<style_part_t>(part));
        if (el < 0) {
            ESP_LOGW(TAG, "styles.%s.%s: a %s has no such part, ignored", name, kPartNames[part], name);
            continue;
        }
        parseStyleProps(sub.as<JsonObjectConst>(), sheet.element[el], name, kPartNames[part]);
    }
}

// config.json "styles" (name -> style) minus the reserved names: calls
// fn(name, length, style) for each entry the table takes, in file order. An
// entry that is not taken (a bad name, not an object, more than
// STYLE_NAMED_MAX) is skipped, with a warning when `warn`. Walked twice by
// parseStyleTable(): to size the table, then to fill it.
template <typename Fn>
void forEachTableStyle(const JsonObjectConst styles, const bool warn, Fn&& fn) {
    static constexpr auto TAG = "ConfigManager";
    size_t count = 0;
    for (const auto kv : styles) {
        const char* name = kv.key().c_str();
        const size_t len = kv.key().size();
        if (style_logic_kind(name, len) >= 0) {
            continue;  // parseKindStyle
        }
        const style_named_status_t st = style_logic_named_check(name, len, count);
        if (st == STYLE_NAMED_BAD_NAME) {
            if (warn) {
                ESP_LOGW(TAG, "styles: \"%.32s\" is not a style name (letters, digits, _; not starting with "
                              "a digit; max %d), skipped", name, STYLE_NAME_MAX);
            }
            continue;
        }
        if (st == STYLE_NAMED_FULL) {
            if (warn) {
                ESP_LOGW(TAG, "styles: more than %d styles, \"%s\" and the rest are left out", STYLE_MAX, name);
            }
            break;
        }
        if (!kv.value().is<JsonObjectConst>()) {
            if (warn) {
                ESP_LOGW(TAG, "styles.%s is not an object, skipped", name);
            }
            continue;
        }
        fn(name, len, kv.value().as<JsonObjectConst>());
        count++;
    }
}

// config.json "styles" -> the reserved styles into `sheet`, the others into
// the style table (ui/ui_style.h); after the fonts (font-family names) and
// before the items (they refer to the styles by name). In two steps, so
// that the styles document is gone before anything is made in the LVGL pool
// (loadStyles): parseStyleTable() sizes the table, allocates it and parses
// the properties into it (plain data); applyStyleTable() then fills the
// lv_style_t of each part. Both take the LVGL lock. A config with only
// reserved styles allocates nothing; a previous table is freed either way.
// Returns the number of styles in the table.
size_t parseStyleTable(const JsonVariantConst styles, style_sheet_t& sheet) {
    static constexpr auto TAG = "ConfigManager";
    auto& display = FreeButton::getLvglDisplay();
    size_t count = 0;
    size_t parts = 0;
    size_t nameBytes = 0;
    const bool isObject = styles.is<JsonObjectConst>();
    if (!isObject && !styles.isNull()) {
        ESP_LOGW(TAG, "\"styles\" is not an object, ignored");
    }
    if (isObject) {
        for (const auto kv : styles.as<JsonObjectConst>()) {
            const int kind = style_logic_kind(kv.key().c_str(), kv.key().size());
            if (kind < 0) {
                continue;
            }
            if (!kv.value().is<JsonObjectConst>()) {
                ESP_LOGW(TAG, "styles.%s is not an object, ignored", kv.key().c_str());
                continue;
            }
            parseKindStyle(kv.value().as<JsonObjectConst>(), static_cast<style_kind_t>(kind), kv.key().c_str(), sheet);
        }
        forEachTableStyle(styles.as<JsonObjectConst>(), false,
                          [&](const char*, const size_t len, const JsonObjectConst block) {
            bool has[STYLE_PART_COUNT];
            count++;
            parts += styleParts(block, has);
            nameBytes += 1 + len;
        });
    }
    display.lock();
    const bool allocated = ui_style_table_alloc(count, parts, nameBytes);
    // The LVGL task refills a style a script changed, before the widgets
    // of that walk draw (fb.style.set). Always: the styles of the kinds
    // exist without a table.
    ui::setWalkHooks(ui_style_refresh, ui_style_refresh_done);
    display.unlock();
    if (!allocated) {
        ESP_LOGE(TAG, "styles: no memory for %u styles, the device runs with the reserved ones only",
                 static_cast<unsigned>(count));
        return 0;
    }
    if (count == 0) {
        return 0;
    }
    // Parsed straight into the table (no copy). Names and properties are
    // plain data until applyStyleTable(); no item exists yet.
    forEachTableStyle(styles.as<JsonObjectConst>(), true,
                      [](const char* name, const size_t len, const JsonObjectConst block) {
        const uint8_t ref = ui_style_table_add(name, len);
        if (ref == 0) {
            return;  // cannot happen: the table was sized by the same walk
        }
        bool has[STYLE_PART_COUNT];
        styleParts(block, has);
        if (has[STYLE_PART_MAIN]) {
            if (style_props_t* props = ui_style_table_part(ref, STYLE_PART_MAIN)) {
                parseStyleProps(block, *props, name, nullptr);
            }
        }
        for (int part = STYLE_PART_TITLE; part <= STYLE_PART_ICON; part++) {
            const auto sub = block[kPartNames[part]];
            if (sub.isNull()) {
                continue;
            }
            if (!sub.is<JsonObjectConst>()) {
                ESP_LOGW("ConfigManager", "styles.%s.%s is not an object, ignored", name, kPartNames[part]);
                continue;
            }
            if (part == STYLE_PART_ICON) {
                if (uint32_t* icon = ui_style_table_icon(ref)) {
                    parseStyleIcon(sub.as<JsonObjectConst>(), *icon, name);
                }
            } else if (has[part]) {
                if (style_props_t* props = ui_style_table_part(ref, static_cast<style_part_t>(part))) {
                    parseStyleProps(sub.as<JsonObjectConst>(), *props, name, kPartNames[part]);
                }
            }
        }
    });
    return count;
}

// The second step: `count` = what parseStyleTable() returned (0: nothing to
// do). A style that finds no room in the LVGL pool is dropped; one log line
// names them all.
void applyStyleTable(const size_t count) {
    static constexpr auto TAG = "ConfigManager";
    if (count == 0) {
        return;
    }
    auto& display = FreeButton::getLvglDisplay();
    display.lock();
    const uint32_t dropped = ui_style_table_apply();
    display.unlock();
    if (dropped != 0) {
        // Up to STYLE_NAMED_MAX names, each with ", ".
        char names[STYLE_NAMED_MAX * (STYLE_NAME_MAX + 2) + 1];
        size_t at = 0;
        for (size_t i = 0; i < count; i++) {
            size_t len = 0;
            const char* name = (dropped & (1u << i)) ? ui_style_name(static_cast<uint8_t>(i + 1), &len) : nullptr;
            if (name == nullptr || at + len + 3 > sizeof(names)) {
                continue;
            }
            if (at > 0) {
                names[at++] = ',';
                names[at++] = ' ';
            }
            std::memcpy(names + at, name, len);
            at += len;
        }
        names[at] = '\0';
        ESP_LOGE(TAG, "styles: dropped for lack of LVGL memory (the pool is too low to hold their "
                      "properties): %s. Items and scripts that name them get \"unknown style\"", names);
    }
    ESP_LOGI(TAG, "Loaded %u styles (%u bytes)", static_cast<unsigned>(ui_style_count()),
             static_cast<unsigned>(ui_style_table_bytes()));
}

// Keys the config no longer has (style_logic_removed_key): each is logged
// once per key name, with what to use instead. Fed by the reader of
// loadStyles() with every key of the file; nothing is built for a key that
// is reported here (its value is skipped like any key without a filter).
struct RemovedKeyScan {
    int itemScope = -1;    // the scope of the entries of the top-level key being read, -1 = none
    uint32_t warned = 0;   // bit i: removed key i was logged

    static void observe(void* context, const char* key, const size_t length, const int depth) {
        auto& scan = *static_cast<RemovedKeyScan*>(context);
        if (depth == 1) {
            scan.report(CONFIG_SCOPE_TOP, key, length);
            const std::string_view name(key, length);
            scan.itemScope = name == "buttons"        ? CONFIG_SCOPE_BUTTON
                             : name == "displayitems" ? CONFIG_SCOPE_DISPLAYITEM
                             : name == "lists"        ? CONFIG_SCOPE_LIST
                             : name == "brokers"      ? CONFIG_SCOPE_BROKER
                                                      : -1;
        } else if (depth == 3 && scan.itemScope >= 0) {
            // root object 1, the array 2, an entry 3
            scan.report(static_cast<config_scope_t>(scan.itemScope), key, length);
        }
    }

    void report(const config_scope_t scope, const char* key, const size_t length) {
        const char* hint = "";
        const int index = style_logic_removed_key(scope, key, length, &hint);
        if (index < 0 || (warned & (1u << index))) {
            return;
        }
        warned |= 1u << index;
        ESP_LOGW("ConfigManager", "\"%.*s\" is no longer read: %s", static_cast<int>(length), key, hint);
    }
};

} // namespace

ConfigManager::ItemsLock::ItemsLock() {
    xSemaphoreTakeRecursive(displayItemsMutex(), portMAX_DELAY);
}

ConfigManager::ItemsLock::~ItemsLock() {
    xSemaphoreGiveRecursive(displayItemsMutex());
}

void ConfigManager::loadFromFile(const std::string& filename) {
    ESP_LOGI(TAG, "Loading components from file: %s", filename.c_str());

    // Clear existing components
    clear();

    // Fonts and styles first: the items refer to the styles by name and
    // build on the kinds' element styles.
    loadStyles(filename);

    // Use FilteredReaderHandler for memory-efficient streaming processing
    json::FilteredReaderHandler handler;

    // Register filter callbacks
    handler.addFilter({"core"},
        [this](const std::vector<std::string>& path, const std::string& json) {
            processCoreFilter(path, json);
        });

    handler.addFilter({"brokers"},
    [this](const std::vector<std::string>& path, const std::string& json) {
        processMqttBrokersFilter(path, json);
    });

    handler.addFilter({"displayitems"},
    [this](const std::vector<std::string>& path, const std::string& json) {
        processDisplayItemsFilter(path, json);
    });

    handler.addFilter({"buttons"},
        [this](const std::vector<std::string>& path, const std::string& json) {
            processButtonsFilter(path, json);
        });

    handler.addFilter({"clocks"},
        [this](const std::vector<std::string>& path, const std::string& json) {
            processClocksFilter(path, json);
        });

    handler.addFilter({"lists"},
        [this](const std::vector<std::string>& path, const std::string& json) {
            processListsFilter(path, json);
        });

    // "icons" (name -> SVG path) is for the web IDE only: not filtered, so
    // the parser skips it without building anything.
    handler.addFilter({"images"},
        [this](const std::vector<std::string>& path, const std::string& json) {
            processImagesFilter(path, json);
        });

    // Process the file once
    FreeButton::BusAwareFile file = FreeButton::getBusAwareFileSystem().open(filename.c_str(), "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename.c_str());
        return;
    }

    bool success = handler.parse(file);
    file.close();

    if (!success) {
        ESP_LOGE(TAG, "Failed to parse JSON file: %s", filename.c_str());
    }

    names_.compact();
    nextItemIdAfterLoad_ = DisplayItem::next_id;

    // Register all component subscriptions now that brokers are configured
    registerAllSubscriptions();

    ESP_LOGI(TAG, "Loaded %zu display items, %zu buttons, and %zu clocks from file",
             getDisplayItemCount(), buttons.size(), clocks.size());
}

void ConfigManager::loadStyles(const std::string& filename) {
    // "fonts" and "styles" are small: one transient document of each (the
    // rest of the file is only scanned).
    JsonDocument fontsDoc;
    JsonDocument stylesDoc;
    json::FilteredReaderHandler handler;
    handler.addFilter({"fonts"},
        [&](const std::vector<std::string>& path, const std::string& json) {
            if (const auto error = deserializeJson(fontsDoc, json)) {
                ESP_LOGE(TAG, "Failed to parse fonts JSON: %s", error.c_str());
                fontsDoc.clear();
            }
        });
    handler.addFilter({"styles"},
        [&](const std::vector<std::string>& path, const std::string& json) {
            if (const auto error = deserializeJson(stylesDoc, json)) {
                ESP_LOGE(TAG, "Failed to parse styles JSON: %s", error.c_str());
                stylesDoc.clear();
            }
        });
    // The same read reports the keys that are no longer read. They have no
    // filter, so nothing is built for them.
    RemovedKeyScan removed;
    handler.setKeyObserver(&RemovedKeyScan::observe, &removed);

    FreeButton::BusAwareFile file = FreeButton::getBusAwareFileSystem().open(filename.c_str(), "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename.c_str());
    } else {
        const bool success = handler.parse(file);
        file.close();
        if (!success) {
            ESP_LOGE(TAG, "Failed to parse JSON file for fonts and styles: %s", filename.c_str());
        }
    }

    // 1. "fonts". Heap only while loading (FONTS_MAX_FACES x ~165 B); the
    // registry copies what it keeps.
    font_def_t* defs = nullptr;
    size_t count = 0;
    const auto fonts = fontsDoc.as<JsonVariantConst>();
    if (!fonts.isNull() && !fonts.is<JsonArrayConst>()) {
        ESP_LOGW(TAG, "\"fonts\" is not an array, ignored");
    } else if (fonts.size() > 0) {
        // malloc, not new(std::nothrow): that aborts with -fno-exceptions
        defs = static_cast<font_def_t*>(std::malloc(sizeof(font_def_t) * FONTS_MAX_FACES));
        if (defs == nullptr) {
            ESP_LOGE(TAG, "fonts: no memory to load them, the system fonts are used");
        } else {
            for (const auto entry : fonts.as<JsonArrayConst>()) {
                if (count == FONTS_MAX_FACES) {
                    ESP_LOGW(TAG, "fonts: more than %d entries, the rest is left out", FONTS_MAX_FACES);
                    break;
                }
                if (parseFontDef(entry, defs[count])) {
                    count++;
                }
            }
        }
    }

    // Always configured, also without entries: the registry then resolves
    // the system faces' fallbacks.
    auto& display = FreeButton::getLvglDisplay();
    display.lock();
    const size_t accepted = fonts_configure(defs, count);
    display.unlock();
    std::free(defs);
    fontsDoc.clear();
    if (count > 0) {
        ESP_LOGI(TAG, "Loaded %u of %u fonts", static_cast<unsigned>(accepted), static_cast<unsigned>(count));
    }

    // 2. "styles": their font-family names resolve against the registry
    // configured above. The reserved styles go into the sheet, the others
    // into the table, which is sized, allocated and parsed while the
    // document is still there. A config with only reserved styles
    // allocates nothing.
    style_sheet_t sheet;
    std::memset(&sheet, 0, sizeof(sheet));
    const size_t tableStyles = parseStyleTable(stylesDoc.as<JsonVariantConst>(), sheet);
    style_props_t resolved[STYLE_EL_COUNT];
    style_logic_resolve(&sheet, resolved);
    // The document goes before the styles are applied: ui_style_apply and
    // applyStyleTable create font instances (FreeType faces, glyph caches),
    // and the document must not sit on the heap during that peak.
    stylesDoc.clear();
    display.lock();
    ui_style_apply(resolved, sheet.icon);
    display.unlock();
    applyStyleTable(tableStyles);
}

void ConfigManager::clear() {
    ESP_LOGI(TAG, "Clearing all components");

    // Clear vectors - this will automatically call destructors and free memory.
    // Display items are taken out under the lock and destroyed outside it.
    std::vector<std::unique_ptr<DisplayItem>> oldItems;
    std::vector<std::unique_ptr<DisplayItem>> oldParked;
    {
        const ItemsLock lock;
        oldItems.swap(displayItems);
        oldParked.swap(parkedItems_);
    }
    oldItems.clear();
    oldParked.clear();
    buttons.clear();
    clocks.clear();
    lists.clear();
    images.clear();
    names_.clear();

    // Shrink capacity to free memory (displayItems: swapped with an empty one)
    buttons.shrink_to_fit();
    clocks.shrink_to_fit();
}

std::vector<DisplayItem*> ConfigManager::getDisplayItemsByPage(const int page) const {
    std::vector<DisplayItem*> pageItems;

    const ItemsLock lock;
    for (auto& item : displayItems) {
        if (item->page == page) {
            pageItems.push_back(item.get());
        }
    }

    return pageItems;
}

std::vector<Clock*> ConfigManager::getClocksByPage(const int page) const {
    std::vector<Clock*> pageClocks;

    for (auto& clock : clocks) {
        if (clock->getPage() == page) {
            pageClocks.push_back(clock.get());
        }
    }

    return pageClocks;
}

Button* ConfigManager::getButtonByPosition(const int position) const {
    for (auto& button : buttons) {
        if (button->getPosition() == position) {
            return button.get();
        }
    }
    return nullptr;
}

// The pointer stays valid until the item is removed, which only script code
// does; script-code callers (JS engine lock) may therefore keep using it.
DisplayItem* ConfigManager::findDisplayItemById(unsigned long id) const {
    const ItemsLock lock;
    for (const auto& item : displayItems) {
        if (item->id == id) {
            return item.get();
        }
    }
    return nullptr;
}

DisplayItem* ConfigManager::addDisplayItem(std::unique_ptr<DisplayItem> item) {
    if (!item) {
        return nullptr;
    }

    DisplayItem* ptr = item.get();
    {
        const ItemsLock lock;
        displayItems.emplace_back(std::move(item));
    }

    ESP_LOGI(TAG, "Added DisplayItem: id=%lu, label='%s', page=%d",
             ptr->id, ptr->label.c_str(), ptr->page);

    return ptr;
}

bool ConfigManager::removeDisplayItem(unsigned long id) {
    std::unique_ptr<DisplayItem> removed;
    {
        const ItemsLock lock;
        auto it = std::find_if(displayItems.begin(), displayItems.end(),
            [id](const std::unique_ptr<DisplayItem>& item) {
                return item->id == id;
            });
        if (it != displayItems.end()) {
            removed = std::move(*it);
            displayItems.erase(it);
            if (removed->fromConfig()) {
                // Parked, not destroyed: what config.json says about it is
                // only in this object (restoreConfigState()).
                removed->park();
                parkedItems_.emplace_back(std::move(removed));
                ESP_LOGI(TAG, "Parked config DisplayItem: id=%lu", id);
                return true;
            }
        }
    }

    if (removed) {
        ESP_LOGI(TAG, "Removing DisplayItem: id=%lu", id);
        // Destroyed here, outside the lock: the destructor unsubscribes
        // (broker lock) and deletes the box (LVGL lock).
        removed.reset();
        return true;
    }

    ESP_LOGW(TAG, "DisplayItem not found: id=%lu", id);
    return false;
}

// Main task, script engine lock held (no script code runs, so nothing adds or
// removes display items meanwhile), LVGL lock not held.
//
// Locks, never nested: 1. the items lock (per vector operation); the
// destructor of a script-made item, outside it, takes the LVGL lock;
// 2. the LVGL lock, once, for all display items and keys (only stores and
// flags; the LVGL task draws afterwards; inside it the text slots' writer
// spinlock per reset slot), also for the styles' values; 3. the
// lists' items mutex, per list.
// No heap is allocated; memory is only given back.
void ConfigManager::restoreConfigState() {
    // 1. Items a script made go, one at a time: taken out under the lock,
    // destroyed outside it (the destructor takes the LVGL lock).
    while (true) {
        std::unique_ptr<DisplayItem> scriptItem;
        {
            const ItemsLock lock;
            const auto it = std::find_if(displayItems.begin(), displayItems.end(),
                [](const std::unique_ptr<DisplayItem>& item) { return !item->fromConfig(); });
            if (it == displayItems.end()) {
                break;
            }
            scriptItem = std::move(*it);
            displayItems.erase(it);
        }
        ESP_LOGI(TAG, "Script restart: removing script-made DisplayItem id=%lu", scriptItem->id);
    }

    // Config items a script removed come back at their config place (what
    // is left is in config order). The vector kept its capacity.
    {
        const ItemsLock lock;
        for (auto& parked : parkedItems_) {
            const int order = parked->configOrder();
            const auto pos = std::find_if(displayItems.begin(), displayItems.end(),
                [order](const std::unique_ptr<DisplayItem>& item) { return item->configOrder() > order; });
            displayItems.insert(pos, std::move(parked));
        }
        parkedItems_.clear();
        parkedItems_.shrink_to_fit();
        if (nextItemIdAfterLoad_ != 0) {
            DisplayItem::next_id = nextItemIdAfterLoad_;
        }
    }

    // 2. Run-time state of the display items and keys. The LVGL lock keeps
    // their applyPending() from running while slots and flags are reset.
    {
        auto& display = FreeButton::getLvglDisplay();
        display.lock();
        for (auto& item : displayItems) {
            item->restoreConfigState();
        }
        for (auto& button : buttons) {
            button->restoreConfigState();
        }
        // The values fb.style.set changed: the config's again (frees the
        // copy; the LVGL task refills the styles in its next walk).
        ui_style_restore();
        display.unlock();
    }

    // 3. Lists, images, the shown page, LEDs.
    for (auto& l : lists) {
        l->restoreConfigState();
    }
    for (auto& image : images) {
        image->restoreConfigState();
    }
    ui::showPage(ui::kFirstPage);
    ButtonLed::restoreScriptPixels();

    ui::requestPendingRender();
}

ConfigManager::MemoryStats ConfigManager::getMemoryStats() const {
    const ItemsLock lock;
    MemoryStats stats{};
    stats.displayItemCount = displayItems.size();
    stats.buttonCount = buttons.size();
    stats.clockCount = clocks.size();

    // Rough estimation of memory usage
    stats.estimatedMemoryBytes =
        (displayItems.size() * sizeof(DisplayItem)) +
        (buttons.size() * sizeof(Button)) +
        (clocks.size() * sizeof(Clock)) +
        (displayItems.capacity() * sizeof(std::unique_ptr<DisplayItem>)) +
        (buttons.capacity() * sizeof(std::unique_ptr<Button>)) +
        (clocks.capacity() * sizeof(std::unique_ptr<Clock>));

    return stats;
}

void ConfigManager::loadDisplayItems(const ArduinoJson::JsonArray& itemsArray) {
    ESP_LOGI(TAG, "Loading %zu display items", itemsArray.size());

    // Reserve capacity for efficiency; keep auto ids above the configured ones.
    unsigned long maxConfiguredId = 0;
    for (const auto& itemElement : itemsArray) {
        if (itemElement.is<ArduinoJson::JsonObject>()) {
            maxConfiguredId = std::max(maxConfiguredId,
                                       DisplayItem::configuredId(itemElement.as<ArduinoJson::JsonObject>()));
        }
    }
    DisplayItem::reserveIdsUpTo(maxConfiguredId);
    {
        const ItemsLock lock;
        displayItems.reserve(displayItems.size() + itemsArray.size());
    }

    for (const auto& itemElement : itemsArray) {
        if (!itemElement.is<ArduinoJson::JsonObject>()) {
            ESP_LOGW(TAG, "Display item element is not a JSON object, skipping");
            continue;
        }

        auto item = DisplayItem::fromJson(itemElement.as<ArduinoJson::JsonObject>());
        if (item) {
            ESP_LOGD(TAG, "Created DisplayItem: id=%lu, label='%s', page=%d",
                    item->id, item->label.c_str(), item->page);
            const ItemsLock lock;  // item (and its box) built outside the lock
            for (const auto& existing : displayItems) {
                if (existing->id == item->id) {
                    const unsigned long duplicate = item->id;
                    item->id = DisplayItem::next_id++;
                    ESP_LOGW(TAG, "Display item id %lu is used twice; the second one gets id %lu", duplicate,
                             item->id);
                    break;
                }
            }
            addName(NameTable::Kind::DisplayItem, item->id, itemElement.as<ArduinoJson::JsonObject>());
            item->markFromConfig(static_cast<int>(displayItems.size()));
            displayItems.emplace_back(std::move(item));
        } else {
            ESP_LOGW(TAG, "Failed to create DisplayItem from JSON");
        }
    }

    ESP_LOGI(TAG, "Successfully loaded %zu display items", getDisplayItemCount());
}

void ConfigManager::addName(const NameTable::Kind kind, const unsigned long id, const JsonObject& config) {
    const auto value = config["name"];
    if (value.isNull()) {
        return;
    }
    const char* what = kind == NameTable::Kind::Key    ? "Key at position"
                     : kind == NameTable::Kind::List   ? "List"
                     : kind == NameTable::Kind::Image  ? "Image"
                                                       : "Display item";
    const char* name = value.as<const char*>();
    if (name == nullptr) {
        ESP_LOGW(TAG, "%s %lu: \"name\" is not a string, ignored", what, id);
        return;
    }
    switch (names_.add(kind, static_cast<uint32_t>(id), name)) {
    case NameTable::AddResult::Added:
        break;
    case NameTable::AddResult::Invalid:
        ESP_LOGW(TAG, "%s %lu: name \"%s\" is not valid (letters, digits, _; not starting with a digit; max %u), "
                      "ignored", what, id, name, static_cast<unsigned>(NameTable::kMaxLen));
        break;
    case NameTable::AddResult::Duplicate:
        ESP_LOGW(TAG, "%s %lu: name \"%s\" is already used, ignored (the first one keeps it)", what, id, name);
        break;
    }
}

void ConfigManager::loadButtons(const ArduinoJson::JsonArray& buttonsArray) {
    ESP_LOGI(TAG, "Loading %zu buttons", buttonsArray.size());

    // Reserve capacity for efficiency
    buttons.reserve(buttonsArray.size());

    for (const auto& buttonElement : buttonsArray) {
        if (!buttonElement.is<ArduinoJson::JsonObject>()) {
            ESP_LOGW(TAG, "Button element is not a JSON object, skipping");
            continue;
        }

        // The position says which key this is. Without one there is no
        // button: nothing is built, subscribed or named for the entry.
        if (!configlogic::buttonHasPosition(buttonElement)) {
            ESP_LOGW(TAG, "A button has no \"position\": it is not shown");
            continue;
        }

        auto button = Button::fromJson(buttonElement.as<ArduinoJson::JsonObject>());
        if (button) {
            ESP_LOGD(TAG, "Created Button: id=%lu, label='%s', topLabel='%s'",
                    button->id, button->label.c_str(), button->topLabel.c_str());
            addName(NameTable::Kind::Key, button->getPosition(), buttonElement.as<ArduinoJson::JsonObject>());
            buttons.emplace_back(std::move(button));
        } else {
            ESP_LOGW(TAG, "Failed to create Button from JSON");
        }
    }

    ESP_LOGI(TAG, "Successfully loaded %zu buttons", buttons.size());
}

void ConfigManager::loadClocks(const JsonArray& clocksArray) {
    ESP_LOGI(TAG, "Loading %zu clocks", clocksArray.size());

    // Reserve capacity for efficiency
    clocks.reserve(clocksArray.size());

    for (const auto& clockElement : clocksArray) {
        if (!clockElement.is<JsonObject>()) {
            ESP_LOGW(TAG, "Clock element is not a JSON object, skipping");
            continue;
        }

        auto clock = Clock::fromJson(clockElement.as<ArduinoJson::JsonObject>());
        if (clock) {
            ESP_LOGD(TAG, "Created Clock: id=%lu, x=%d, y=%d, page=%d",
                    clock->id, clock->getX(), clock->getY(), clock->getPage());
            clocks.emplace_back(std::move(clock));
        } else {
            ESP_LOGW(TAG, "Failed to create Clock from JSON");
        }
    }

    ESP_LOGI(TAG, "Successfully loaded %zu clocks", clocks.size());
}

void ConfigManager::loadBrokers(const ArduinoJson::JsonArray& brokersArray) {
    ESP_LOGI(TAG, "Loading %zu MQTT brokers", brokersArray.size());

    auto& registry = MqttBrokerRegistry::instance();

    for (const auto& brokerElement : brokersArray) {
        if (!brokerElement.is<ArduinoJson::JsonObject>()) {
            ESP_LOGW(TAG, "Broker element is not a JSON object, skipping");
            continue;
        }

        const auto& brokerObj = brokerElement.as<ArduinoJson::JsonObject>();

        // Extract required fields
        const char* brokerId = brokerObj["brokerid"];
        const char* url = brokerObj["url"];

        if (!brokerId || !url) {
            ESP_LOGW(TAG, "Broker missing required fields (brokerid, url), skipping");
            continue;
        }

        // Extract optional fields with defaults
        uint32_t port = brokerObj["port"] | 0;
        uint32_t wsPort = brokerObj["wsport"] | 0;
        const char* username = brokerObj["username"] | "";
        const char* password = brokerObj["password"] | "";

        // "hassdiscovery" (what Broker::toJson writes). Absent = enabled;
        // otherwise the value as a boolean (0 and false: off). Not
        // `| true`: that reads every value that is no JSON boolean as true.
        const bool autoDiscovery = configlogic::hassDiscovery(brokerObj["hassdiscovery"]);

        // Check if broker already exists (auto-registered by components)
        uint8_t existingId = MqttBrokerRegistry::getBrokerId(brokerId);
        if (existingId != 255) {
            MqttBroker* existingBroker = MqttBrokerRegistry::getBroker(existingId);
            // Update existing broker configuration
            ESP_LOGI(TAG, "Updating existing broker '%s' with configuration", brokerId);
            existingBroker->updateConfig(std::string(url), port, wsPort,
                                       std::string(username), std::string(password), autoDiscovery);
        } else {
            // Create new broker with full configuration
            auto broker = std::make_unique<MqttBroker>(
                std::string(brokerId),
                std::string(url),
                port,
                wsPort,
                std::string(username),
                std::string(password),
                autoDiscovery
            );

            uint8_t id = registry.addBroker(std::move(broker));
            if (id != 255) {
                ESP_LOGI(TAG, "Successfully registered new broker '%s' with ID %d", brokerId, id);
            } else {
                ESP_LOGE(TAG, "Failed to register broker '%s'", brokerId);
            }
        }
    }

    ESP_LOGI(TAG, "Successfully processed %zu MQTT brokers", brokersArray.size());
}

void ConfigManager::registerAllSubscriptions() {
    ESP_LOGI(TAG, "Registering subscriptions for all components");

    size_t totalSubscriptions = 0;

    // Register subscriptions for display items
    forEachDisplayItem([&totalSubscriptions](DisplayItem& item) {
        item.registerTopics();
        totalSubscriptions++;
    });

    // Register subscriptions for buttons
    forEachButton([&totalSubscriptions](Button& button) {
        button.registerTopics();
        totalSubscriptions++;
    });

    ESP_LOGI(TAG, "Registered subscriptions for %zu components", totalSubscriptions);
}

void ConfigManager::startBrokers(const std::string& deviceId) {
    ESP_LOGI(TAG, "Starting all brokers and configuring Home Assistant discovery");

    auto& registry = MqttBrokerRegistry::instance();

    // Configure each broker with Home Assistant discovery
    MqttBrokerRegistry::forEachBroker([](uint8_t brokerId, MqttBroker* broker) {
        if (broker && broker->isConfigured()) {
            // Set up connection callback for HomeAssistant discovery
            broker->setOnConnectedCallback([](const uint8_t connectedBrokerId) {
                ESP_LOGI(TAG, "Broker %d connected, triggering Home Assistant discovery", connectedBrokerId);
                HomeAssistantDiscovery::onBrokerConnected(connectedBrokerId);
            });

            // Configure Home Assistant discovery for this broker ("hassdiscovery")
            HomeAssistantDiscovery::configureBroker(brokerId, broker->hasAutoDiscovery());

            ESP_LOGI(TAG, "Broker %d: %.*s, Home Assistant discovery %s", brokerId,
                    (int)broker->getName().length(), broker->getName().data(),
                    broker->hasAutoDiscovery() ? "enabled" : "disabled");
        } else {
            ESP_LOGW(TAG, "Skipping unconfigured broker at ID %d", brokerId);
        }
    });

    // Start all configured brokers
    registry.connectAll();

    ESP_LOGI(TAG, "All brokers started and Home Assistant discovery configured");
}

void ConfigManager::processCoreFilter(const std::vector<std::string>& path, const std::string& json) {
    JsonDocument doc;
    auto error = deserializeJson(doc, json);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse core JSON: %s", error.c_str());
        return;
    }

    if (!doc["name"].isNull()) {
        HomeAssistantDiscovery::setDeviceName(doc["name"].as<std::string>());
    }

    if (!doc["ntpserver"].isNull()) {
        ntpServer = doc["ntpserver"].as<std::string>();
    }

    if (!doc["timezone"].isNull()) {
        timezone = doc["timezone"].as<std::string>();
    }
}

void ConfigManager::processMqttBrokersFilter(const std::vector<std::string>& path, const std::string& json) {
    JsonDocument doc;
    auto error = deserializeJson(doc, json);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse brokers JSON: %s", error.c_str());
        return;
    }

    if (doc.is<JsonArray>()) {
        loadBrokers(doc.as<JsonArray>());
    }
}

void ConfigManager::processDisplayItemsFilter(const std::vector<std::string>& path, const std::string& json) {
    JsonDocument doc;
    auto error = deserializeJson(doc, json);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse displayitems JSON: %s", error.c_str());
        return;
    }

    if (doc.is<JsonArray>()) {
        loadDisplayItems(doc.as<JsonArray>());
    }
}

void ConfigManager::processButtonsFilter(const std::vector<std::string>& path, const std::string& json) {
    JsonDocument doc;
    auto error = deserializeJson(doc, json);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse buttons JSON: %s", error.c_str());
        return;
    }

    if (doc.is<JsonArray>()) {
        loadButtons(doc.as<JsonArray>());
    }
}

void ConfigManager::processClocksFilter(const std::vector<std::string>& path, const std::string& json) {
    JsonDocument doc;
    auto error = deserializeJson(doc, json);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse clocks JSON: %s", error.c_str());
        return;
    }

    if (doc.is<JsonArray>()) {
        loadClocks(doc.as<JsonArray>());
    }
}

void ConfigManager::loadLists(const JsonArray& listsArray) {
    for (const auto& element : listsArray) {
        if (!element.is<JsonObject>()) {
            ESP_LOGW(TAG, "List element is not a JSON object, skipping");
            continue;
        }
        const auto config = element.as<ArduinoJson::JsonObject>();
        auto l = List::fromJson(config);
        const size_t index = lists.size();
        addName(NameTable::Kind::List, index, config);
        if (names_.nameOf(NameTable::Kind::List, static_cast<uint32_t>(index)).empty()) {
            ESP_LOGW(TAG, "List %u has no valid name: scripts can only use its index", static_cast<unsigned>(index));
        }
        l->attach();
        ESP_LOGI(TAG, "List %u: %d items on page %d", static_cast<unsigned>(index), l->count(), l->getPage());
        lists.emplace_back(std::move(l));
    }
}

void ConfigManager::processListsFilter(const std::vector<std::string>& path, const std::string& json) {
    JsonDocument doc;
    const auto error = deserializeJson(doc, json);
    if (error) {
        ESP_LOGE(TAG, "Failed to parse lists JSON: %s", error.c_str());
        return;
    }
    if (doc.is<JsonArray>()) {
        loadLists(doc.as<JsonArray>());
    }
}

} // namespace freebutton

namespace freebutton {

// "images": main-display rasters (ImageItem.h). Units: x / y are PIXELS of
// the 320x240 main display (top-left of the raster), the same unit as width /
// height (which pick the icon raster) - unlike display items and lists, whose
// x / y / width are % of the screen. "color" is "#rrggbb" or "rrggbb".
//
// The top-level "icons" table (icon name -> SVG path) is web IDE data and is
// not kept on the device: the IDE reads and writes config.json in the app
// bucket, and GET /config (regenerated from RAM) leaves "icons" out.
void ConfigManager::loadImages(const JsonArray& imagesArray) {
    images.reserve(images.size() + imagesArray.size());
    for (const auto& element : imagesArray) {
        if (!element.is<JsonObject>()) {
            ESP_LOGW(TAG, "Image element is not a JSON object, skipping");
            continue;
        }
        const auto config = element.as<ArduinoJson::JsonObject>();
        auto image = ImageItem::fromJson(config);
        const size_t index = images.size();
        addName(NameTable::Kind::Image, index, config);
        if (names_.nameOf(NameTable::Kind::Image, static_cast<uint32_t>(index)).empty()) {
            ESP_LOGW(TAG, "Image %u has no valid name: scripts can only use its index", static_cast<unsigned>(index));
        }
        image->attach();
        ESP_LOGI(TAG, "Image %u on page %d", static_cast<unsigned>(index), image->getPage());
        images.emplace_back(std::move(image));
    }
}

void ConfigManager::processImagesFilter(const std::vector<std::string>& path, const std::string& json) {
    JsonDocument doc;
    const auto error = deserializeJson(doc, json);
    if (error) {
        ESP_LOGE(TAG, "Failed to parse images JSON: %s", error.c_str());
        return;
    }
    if (doc.is<JsonArray>()) {
        loadImages(doc.as<JsonArray>());
    }
}

} // namespace freebutton
