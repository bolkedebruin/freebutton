//
// DisplayItem Binding
// C-compatible interface to C++ DisplayItem components
//

#include <Arduino.h>
#include <esp_log.h>
#include "displayitem_binding.h"
#include "components/ConfigManager.h"
#include "components/DisplayItem.h"
#include "components/Topics.h"
#include "drivers/LvglDisplay.h"

extern "C" {
#include "ui/ui.h"
}

static const char* TAG = "DisplayItemBinding";

// Maximum number of display items for iteration
static constexpr int MAX_DISPLAY_ITEMS = 32;

// Static buffer for returning display item info (to avoid dynamic allocation)
static DisplayItemInfo cachedInfo;

// External reference to global ConfigManager
extern freebutton::ConfigManager configManager;

extern "C" {

unsigned long displayitem_create(
    int x, int y, int width, int boxType,
    int fontSize, int alignment,
    const char* label, const char* unit,
    int round, int page)
{
    // Create DisplayItem with empty topics (topics will be added separately if needed)
    auto item = std::make_unique<freebutton::DisplayItem>(
        x, y, width, boxType,
        fontSize, alignment,
        label ? std::string(label) : "",
        unit ? std::string(unit) : "",
        round, page,
        std::vector<Topic>{}
    );

    if (!item) {
        ESP_LOGE(TAG, "Failed to create DisplayItem");
        return 0;
    }

    unsigned long id = item->id;

    // Create the UI box
    item->createBox();

    // Add to ConfigManager (this transfers ownership)
    freebutton::DisplayItem* added = configManager.addDisplayItem(std::move(item));
    if (!added) {
        ESP_LOGE(TAG, "Failed to add DisplayItem to ConfigManager");
        return 0;
    }

    ESP_LOGI(TAG, "Created DisplayItem id=%lu at (%d,%d) page=%d", id, x, y, page);
    return id;
}

bool displayitem_remove(unsigned long id)
{
    bool removed = configManager.removeDisplayItem(id);
    if (removed) {
        ESP_LOGI(TAG, "Removed DisplayItem id=%lu", id);
    }
    return removed;
}

int displayitem_count(void)
{
    return static_cast<int>(configManager.getDisplayItemCount());
}

const DisplayItemInfo* displayitem_get_info(unsigned long id)
{
    freebutton::DisplayItem* item = configManager.findDisplayItemById(id);
    if (!item) {
        ESP_LOGW(TAG, "DisplayItem %lu not found", id);
        return nullptr;
    }

    // Populate the static info struct
    cachedInfo.id = item->id;
    cachedInfo.x = item->x;
    cachedInfo.y = item->y;
    cachedInfo.width = item->width;
    cachedInfo.boxType = item->boxType;
    cachedInfo.fontSize = item->fontSize;
    cachedInfo.alignment = item->alignment;
    cachedInfo.label = item->label.c_str();
    cachedInfo.unit = item->unit.c_str();
    cachedInfo.round = item->round;
    cachedInfo.page = item->page;

    return &cachedInfo;
}

bool displayitem_set_label(const unsigned long id, const char* label)
{
    freebutton::DisplayItem* item = configManager.findDisplayItemById(id);
    if (!item) {
        ESP_LOGW(TAG, "DisplayItem %lu not found", id);
        return false;
    }

    item->label = label ? label : "";
    item->setDisplayLabel(label ? label : "");

    return true;
}

bool displayitem_set_value(const unsigned long id, const char* value)
{
    const freebutton::DisplayItem* item = configManager.findDisplayItemById(id);
    if (!item) {
        ESP_LOGW(TAG, "DisplayItem %lu not found", id);
        return false;
    }

    item->setDisplayValue(value ? value : "");

    return true;
}

bool displayitem_set_unit(unsigned long id, const char* unit)
{
    freebutton::DisplayItem* item = configManager.findDisplayItemById(id);
    if (!item) {
        ESP_LOGW(TAG, "DisplayItem %lu not found", id);
        return false;
    }

    item->unit = unit ? unit : "";
    item->setDisplayUnit(unit ? unit : "");

    return true;
}

bool displayitem_set_position(unsigned long id, int x, int y)
{
    freebutton::DisplayItem* item = configManager.findDisplayItemById(id);
    if (!item) {
        ESP_LOGW(TAG, "DisplayItem %lu not found", id);
        return false;
    }

    item->x = x;
    item->y = y;

    // Redraw to apply changes
    item->createBox();

    return true;
}

bool displayitem_set_page(unsigned long id, int page)
{
    freebutton::DisplayItem* item = configManager.findDisplayItemById(id);
    if (!item) {
        ESP_LOGW(TAG, "DisplayItem %lu not found", id);
        return false;
    }

    item->page = page;

    // Note: Page changes may require additional handling for visibility

    return true;
}

int displayitem_get_all_ids(unsigned long* ids, int maxCount)
{
    if (!ids || maxCount <= 0) {
        ESP_LOGE(TAG, "Invalid parameters to displayitem_get_all_ids");
        return 0;
    }

    int count = 0;
    configManager.forEachDisplayItem([&count, ids, maxCount](freebutton::DisplayItem& item) {
        if (count < maxCount) {
            ids[count++] = item.id;
        }
    });

    ESP_LOGD(TAG, "Retrieved %d display item IDs", count);
    return count;
}

} // extern "C"
