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
#include "NameTable.h"
#include "List.h"
#include "ImageItem.h"
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
    size_t getDisplayItemCount() const {
        const ItemsLock lock;
        return displayItems.size();
    }

    /**
     * Get number of buttons
     */
    size_t getButtonCount() const { return buttons.size(); }

    /**
     * Get number of clocks
     */
    size_t getClockCount() const { return clocks.size(); }

    /**
     * Iterate over all display items, with the display-items lock held.
     * func must not add or remove display items.
     * @param func Function to call for each DisplayItem
     */
    template<typename Func>
    void forEachDisplayItem(Func&& func) {
        const ItemsLock lock;
        for (auto& item : displayItems) {
            func(*item);
        }
    }

    /**
     * Iterate over all display items (const version)
     */
    template<typename Func>
    void forEachDisplayItem(Func&& func) const {
        const ItemsLock lock;
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
     * @return Reference to DisplayItem or nullptr if index invalid. Display
     *         item pointers stay valid until the item is removed, which only
     *         script code does (JS engine lock).
     */
    DisplayItem* getDisplayItem(const size_t index) const {
        const ItemsLock lock;
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
     * Names from config.json ("name" on display items and keys) for the JS
     * bindings. Filled at boot while config.json loads, read-only afterwards.
     */
    const NameTable& names() const { return names_; }

    /**
     * Main-display list by its index in config.json "lists" (nullptr when
     * out of range). The lists are created at boot and never removed.
     */
    List* findList(size_t index) const { return index < lists.size() ? lists[index].get() : nullptr; }

    /**
     * Main-display image by its index in config.json "images" (nullptr when
     * out of range). Created at boot and never removed.
     */
    ImageItem* findImage(size_t index) const { return index < images.size() ? images[index].get() : nullptr; }

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

    // Guards the displayItems VECTOR: its storage and which items it holds.
    // Script code adds and removes items at run time (displayitem_binding,
    // loop or button task) while the config writer iterates it (/config,
    // async_tcp task); a push_back that reallocates, or an erase, under that
    // iteration was a use-after-free. It does not guard the items' fields.
    // Recursive, statically allocated, held only for vector operations and
    // iteration. Items are never destroyed under it (their destructor takes
    // the broker's subscription lock and the LVGL lock). The only lock taken
    // while holding it is a broker's subscriptionsLock_ (registerTopics() in
    // forEachDisplayItem at boot); nothing takes it while holding the LVGL
    // lock or a subscription lock.
    class ItemsLock {
    public:
        ItemsLock();
        ~ItemsLock();
        ItemsLock(const ItemsLock&) = delete;
        ItemsLock& operator=(const ItemsLock&) = delete;
    };

    std::vector<std::unique_ptr<DisplayItem>> displayItems;
    std::vector<std::unique_ptr<Button>> buttons;
    std::vector<std::unique_ptr<Clock>> clocks;
    std::vector<std::unique_ptr<List>> lists;
    std::vector<std::unique_ptr<ImageItem>> images;
    NameTable names_;

    // Adds config["name"] (if any) for kind / id; an invalid or duplicate
    // name is logged and ignored.
    void addName(NameTable::Kind kind, unsigned long id, const JsonObject& config);

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
     * Config "style" (docs/STYLE.md): a pass over the file of its own,
     * before the items are built (they resolve their font names at load and
     * build on the element styles), whatever the key order. Configures the
     * font registry with style.fonts (fonts_configure), then resolves the
     * roles and elements into the shared element styles (ui_style_apply);
     * LVGL lock taken here. Without "style": the system fonts and the
     * firmware defaults.
     */
    void loadStyle(const std::string& filename);

    /**
     * Load main-display lists from JSON array
     */
    void loadLists(const JsonArray& listsArray);
    void processListsFilter(const std::vector<std::string>& path, const std::string& json);

    /**
     * Load main-display images from JSON array
     */
    void loadImages(const JsonArray& imagesArray);
    void processImagesFilter(const std::vector<std::string>& path, const std::string& json);

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
// The regenerated config (GET /config) omits the IDE-level tables ("icons",
// "style" with its fonts; items still name their fonts): it is not a
// round-trip of config.json.
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
            const auto name = names_.nameOf(NameTable::Kind::Key, button->getPosition());
            if (!name.empty()) {
                jbutton.Add("name", name);
            }
        }
    }

    // display items array (script code may add / remove items meanwhile)
    {
        auto items = doc.Array("displayitems");
        const ItemsLock lock;
        for (auto& item : displayItems) {
            auto jitem = items.Object();
            item->toJson<OutputStream>(jitem);
            const auto name = names_.nameOf(NameTable::Kind::DisplayItem, item->id);
            if (!name.empty()) {
                jitem.Add("name", name);
            }
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

    // lists array (main-display rollers)
    {
        auto arr = doc.Array("lists");
        for (size_t i = 0; i < lists.size(); ++i) {
            auto jlist = arr.Object();
            const auto name = names_.nameOf(NameTable::Kind::List, static_cast<uint32_t>(i));
            if (!name.empty()) {
                jlist.Add("name", name);
            }
            lists[i]->toJson<OutputStream>(jlist);
        }
    }

    // images array (main-display rasters). The config's "icons" table
    // (SVG sources) is web IDE data the firmware does not keep.
    {
        auto arr = doc.Array("images");
        for (size_t i = 0; i < images.size(); ++i) {
            auto jimage = arr.Object();
            const auto name = names_.nameOf(NameTable::Kind::Image, static_cast<uint32_t>(i));
            if (!name.empty()) {
                jimage.Add("name", name);
            }
            images[i]->toJson<OutputStream>(jimage);
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