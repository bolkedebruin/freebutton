//
// ComponentManager - Efficient management of multiple buttons and display items
// Created by Bolke de Bruin on 19/11/2025.
//

#ifndef FREEBUTTON_COMPONENTMANAGER_H
#define FREEBUTTON_COMPONENTMANAGER_H

#include <vector>
#include <memory>
#include <functional>
#include "DisplayItem.h"
#include "Button.h"
#include "mqtt/Registry.h"

namespace freebutton {

/**
 * Manages collections of DisplayItems and Buttons with memory-efficient loading
 */
class ComponentManager {
public:
    ComponentManager() = default;
    ~ComponentManager() = default;

    // Non-copyable, movable
    ComponentManager(const ComponentManager&) = delete;
    ComponentManager& operator=(const ComponentManager&) = delete;
    ComponentManager(ComponentManager&&) = default;
    ComponentManager& operator=(ComponentManager&&) = default;

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
     * Find display items by page
     * @param page Page number to filter by
     * @return Vector of DisplayItem pointers on the specified page
     */
    std::vector<DisplayItem*> getDisplayItemsByPage(int page) const;

    /**
     * Get memory usage statistics
     */
    struct MemoryStats {
        size_t displayItemCount;
        size_t buttonCount;
        size_t estimatedMemoryBytes;
    };

    MemoryStats getMemoryStats() const;

private:
    static constexpr auto TAG = "ComponentManager";

    std::vector<std::unique_ptr<DisplayItem>> displayItems;
    std::vector<std::unique_ptr<Button>> buttons;

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
     * Register all component subscriptions with their brokers
     * Call this after brokers are fully configured
     */
    void registerAllSubscriptions();
};

} // namespace freebutton

#endif //FREEBUTTON_COMPONENTMANAGER_H