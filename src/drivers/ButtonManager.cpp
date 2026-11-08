//
// Created by Bolke de Bruin on 25/11/2025.
//

#include "ButtonManager.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <algorithm>
#include <map>

#include "mqtt/HomeAssistantDiscovery.h"
#include "mqtt/HaEntities.h"
#include "mqtt/Registry.h"
#include "components/ButtonEventPlan.h"
#include "components/ConfigManager.h"
#include "scripting/button_hardware.h"

namespace ha = freebutton::ha;

namespace FreeButton {

ButtonManager::ButtonManager()
    : taskHandle_(nullptr)
    , mcpMutex_(xSemaphoreCreateMutex())
    , updateInterval_(DEFAULT_UPDATE_INTERVAL)
    , monitoring_(false) {
}

void ButtonManager::addButton(std::unique_ptr<PushButton> button) {
    if (!button) {
        ESP_LOGW(TAG, "Attempting to add null button");
        return;
    }

    const int position = button->position();
    ESP_LOGI(TAG, "Adding push button at position %d", position);

    // Set up button event callback to forward to display components
    button->onButtonEvent([this](const int pos, const EventType event) {
        ESP_LOGI(TAG, "Button at position %d with event %d", pos, event);

        // Dispatch to JavaScript callbacks first. A handler returning `false`
        // cancels the default action: no MQTT publishing for this press.
        bool defaultPrevented = false;
        switch (event) {
            case CLICK:
                defaultPrevented = button_hw_dispatch_click(pos);
                break;
            case LONG_PRESS:
                defaultPrevented = button_hw_dispatch_long_press(pos);
                break;
            case RELEASE:
                defaultPrevented = button_hw_dispatch_release(pos);
                break;
            default:
                break;
        }

        if (defaultPrevented) {
            ESP_LOGI(TAG, "Button %d event %d: default prevented by script, not publishing", pos, event);
            return;
        }

        // Find and trigger corresponding display button
        auto* displayButton = configManager.getButtonByPosition(pos);
        if (displayButton) {
            ESP_LOGI(TAG, "Found display button for position %d, triggering event", pos);
            displayButton->handlePhysicalEvent(event);
        } else {
            ESP_LOGI(TAG, "No display button found for position %d", pos);
        }

        // Home Assistant event entity: make sure the canonical topic carries
        // the press, for configured and unconfigured keys alike.
        mirrorToCanonicalTopic(pos, event);
    });

    buttons_.push_back(std::move(button));
}

void ButtonManager::removeButton(int position) {
    auto it = std::ranges::find_if(buttons_,
                                   [position](const auto& button) {
                                       return button->position() == position;
                                   });

    if (it != buttons_.end()) {
        ESP_LOGI(TAG, "Removing button at position %d", position);
        buttons_.erase(it);
    } else {
        ESP_LOGW(TAG, "Button at position %d not found for removal", position);
    }
}

PushButton* ButtonManager::getButton(int position) const {
    const auto it = std::ranges::find_if(buttons_,
                                   [position](const auto& button) {
                                       return button->position() == position;
                                   });

    return it != buttons_.end() ? it->get() : nullptr;
}

namespace {

// How a key position's presses reach the canonical topic. The config only
// refines this (user topics already covering it, extra event types); a key
// without a config Button still publishes click/longpress there.
//
// Reads ConfigManager's buttons and their topics WITHOUT synchronisation
// (from the button task, loop() for discovery, ...). That is only valid
// because the configuration is immutable after boot; a runtime config reload
// would need a lock here.
freebutton::ha::ButtonEventPlan planForPosition(const int pos, const uint8_t haBrokerId) {
    namespace ha = freebutton::ha;
    if (const auto* button = configManager.getButtonByPosition(pos)) {
        return button->homeAssistantPlan(haBrokerId);
    }
    char canonical[96];
    const size_t length = ha::canonicalButtonStateTopic(canonical, sizeof(canonical), deviceId, pos);
    if (length == 0) {
        return ha::unannouncedPlan("canonical press topic does not fit its buffer");
    }
    return ha::planButtonEvents({}, haBrokerId, std::string_view(canonical, length));
}

} // namespace

void ButtonManager::mirrorToCanonicalTopic(const int pos, const EventType event) {
    namespace ha = freebutton::ha;
    if (event != CLICK && event != LONG_PRESS) {
        return;
    }
    char topic[96];
    const size_t topicLength = ha::canonicalButtonStateTopic(topic, sizeof(topic), deviceId, pos);
    if (topicLength == 0) {
        return;
    }
    MqttBrokerRegistry::forEachBroker([&](const uint8_t brokerId, const MqttBroker* broker) {
        if (!broker || !HomeAssistantDiscovery::isDiscoveryEnabled(brokerId)) {
            return;
        }
        const auto plan = planForPosition(pos, brokerId);
        if (!(event == CLICK ? plan.mirrorClick : plan.mirrorLongPress)) {
            return;
        }
        // Not retained: Home Assistant ignores retained events, and a retained
        // press would replay after a reconnect.
        // Zero-copy: topic is NUL-terminated in this frame, the payload is a
        // string literal (CLICK_PAYLOAD / LONG_PRESS_PAYLOAD).
        const std::string_view payload = ha::defaultPayloadFor(event);
        broker->publish(topic, payload.data(), payload.size(), 0, 0);
    });
}

// Entries per key position: event, button, click trigger, long-press trigger.
static constexpr size_t kEntriesPerPosition = 4;

size_t ButtonManager::discoveryCount() const {
    // Every position, not just the physical ones: positions without a
    // physical button publish empty payloads, clearing configs left behind
    // by earlier firmware or a different module layout.
    return BUTTON_COUNT * kEntriesPerPosition + leds_.size();
}

bool ButtonManager::writeDiscovery(const size_t index, const uint8_t brokerId, ha::DiscoveryOut& out) const {
    if (index >= BUTTON_COUNT * kEntriesPerPosition) {
        const size_t led = index - BUTTON_COUNT * kEntriesPerPosition;
        if (led >= leds_.size()) {
            return false;
        }
        leds_[led]->writeDiscovery(out);
        return true;
    }

    const int pos = static_cast<int>(index / kEntriesPerPosition) + 1;
    const bool present = getButton(pos) != nullptr;
    switch (index % kEntriesPerPosition) {
        case 0: {
            // MQTT event: physical presses (click / long press / ...)
            const auto plan = planForPosition(pos, brokerId);
            if (present && !plan.announceEvent) {
                ESP_LOGW(TAG, "Key %d: not announcing HA event entity: %s", pos, plan.problem.c_str());
            }
            std::string_view types[ha::EventTypeList::kMaxTypes];
            size_t count = 0;
            for (const auto type : plan.eventTypes) {
                types[count++] = type;
            }
            ha::writeButtonEvent(out, pos, present && plan.announceEvent, types, count);
            return true;
        }
        case 1:
            // MQTT button: a press sent from Home Assistant acts as a short press
            ha::writeButtonCommand(out, pos, present);
            return true;
        default: {
            // Device triggers, kept for existing automations. They match payloads
            // on the canonical topic, which carries them for every announced key.
            const bool longPress = index % kEntriesPerPosition == 3;
            const bool exists = present && planForPosition(pos, brokerId).announceEvent;
            ha::writeButtonTrigger(out, pos, longPress, exists);
            return true;
        }
    }
}

void ButtonManager::subscribeHomeAssistantCommands(const uint8_t brokerId) const {
    if (!HomeAssistantDiscovery::isDiscoveryEnabled(brokerId)) {
        return;
    }
    for (int pos = 1; pos <= BUTTON_COUNT; ++pos) {
        if (getButton(pos)) {
            commandSubscriber_->addSubscription(brokerId, freebutton::ha::buttonCommandTopic(deviceId, pos));
        }
    }
}

// Topic buttonplus/<deviceId>/button_<pos>/set (ha::buttonCommandTopic):
// the position is parsed from it, no strings are built per command.
void HaButtonCommandSubscriber::onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) {
    constexpr std::string_view kRoot = PUBLICMQTTID "/";
    constexpr std::string_view kButton = "/button_";
    constexpr std::string_view kSet = "/set";
    if (!topic.starts_with(kRoot)) return;
    topic.remove_prefix(kRoot.size());
    if (!topic.starts_with(deviceId)) return;
    topic.remove_prefix(deviceId.size());
    if (!topic.starts_with(kButton) || !topic.ends_with(kSet)) return;
    topic.remove_prefix(kButton.size());
    topic.remove_suffix(kSet.size());
    // Exactly the decimal digits std::to_string writes: no sign, no leading 0.
    if (topic.empty() || topic.size() > 2 || topic.front() == '0') return;
    int pos = 0;
    for (const char c : topic) {
        if (c < '0' || c > '9') return;
        pos = pos * 10 + (c - '0');
    }
    if (pos >= 1 && pos <= BUTTON_COUNT) {
        pending_.fetch_or(1u << (pos - 1));
    }
}

void ButtonManager::processHomeAssistantCommands() const {
    const uint32_t pending = commandSubscriber_->takePending();
    if (!pending) {
        return;
    }
    for (int pos = 1; pos <= BUTTON_COUNT; ++pos) {
        if (pending & (1u << (pos - 1))) {
            if (const auto* button = getButton(pos)) {
                ESP_LOGI(TAG, "Home Assistant press on button %d", pos);
                // Same path as a short physical press: down (click), then release.
                button->injectEvent(CLICK);
                button->injectEvent(RELEASE);
            }
        }
    }
}

void ButtonManager::startMonitoring() {
    if (monitoring_) {
        ESP_LOGW(TAG, "Button monitoring already started");
        return;
    }

    ESP_LOGI(TAG, "Starting button monitoring task");

    monitoring_ = true;

    if (xTaskCreate(buttonTaskWrapper, "button_monitor", 4096, this, 5, &taskHandle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create button monitoring task");
        monitoring_ = false;
        return;
    }
}

void ButtonManager::stopMonitoring() {
    if (!monitoring_) {
        return;
    }

    ESP_LOGI(TAG, "Stopping button monitoring");
    monitoring_ = false;

    if (taskHandle_) {
        vTaskDelete(taskHandle_);
        taskHandle_ = nullptr;
    }
}

void ButtonManager::buttonTaskWrapper(void* parameter) {
    if (auto* manager = static_cast<ButtonManager*>(parameter)) {
        manager->buttonTask();
    }
    vTaskDelete(nullptr);
}

/* do NOT make this const */
void ButtonManager::buttonTask() {
    ESP_LOGI(TAG, "Button monitoring task started with %zu buttons", buttons_.size());

    while (monitoring_) {
        updateAllButtons();
        processHomeAssistantCommands();
        vTaskDelay(pdMS_TO_TICKS(updateInterval_));
    }

    ESP_LOGI(TAG, "Button monitoring task ended");
}

void ButtonManager::updateAllButtons() const {
    if (buttons_.empty()) {
        return;
    }

    // Take mutex to prevent concurrent MCP access
    if (xSemaphoreTake(mcpMutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take MCP mutex, skipping button update");
        return;
    }

    // Group buttons by MCP to minimize I2C traffic
    std::map<std::shared_ptr<Adafruit_MCP23X08>, std::vector<PushButton*>> mcpButtons;

    for (const auto& button : buttons_) {
        auto mcp = button->getMCP();
        if (mcp) {
            mcpButtons[mcp].push_back(button.get());
        }
    }

    // Update all buttons grouped by MCP
    for (const auto& [mcp, buttonGroup] : mcpButtons) {
        if (mcp) {
            // Read GPIO state once per MCP
            const uint8_t gpioState = mcp->readGPIO();

            // Update all buttons on this MCP
            for (auto* button : buttonGroup) {
                button->updateState(gpioState);
                button->processEvents();
            }
        }
    }

    xSemaphoreGive(mcpMutex_);
}

void ButtonManager::createLEDsForAllButtons(const std::string& deviceId, uint8_t brokerId,
                                            const uint8_t connectorTypes[4]) {
    ESP_LOGI(TAG, "Creating LEDs for detected bar buttons");

    // Precompute each connector's bar index = number of BAR connectors before it.
    // Only BAR (type 1) connectors carry LEDs; main (LARGE) / empty carry none.
    int barIndexOf[4] = {0, 0, 0, 0};
    int barCount = 0;
    for (int c = 0; c < 4; ++c) {
        barIndexOf[c] = barCount;
        if (connectorTypes[c] == 1) ++barCount;  // BAR
    }

    for (const auto& button : buttons_) {
        const int position  = button->position();   // 0-based: connector*2 + side
        const int connector = position / 2;
        const int isRight   = position % 2;          // 0=left, 1=right

        if (connector < 0 || connector >= 4 || connectorTypes[connector] != 1) {
            ESP_LOGD(TAG, "Button %d on connector %d has no LEDs (not a bar) — skipping",
                     position, connector);
            continue;
        }

        const int base       = barIndexOf[connector] * 4;
        const int frontIndex = base + (isRight ? 2 : 0);  // front LED
        const int wallIndex  = frontIndex + 1;            // back/wall LED
        createLEDsForButton(position, frontIndex, wallIndex, deviceId, brokerId);
    }

    ESP_LOGI(TAG, "Created %zu LEDs total for %zu buttons", leds_.size(), buttons_.size());
}

void ButtonManager::createLEDsForButton(int position, int frontIndex, int wallIndex,
                                        const std::string& deviceId, uint8_t brokerId) {
    ESP_LOGD(TAG, "Button %d LEDs: front=%d wall=%d", position, frontIndex, wallIndex);

    leds_.push_back(std::make_unique<ButtonLed>(
        position, LedPosition::FRONT, 0, frontIndex, deviceId, brokerId));
    leds_.push_back(std::make_unique<ButtonLed>(
        position, LedPosition::WALL, 1, wallIndex, deviceId, brokerId));
}

std::vector<ButtonLed*> ButtonManager::getLEDsForButton(const int position) const {
    std::vector<ButtonLed*> buttonLeds;

    for (const auto& led : leds_) {
        if (led->getButtonPosition() == position) {
            buttonLeds.push_back(led.get());
        }
    }

    return buttonLeds;
}

uint8_t ButtonManager::getBaseButtonPosition() const {
    uint8_t pos = 255;
    for (const auto& button : buttons_) {
        if (button->position() < pos) {
            pos = button->position();
        }
    }
    return pos;
}
} // namespace FreeButton