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

    // Register all component subscriptions now that brokers are configured
    registerAllSubscriptions();

    ESP_LOGI(TAG, "Loaded %zu display items, %zu buttons, and %zu clocks from file",
             getDisplayItemCount(), buttons.size(), clocks.size());
}

void ConfigManager::clear() {
    ESP_LOGI(TAG, "Clearing all components");

    // Clear vectors - this will automatically call destructors and free memory.
    // Display items are taken out under the lock and destroyed outside it.
    std::vector<std::unique_ptr<DisplayItem>> oldItems;
    {
        const ItemsLock lock;
        oldItems.swap(displayItems);
    }
    oldItems.clear();
    buttons.clear();
    clocks.clear();

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

    // Reserve capacity for efficiency
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
            displayItems.emplace_back(std::move(item));
        } else {
            ESP_LOGW(TAG, "Failed to create DisplayItem from JSON");
        }
    }

    ESP_LOGI(TAG, "Successfully loaded %zu display items", getDisplayItemCount());
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

} // namespace freebutton