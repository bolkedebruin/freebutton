//
// ConfigManager - Efficient management of multiple buttons and display items
// Created by Bolke de Bruin on 19/11/2025.
//

#ifndef FREEBUTTON_COMPONENTMANAGER_H
#define FREEBUTTON_COMPONENTMANAGER_H

#include <vector>
#include <memory>
#include <functional>
#include "DisplayItem.h"
#include "Button.h"
#include "Clock.h"
#include "mqtt/Registry.h"
#include "json/JsonWriter.h"
#include "config.h"
#include "drivers/Sensors.h"
#include "core/HardwareInitializer.h"
#include <rapidjson/writer.h>

namespace freebutton {

/**
 * Manages collections of DisplayItems and Buttons with memory-efficient loading
 */
class ConfigManager {
public:
    ConfigManager() = default;
    ~ConfigManager() = default;

    // Non-copyable, movable
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = default;
    ConfigManager& operator=(ConfigManager&&) = default;

    /**
     * Load components from JSON file using streaming parser for memory efficiency
     * @param filename Path to JSON configuration file
     */
    void loadFromFile(const std::string& filename);

    /**
     * Clear all components and free memory
     */
    void clear();

    /**
     * Get number of display items
     */
    size_t getDisplayItemCount() const { return displayItems.size(); }

    /**
     * Get number of buttons
     */
    size_t getButtonCount() const { return buttons.size(); }

    /**
     * Get number of clocks
     */
    size_t getClockCount() const { return clocks.size(); }

    /**
     * Iterate over all display items
     * @param func Function to call for each DisplayItem
     */
    template<typename Func>
    void forEachDisplayItem(Func&& func) {
        for (auto& item : displayItems) {
            func(*item);
        }
    }

    /**
     * Iterate over all display items (const version)
     */
    template<typename Func>
    void forEachDisplayItem(Func&& func) const {
        for (const auto& item : displayItems) {
            func(*item);
        }
    }

    /**
     * Iterate over all buttons
     * @param func Function to call for each Button
     */
    template<typename Func>
    void forEachButton(Func&& func) {
        for (auto& button : buttons) {
            func(*button);
        }
    }

    /**
     * Iterate over all buttons (const version)
     */
    template<typename Func>
    void forEachButton(Func&& func) const {
        for (const auto& button : buttons) {
            func(*button);
        }
    }

    /**
     * Iterate over all clocks
     * @param func Function to call for each Clock
     */
    template<typename Func>
    void forEachClock(Func&& func) {
        for (auto& clock : clocks) {
            func(*clock);
        }
    }

    /**
     * Iterate over all clocks (const version)
     */
    template<typename Func>
    void forEachClock(Func&& func) const {
        for (const auto& clock : clocks) {
            func(*clock);
        }
    }

    /**
     * Get display item by index
     * @param index Index of display item
     * @return Reference to DisplayItem or nullptr if index invalid
     */
    DisplayItem* getDisplayItem(const size_t index) const {
        return index < displayItems.size() ? displayItems[index].get() : nullptr;
    }

    /**
     * Get button by index
     * @param index Index of button
     * @return Reference to Button or nullptr if index invalid
     */
    Button* getButton(const size_t index) const {
        return index < buttons.size() ? buttons[index].get() : nullptr;
    }

    /**
     * Get button by position
     * @param position Position of button
     * @return Reference to Button or nullptr if position not found
     */
    Button* getButtonByPosition(const int position) const;

    /**
     * Get clock by index
     * @param index Index of clock
     * @return Reference to Clock or nullptr if index invalid
     */
    Clock* getClock(const size_t index) const {
        return index < clocks.size() ? clocks[index].get() : nullptr;
    }

    /**
     * Find display items by page
     * @param page Page number to filter by
     * @return Vector of DisplayItem pointers on the specified page
     */
    std::vector<DisplayItem*> getDisplayItemsByPage(int page) const;

    /**
     * Find clocks by page
     * @param page Page number to filter by
     * @return Vector of Clock pointers on the specified page
     */
    std::vector<Clock*> getClocksByPage(int page) const;

    /**
     * Find display item by ID
     * @param id ID of display item
     * @return Pointer to DisplayItem or nullptr if not found
     */
    DisplayItem* findDisplayItemById(unsigned long id) const;

    /**
     * Add a new display item dynamically
     * @return Pointer to the created DisplayItem, or nullptr on failure
     */
    DisplayItem* addDisplayItem(std::unique_ptr<DisplayItem> item);

    /**
     * Remove a display item by ID
     * @param id ID of display item to remove
     * @return true if removed, false if not found
     */
    bool removeDisplayItem(unsigned long id);

    /**
     * Get memory usage statistics
     */
    struct MemoryStats {
        size_t displayItemCount;
        size_t buttonCount;
        size_t clockCount;
        size_t estimatedMemoryBytes;
    };

    MemoryStats getMemoryStats() const;

    /**
     * Register all component subscriptions with their brokers
     * Call this after brokers are fully configured
     */
    void registerAllSubscriptions();

    /**
     * Start all brokers and configure Home Assistant auto-discovery
     * Call this after brokers are configured and subscriptions registered
     */
    void startBrokers(const std::string& deviceId);

    /**
     * Get NTP server from configuration
     */
    const std::string& getNtpServer() const { return ntpServer; }

    /**
     * Get timezone from configuration
     */
    const std::string& getTimezone() const { return timezone; }

    /**
     * Write configuration to an output stream as JSON
     * @param stream Output stream to write to (e.g., AsyncJsonOutputStream, FileOutputStream)
     */
    template<class OutputStream>
    void write(OutputStream &stream);

private:
    static constexpr auto TAG = "ConfigManager";

    std::vector<std::unique_ptr<DisplayItem>> displayItems;
    std::vector<std::unique_ptr<Button>> buttons;
    std::vector<std::unique_ptr<Clock>> clocks;

    std::string ntpServer;
    std::string timezone;

    /**
     * Load display items from JSON array
     */
    void loadDisplayItems(const JsonArray& itemsArray);

    /**
     * Load buttons from JSON array
     */
    void loadButtons(const JsonArray& buttonsArray);

    /**
     * Load MQTT brokers from JSON array
     */
    void loadBrokers(const JsonArray& brokersArray);

    /**
     * Load clocks from JSON array
     */
    void loadClocks(const JsonArray& clocksArray);

    /**
     * Filter callback for processing core configuration
     */
    void processCoreFilter(const std::vector<std::string>& path, const std::string& json);

    /**
     * Filter callback for processing MQTT brokers
     */
    void processMqttBrokersFilter(const std::vector<std::string>& path, const std::string& json);

    /**
     * Filter callback for processing display items
     */
    void processDisplayItemsFilter(const std::vector<std::string>& path, const std::string& json);

    /**
     * Filter callback for processing buttons
     */
    void processButtonsFilter(const std::vector<std::string>& path, const std::string& json);

    /**
     * Filter callback for processing clocks
     */
    void processClocksFilter(const std::vector<std::string>& path, const std::string& json);
};

// Template method implementation must be in header
template<typename OutputStream>
void ConfigManager::write(OutputStream &stream) {
    rapidjson::Writer<OutputStream> writer(stream);
    json::JsonWriter<OutputStream> jsonWriter(writer);

    auto doc = jsonWriter.Object();

    // info
    {
        auto info = doc.Object("info");
        info.Add("id", deviceId);
        info.Add("firmware", FIRMWARE);

        // connectors - hardware detected connectors
        {
            auto connectors = info.Array("connectors");
            if (FreeButton::g_hardwareInitializer) {
                const auto& scanner = FreeButton::g_hardwareInitializer->getScanner();
                for (const auto& [connectorNum, connectorType] : scanner.getConnectors()) {
                    if (connectorType == FreeButton::ConnectorType::EMPTY) continue;

                    auto conn = connectors.Object();
                    conn.Add("id", connectorNum);
                    conn.Add("connector", connectorNum + 1); // backwards compatibility
                    conn.Add("type", static_cast<uint8_t>(connectorType));
                }
            }
        }

        // sensors info
        {
            auto sensors = info.Array("sensors");
            for (auto& [id, dev] : FreeButton::sensorDevices) {
                auto s = sensors.Object();
                s.Add("sensorid", id);
                s.Add("description", dev->description);
            }
        }
    }

    // core
    {
        auto core = doc.Object("core");
        core.Add("ntpserver", ntpServer);
        core.Add("timezone", timezone);
    }

    // buttons array
    {
        auto btns = doc.Array("buttons");
        for (auto& button : buttons) {
            auto jbutton = btns.Object();
            button->toJson<OutputStream>(jbutton);
        }
    }

    // display items array
    {
        auto items = doc.Array("displayitems");
        for (auto& item : displayItems) {
            auto jitem = items.Object();
            item->toJson<OutputStream>(jitem);
        }
    }

    // clocks array
    {
        auto clocksArr = doc.Array("clocks");
        for (auto& clock : clocks) {
            auto jclock = clocksArr.Object();
            clock->toJson<OutputStream>(jclock);
        }
    }

    // brokers array
    {
        auto brkrs = doc.Array("brokers");
        MqttBrokerRegistry::forEachBroker([&brkrs](uint8_t broker_id, MqttBroker* broker) {
            auto jbroker = brkrs.Object();
            broker->toJson<OutputStream>(jbroker);
        });
    }

    // sensors configuration array
    {
        auto sensors = doc.Array("sensors");
        for (const auto& [sensorId, device] : FreeButton::sensorDevices) {
            // Only include physical sensors (not virtual ones)
            if (device->type != FreeButton::SensorType::VIRTUAL) {
                auto s = sensors.Object();
                s.Add("sensorid", sensorId);
                s.Add("interval", 10); // Default interval

                // topics array (empty for now - will be populated when sensor topic system is implemented)
                auto topics = s.Array("topics");
            }
        }
    }
}

} // namespace freebutton

#endif //FREEBUTTON_COMPONENTMANAGER_H