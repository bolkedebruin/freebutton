//
// ButtonLed - Implementation of lightweight LED controller
// Created by Claude Code on 25/11/2025.
//

#include "ButtonLed.h"
#include "NeoPixel.h"
#include "mqtt/HaEntities.h"
#include <atomic>
#include <cstdio>
#include "mqtt/Component.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include "config.h"

static auto TAG = "led";

ButtonLed* ButtonLed::first_ = nullptr;

namespace {
// Strip pixels a script wrote since the last restore, one bit per pixel.
std::atomic<uint16_t> g_scriptPixels{0};
static_assert(NeoPixelController::NUMBER_OF_LEDS <= 16, "one bit per LED in g_scriptPixels");
} // namespace

void ButtonLed::noteScriptPixel(const int ledIndex) noexcept {
    if (ledIndex >= 0 && ledIndex < NeoPixelController::NUMBER_OF_LEDS) {
        g_scriptPixels.fetch_or(static_cast<uint16_t>(1u << ledIndex), std::memory_order_acq_rel);
    }
}

void ButtonLed::restoreScriptPixels() noexcept {
    uint16_t pixels = g_scriptPixels.exchange(0, std::memory_order_acq_rel);
    if (pixels == 0) {
        return;  // no script touched an LED: nothing to write, no show()
    }
    for (const ButtonLed* led = first_; led != nullptr; led = led->next_) {
        const uint16_t bit = led->ledIndex_ >= 0 && led->ledIndex_ < NeoPixelController::NUMBER_OF_LEDS
                                 ? static_cast<uint16_t>(1u << led->ledIndex_) : 0;
        if (pixels & bit) {
            led->writePixel();
            pixels = static_cast<uint16_t>(pixels & ~bit);
        }
    }
    for (int i = 0; i < NeoPixelController::NUMBER_OF_LEDS; ++i) {
        if (pixels & (1u << i)) {
            neopixels.setPixel(i, CRGB::Black);
        }
    }
    NeoPixelController::show();
}

void ButtonLed::updateDisplay() const noexcept {
    writePixel();
    NeoPixelController::show();
}

void ButtonLed::writePixel() const noexcept {
    if (state_.on) {
        const uint8_t scaledR = (state_.red * state_.brightness) / 255;
        const uint8_t scaledG = (state_.green * state_.brightness) / 255;
        const uint8_t scaledB = (state_.blue * state_.brightness) / 255;

        neopixels.setPixel(ledIndex_,
                          NeoPixelController::colorFromRGB(scaledR, scaledG, scaledB));
    } else {
        neopixels.setPixel(ledIndex_, CRGB::Black);
    }
}

// Public methods for state retrieval (used by Button component for publishing)
std::string ButtonLed::getStateJson() const {
    return stateToJson();
}

std::string ButtonLed::getBrightnessString() const {
    return std::to_string(state_.brightness);
}

std::string ButtonLed::getColorJson() const {
    return colorToJson();
}

std::string ButtonLed::getEffectString() const {
    return state_.effect;
}

std::string ButtonLed::generateBaseTopic() const noexcept {
    const std::string buttonId = std::to_string(buttonPosition_);
    const std::string ledSuffix = (ledPosition_ == LedPosition::BOTH) ?
        ("_" + std::to_string(ledNumber_)) :
        ("_" + std::string(positionToString()));

    return std::string(PUBLICMQTTID) + "/" + deviceId_ + "/button" + buttonId + ledSuffix;
}

std::string ButtonLed::getStateTopic() const noexcept {
    return generateBaseTopic() + "/led/state";
}

std::string ButtonLed::getCommandTopic() const noexcept {
    return generateBaseTopic() + "/led/set";
}

std::string ButtonLed::getBrightnessStateTopic() const noexcept {
    return generateBaseTopic() + "/led/brightness/state";
}

std::string ButtonLed::getBrightnessCommandTopic() const noexcept {
    return generateBaseTopic() + "/led/brightness/set";
}

std::string ButtonLed::getColorStateTopic() const noexcept {
    return generateBaseTopic() + "/led/color/state";
}

std::string ButtonLed::getColorCommandTopic() const noexcept {
    return generateBaseTopic() + "/led/color/set";
}

std::string ButtonLed::getEffectStateTopic() const noexcept {
    return generateBaseTopic() + "/led/effect/state";
}

std::string ButtonLed::getEffectCommandTopic() const noexcept {
    return generateBaseTopic() + "/led/effect/set";
}

std::string ButtonLed::stateToJson() const {
    JsonDocument doc;
    doc["state"] = state_.on ? "ON" : "OFF";
    doc["brightness"] = state_.brightness;
    doc["color"]["r"] = state_.red;
    doc["color"]["g"] = state_.green;
    doc["color"]["b"] = state_.blue;
    doc["effect"] = state_.effect;

    std::string result;
    serializeJson(doc, result);
    return result;
}

std::string ButtonLed::colorToJson() const {
    JsonDocument doc;
    doc["r"] = state_.red;
    doc["g"] = state_.green;
    doc["b"] = state_.blue;

    std::string result;
    serializeJson(doc, result);
    return result;
}

void ButtonLed::writeDiscovery(freebutton::ha::DiscoveryOut& out) const {
    // Same root as generateBaseTopic(), built on the stack.
    char base[80];
    int n;
    if (ledPosition_ == LedPosition::BOTH) {
        n = std::snprintf(base, sizeof(base), PUBLICMQTTID "/%s/button%u_%u", deviceId_.c_str(),
                          static_cast<unsigned>(buttonPosition_), static_cast<unsigned>(ledNumber_));
    } else {
        const auto position = positionToString();
        n = std::snprintf(base, sizeof(base), PUBLICMQTTID "/%s/button%u_%.*s", deviceId_.c_str(),
                          static_cast<unsigned>(buttonPosition_), static_cast<int>(position.size()),
                          position.data());
    }
    if (n < 0 || static_cast<size_t>(n) >= sizeof(base)) {
        ESP_LOGE(TAG, "LED %u/%u topic root too long", buttonPosition_, ledNumber_);
        return;  // nothing written: no message
    }
    freebutton::ha::writeLight(out, buttonPosition_, ledNumber_, positionToString(),
                               std::string_view(base, static_cast<size_t>(n)));
}

bool ButtonLed::matchesTopic(std::string_view topic) const noexcept {
    const std::string baseTopic = generateBaseTopic();
    return topic.find(baseTopic) == 0;
}

void ButtonLed::handleMqttMessage(const std::string_view topic, std::string_view data) {
    const std::string topicStr(topic);

    if (topicStr == getCommandTopic()) {
        // Handle JSON state command
        JsonDocument doc;
        if (deserializeJson(doc, data) == DeserializationError::Ok) {
            if (!doc["state"].isNull()) {
                setState(doc["state"] == "ON");
            }
            if (!doc["brightness"].isNull()) {
                setBrightness(doc["brightness"].as<uint8_t>());
            }
            if (!doc["color"].isNull()) {
                setColor(doc["color"]["r"], doc["color"]["g"], doc["color"]["b"]);
            }
            if (!doc["effect"].isNull()) {
                setEffect(doc["effect"].as<std::string>());
            }
        }
    } else if (topicStr == getBrightnessCommandTopic()) {
        setBrightness(std::stoi(std::string(data)));
    } else if (topicStr == getColorCommandTopic()) {
        JsonDocument doc;
        if (deserializeJson(doc, data) == DeserializationError::Ok) {
            setColor(doc["r"], doc["g"], doc["b"]);
        }
    } else if (topicStr == getEffectCommandTopic()) {
        setEffect(std::string(data));
    }
}