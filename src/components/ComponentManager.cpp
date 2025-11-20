//
// ComponentManager - Efficient management of multiple buttons and display items
// Created by Bolke de Bruin on 19/11/2025.
//

#include "ComponentManager.h"
#include "json/FilteredReaderHandler.h"
#include <esp_log.h>
#include "LittleFS.h"
#include "drivers/BusAwareFileSystem.h"

namespace freebutton {

void ComponentManager::loadFromFile(const std::string& filename) {
    ESP_LOGI(TAG, "Loading components from file: %s", filename.c_str());

    // Clear existing components
    clear();

    // Use FilteredReaderHandler for memory-efficient streaming processing
    json::FilteredReaderHandler handler;

    // Stream process display items
    handler.addFilter({"displayitems"}, [this](const std::vector<std::string>& path, const std::string& json) {
        ArduinoJson::JsonDocument doc;
        auto error = deserializeJson(doc, json);

        if (error) {
            ESP_LOGE(TAG, "Failed to parse displayitems JSON: %s", error.c_str());
            return;
        }

        if (doc.is<ArduinoJson::JsonArray>()) {
            loadDisplayItems(doc.as<ArduinoJson::JsonArray>());
        }
    });

    // Stream process buttons
    handler.addFilter({"buttons"}, [this](const std::vector<std::string>& path, const std::string& json) {
        ArduinoJson::JsonDocument doc;
        auto error = deserializeJson(doc, json);

        if (error) {
            ESP_LOGE(TAG, "Failed to parse buttons JSON: %s", error.c_str());
            return;
        }

        if (doc.is<ArduinoJson::JsonArray>()) {
            loadButtons(doc.as<ArduinoJson::JsonArray>());
        }
    });

    // Process the file
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

    ESP_LOGI(TAG, "Loaded %zu display items and %zu buttons from file",
             displayItems.size(), buttons.size());
}

void ComponentManager::clear() {
    ESP_LOGI(TAG, "Clearing all components");

    // Clear vectors - this will automatically call destructors and free memory
    displayItems.clear();
    buttons.clear();

    // Shrink capacity to free memory
    displayItems.shrink_to_fit();
    buttons.shrink_to_fit();
}

std::vector<DisplayItem*> ComponentManager::getDisplayItemsByPage(int page) {
    std::vector<DisplayItem*> pageItems;

    for (auto& item : displayItems) {
        if (item->page == page) {
            pageItems.push_back(item.get());
        }
    }

    return pageItems;
}

ComponentManager::MemoryStats ComponentManager::getMemoryStats() const {
    MemoryStats stats;
    stats.displayItemCount = displayItems.size();
    stats.buttonCount = buttons.size();

    // Rough estimation of memory usage
    stats.estimatedMemoryBytes =
        (displayItems.size() * sizeof(DisplayItem)) +
        (buttons.size() * sizeof(Button)) +
        (displayItems.capacity() * sizeof(std::unique_ptr<DisplayItem>)) +
        (buttons.capacity() * sizeof(std::unique_ptr<Button>));

    return stats;
}

void ComponentManager::loadDisplayItems(const ArduinoJson::JsonArray& itemsArray) {
    ESP_LOGI(TAG, "Loading %zu display items", itemsArray.size());

    // Reserve capacity for efficiency
    displayItems.reserve(itemsArray.size());

    for (const auto& itemElement : itemsArray) {
        if (!itemElement.is<ArduinoJson::JsonObject>()) {
            ESP_LOGW(TAG, "Display item element is not a JSON object, skipping");
            continue;
        }

        auto item = DisplayItem::fromJson(itemElement.as<ArduinoJson::JsonObject>());
        if (item) {
            ESP_LOGD(TAG, "Created DisplayItem: id=%lu, label='%s', page=%d",
                    item->id, item->label.c_str(), item->page);
            displayItems.emplace_back(std::move(item));
        } else {
            ESP_LOGW(TAG, "Failed to create DisplayItem from JSON");
        }
    }

    ESP_LOGI(TAG, "Successfully loaded %zu display items", displayItems.size());
}

void ComponentManager::loadButtons(const ArduinoJson::JsonArray& buttonsArray) {
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
            buttons.emplace_back(std::move(button));
        } else {
            ESP_LOGW(TAG, "Failed to create Button from JSON");
        }
    }

    ESP_LOGI(TAG, "Successfully loaded %zu buttons", buttons.size());
}

} // namespace freebutton