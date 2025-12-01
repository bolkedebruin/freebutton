//
// ComponentManager - Efficient management of multiple buttons and display items
// Created by Bolke de Bruin on 19/11/2025.
//

#include "ComponentManager.h"
#include "json/FilteredReaderHandler.h"
#include <esp_log.h>
#include "LittleFS.h"
#include "drivers/BusAwareFileSystem.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include "Clock.h"

namespace freebutton {

void ComponentManager::loadFromFile(const std::string& filename) {
    ESP_LOGI(TAG, "Loading components from file: %s", filename.c_str());

    // Clear existing components
    clear();

    // Use FilteredReaderHandler for memory-efficient streaming processing
    json::FilteredReaderHandler handler;

    handler.addFilter({"core"}, [this](const std::vector<std::string>& path, const std::string& json) {
        JsonDocument doc;
        auto error= deserializeJson(doc, json);

        if (error) {
            ESP_LOGE(TAG, "Failed to parse core JSON: %s", error.c_str());
            return;
        }

        if (!doc["location"].isNull()) {
            HomeAssistantDiscovery::setDeviceName(doc["location"].as<std::string>());
        }

        if (!doc["ntpserver"].isNull()) {
            ntpServer = doc["ntpserver"].as<std::string>();
        }

        if (!doc["timezone"].isNull()) {
            timezone = doc["timezone"].as<std::string>();
        }
    });

    // Stream process MQTT brokers FIRST (order matters!)
    handler.addFilter({"mqttbrokers"}, [this](const std::vector<std::string>& path, const std::string& json) {
        JsonDocument doc;
        auto error = deserializeJson(doc, json);

        if (error) {
            ESP_LOGE(TAG, "Failed to parse brokers JSON: %s", error.c_str());
            return;
        }

        if (doc.is<JsonArray>()) {
            loadBrokers(doc.as<JsonArray>());
        }
    });

    // Stream process display items (after brokers)
    handler.addFilter({"mqttdisplays"}, [this](const std::vector<std::string>& path, const std::string& json) {
        JsonDocument doc;
        auto error = deserializeJson(doc, json);

        if (error) {
            ESP_LOGE(TAG, "Failed to parse displayitems JSON: %s", error.c_str());
            return;
        }

        if (doc.is<JsonArray>()) {
            loadDisplayItems(doc.as<JsonArray>());
        }
    });

    // Stream process buttons (after brokers)
    handler.addFilter({"mqttbuttons"}, [this](const std::vector<std::string>& path, const std::string& json) {
        JsonDocument doc;
        auto error = deserializeJson(doc, json);

        if (error) {
            ESP_LOGE(TAG, "Failed to parse buttons JSON: %s", error.c_str());
            return;
        }

        if (doc.is<JsonArray>()) {
            loadButtons(doc.as<JsonArray>());
        }
    });

    // Stream process clocks (no MQTT dependencies)
    handler.addFilter({"clocks"}, [this](const std::vector<std::string>& path, const std::string& json) {
        JsonDocument doc;
        auto error = deserializeJson(doc, json);

        if (error) {
            ESP_LOGE(TAG, "Failed to parse clocks JSON: %s", error.c_str());
            return;
        }

        if (doc.is<JsonArray>()) {
            loadClocks(doc.as<JsonArray>());
        }
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
             displayItems.size(), buttons.size(), clocks.size());
}

void ComponentManager::clear() {
    ESP_LOGI(TAG, "Clearing all components");

    // Clear vectors - this will automatically call destructors and free memory
    displayItems.clear();
    buttons.clear();
    clocks.clear();

    // Shrink capacity to free memory
    displayItems.shrink_to_fit();
    buttons.shrink_to_fit();
    clocks.shrink_to_fit();
}

std::vector<DisplayItem*> ComponentManager::getDisplayItemsByPage(const int page) const {
    std::vector<DisplayItem*> pageItems;

    for (auto& item : displayItems) {
        if (item->page == page) {
            pageItems.push_back(item.get());
        }
    }

    return pageItems;
}

std::vector<Clock*> ComponentManager::getClocksByPage(const int page) const {
    std::vector<Clock*> pageClocks;

    for (auto& clock : clocks) {
        if (clock->getPage() == page) {
            pageClocks.push_back(clock.get());
        }
    }

    return pageClocks;
}

ComponentManager::MemoryStats ComponentManager::getMemoryStats() const {
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

void ComponentManager::loadClocks(const JsonArray& clocksArray) {
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

void ComponentManager::loadBrokers(const ArduinoJson::JsonArray& brokersArray) {
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

        bool autoDiscovery = brokerObj["autoDiscovery"] | true;

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

void ComponentManager::registerAllSubscriptions() {
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

void ComponentManager::startBrokers(const std::string& deviceId) {
    ESP_LOGI(TAG, "Starting all brokers and configuring Home Assistant discovery");

    auto& registry = MqttBrokerRegistry::instance();

    // Configure each broker with Home Assistant discovery
    MqttBrokerRegistry::forEachBroker([](uint8_t brokerId, MqttBroker* broker) {
        if (broker && broker->isConfigured()) {
            // Set up connection callback for HomeAssistant discovery
            broker->setOnConnectedCallback([brokerId](uint8_t connectedBrokerId) {
                ESP_LOGI(TAG, "Broker %d connected, triggering Home Assistant discovery", connectedBrokerId);
                HomeAssistantDiscovery::onBrokerConnected(connectedBrokerId);
            });

            // Configure Home Assistant discovery for this broker
            HomeAssistantDiscovery::configureBroker(brokerId);

            ESP_LOGI(TAG, "Configured broker %d: %.*s for Home Assistant discovery", brokerId,
                    (int)broker->getName().length(), broker->getName().data());
        } else {
            ESP_LOGW(TAG, "Skipping unconfigured broker at ID %d", brokerId);
        }
    });

    // Start all configured brokers
    registry.connectAll();

    ESP_LOGI(TAG, "All brokers started and Home Assistant discovery configured");
}

} // namespace freebutton