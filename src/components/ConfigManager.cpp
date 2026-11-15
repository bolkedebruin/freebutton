//
// ConfigManager - Efficient management of multiple buttons and display items
// Created by Bolke de Bruin on 19/11/2025.
//

#include "ConfigManager.h"
#include "json/FilteredReaderHandler.h"
#include <algorithm>
#include <esp_log.h>
#include "LittleFS.h"
#include "drivers/BusAwareFileSystem.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include "Clock.h"
#include "drivers/LvglDisplay.h"
#include "ui/ui_style.h"
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

// One entry of config.json "style.fonts" -> def. false (logged) when it cannot be
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

// style_logic text_font resolver: the registry (configured first).
int resolveStyleFont(const char* name, const size_t len, const bool is_id, const int32_t id, void*) {
    return is_id ? fonts_find_id(id) : fonts_find(name, len);
}

// One style block ({"text_font": ..., ...}) into p. `skip`: a key that is
// not a property here (list's "selected"), or nullptr.
void parseStyleProps(const JsonObjectConst block, style_props_t& p, const bool allow_bg, const char* where,
                     const char* skip) {
    static constexpr auto TAG = "ConfigManager";
    for (const auto kv : block) {
        const char* prop = kv.key().c_str();
        if (skip != nullptr && std::strcmp(prop, skip) == 0) {
            continue;
        }
        const auto v = kv.value();
        style_prop_status_t st;
        if (v.is<const char*>()) {
            st = style_logic_set_string(&p, prop, v.as<const char*>(), allow_bg, resolveStyleFont, nullptr);
        } else if (v.is<int32_t>()) {
            st = style_logic_set_number(&p, prop, v.as<int32_t>(), allow_bg, resolveStyleFont, nullptr);
        } else {
            st = STYLE_PROP_BAD_VALUE;
        }
        switch (st) {
            case STYLE_PROP_OK:
                break;
            case STYLE_PROP_UNKNOWN:
                ESP_LOGW(TAG, "style.%s: unknown property \"%.32s\", ignored", where, prop);
                break;
            case STYLE_PROP_UNKNOWN_FONT:
                ESP_LOGW(TAG, "style.%s.%s: unknown font, ignored", where, prop);
                break;
            default:
                ESP_LOGW(TAG, "style.%s.%s: bad value, ignored", where, prop);
                break;
        }
    }
}

// config.json "style" minus "fonts" -> sheet (layers 2 and 3).
void parseStyleSheet(const JsonObjectConst style, style_sheet_t& sheet) {
    static constexpr auto TAG = "ConfigManager";
    for (const auto kv : style) {
        const char* key = kv.key().c_str();
        if (std::strcmp(key, "fonts") == 0) {
            continue;
        }
        const auto block = kv.value();
        if (!block.is<JsonObjectConst>()) {
            ESP_LOGW(TAG, "style.%.32s is not an object, ignored", key);
            continue;
        }
        const auto obj = block.as<JsonObjectConst>();
        if (const int role = style_logic_role(key); role >= 0) {
            parseStyleProps(obj, sheet.role[role], false, key, nullptr);
            continue;
        }
        if (std::strcmp(key, "list") == 0) {
            parseStyleProps(obj, sheet.element[STYLE_EL_LIST], false, "list", "selected");
            const auto selected = obj["selected"];
            if (selected.is<JsonObjectConst>()) {
                parseStyleProps(selected.as<JsonObjectConst>(), sheet.element[STYLE_EL_LIST_SELECTED], true,
                                "list.selected", nullptr);
            } else if (!selected.isNull()) {
                ESP_LOGW(TAG, "style.list.selected is not an object, ignored");
            }
            continue;
        }
        if (std::strcmp(key, "key") != 0 && std::strcmp(key, "display") != 0) {
            ESP_LOGW(TAG, "style: unknown key \"%.32s\", ignored", key);
            continue;
        }
        for (const auto el : obj) {
            const char* name = el.key().c_str();
            const int e = style_logic_element(key, name);
            if (e < 0) {
                ESP_LOGW(TAG, "style.%s: unknown element \"%.32s\", ignored", key, name);
                continue;
            }
            if (!el.value().is<JsonObjectConst>()) {
                ESP_LOGW(TAG, "style.%s.%.32s is not an object, ignored", key, name);
                continue;
            }
            char where[48];
            std::snprintf(where, sizeof(where), "%s.%s", key, name);
            parseStyleProps(el.value().as<JsonObjectConst>(), sheet.element[e], false, where, nullptr);
        }
    }
}

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

    // Style first: items resolve their "font" names and build on the
    // element styles.
    loadStyle(filename);

    // Use FilteredReaderHandler for memory-efficient streaming processing
    json::FilteredReaderHandler handler;

    // Register filter callbacks
    handler.addFilter({"core"},
        [this](const std::vector<std::string>& path, const std::string& json) {
            processCoreFilter(path, json);
        });

    handler.addFilter({"mqttbrokers"},
        [this](const std::vector<std::string>& path, const std::string& json) {
            processMqttBrokersFilter(path, json);
        });
    handler.addFilter({"brokers"},
    [this](const std::vector<std::string>& path, const std::string& json) {
        processMqttBrokersFilter(path, json);
    });

    handler.addFilter({"mqttdisplays"},
        [this](const std::vector<std::string>& path, const std::string& json) {
            processDisplayItemsFilter(path, json);
        });
    handler.addFilter({"displayitems"},
    [this](const std::vector<std::string>& path, const std::string& json) {
        processDisplayItemsFilter(path, json);
    });

    handler.addFilter({"mqttbuttons"},
        [this](const std::vector<std::string>& path, const std::string& json) {
            processButtonsFilter(path, json);
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

void ConfigManager::loadStyle(const std::string& filename) {
    // "style" is small: one transient document of it (the rest of the file
    // is only scanned).
    JsonDocument doc;
    bool found = false;
    json::FilteredReaderHandler handler;
    handler.addFilter({"style"},
        [&](const std::vector<std::string>& path, const std::string& json) {
            if (const auto error = deserializeJson(doc, json)) {
                ESP_LOGE(TAG, "Failed to parse style JSON: %s", error.c_str());
                doc.clear();
                return;
            }
            found = true;
        });

    FreeButton::BusAwareFile file = FreeButton::getBusAwareFileSystem().open(filename.c_str(), "r");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename.c_str());
    } else {
        const bool success = handler.parse(file);
        file.close();
        if (!success) {
            ESP_LOGE(TAG, "Failed to parse JSON file for style: %s", filename.c_str());
        }
    }
    if (found && !doc.is<JsonObject>()) {
        ESP_LOGW(TAG, "\"style\" is not an object, ignored");
        found = false;
    }
    const auto style = doc.as<JsonObjectConst>();

    // style.fonts. Heap only while loading (FONTS_MAX_FACES x ~165 B); the
    // registry copies what it keeps.
    font_def_t* defs = nullptr;
    size_t count = 0;
    if (found) {
        const auto fonts = style["fonts"];
        if (!fonts.isNull() && !fonts.is<JsonArrayConst>()) {
            ESP_LOGW(TAG, "style.fonts is not an array, ignored");
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
    }

    // Always configured, also without entries: the registry then resolves
    // the system faces' fallbacks.
    auto& display = FreeButton::getLvglDisplay();
    display.lock();
    const size_t accepted = fonts_configure(defs, count);
    display.unlock();
    std::free(defs);
    if (count > 0) {
        ESP_LOGI(TAG, "Loaded %u of %u fonts", static_cast<unsigned>(accepted), static_cast<unsigned>(count));
    }

    // The roles and elements: their text_font names resolve against the
    // registry configured above.
    style_sheet_t sheet;
    std::memset(&sheet, 0, sizeof(sheet));
    if (found) {
        parseStyleSheet(style, sheet);
    }
    doc.clear();
    style_props_t resolved[STYLE_EL_COUNT];
    style_logic_resolve(&sheet, resolved);
    display.lock();
    ui_style_apply(resolved);
    display.unlock();
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
// spinlock per reset slot); 3. the lists' items mutex, per list.
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

        // Button+ configs (and Broker::toJson) use "hassdiscovery"; "autoDiscovery"
        // is accepted as a legacy alias. Absent = enabled (previous behaviour).
        bool autoDiscovery = true;
        if (!brokerObj["hassdiscovery"].isNull()) {
            autoDiscovery = brokerObj["hassdiscovery"].as<bool>();
        } else if (!brokerObj["autoDiscovery"].isNull()) {
            autoDiscovery = brokerObj["autoDiscovery"].as<bool>();
        }

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
