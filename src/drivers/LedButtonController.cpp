//
// LedButtonController - Integrates LedButton with NeoPixel LEDs and MQTT control
// Created by Bolke de Bruin on 31/08/2025.
//

#include "LedButtonController.h"
#include "config.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include <esp_log.h>
#include <ArduinoJson.h>

namespace ButtonPlus {

LedButtonController::LedButtonController(std::shared_ptr<LedButton> button, 
                                       std::shared_ptr<NeoPixelController> neopixel,
                                       uint8_t brokerId,
                                       const std::string& deviceId)
    : button_(std::move(button))
    , neopixel_(std::move(neopixel))
    , brokerId_(brokerId)
    , deviceId_(deviceId) {
    
    if (!button_ || !neopixel_) {
        ESP_LOGE(TAG, "Invalid button or neopixel controller");
        return;
    }

    ESP_LOGI(TAG, "Created LedButtonController for button %d", getPosition());
    
    setupMqttTopics();
    updateLedDisplay();  // Initialize LED to default state
}

const std::string LedButtonController::getComponentType() const {
    return "LedButtonController";
}

void LedButtonController::setupMqttTopics() {
    const std::string buttonId = std::to_string(getPosition());
    const std::string baseTopic = std::string(BASE_TOPIC) + deviceId_ + "/button" + buttonId;
    
    // LED state topics
    stateTopic_ = baseTopic + "/led/state";
    commandTopic_ = baseTopic + "/led/set";
    
    // Brightness topics  
    brightnessStateTopic_ = baseTopic + "/led/brightness/state";
    brightnessCommandTopic_ = baseTopic + "/led/brightness/set";
    
    // Color topics
    colorStateTopic_ = baseTopic + "/led/color/state"; 
    colorCommandTopic_ = baseTopic + "/led/color/set";
    
    // Effect topics
    effectStateTopic_ = baseTopic + "/led/effect/state";
    effectCommandTopic_ = baseTopic + "/led/effect/set";

    // Register MQTT topics
    addPublication(brokerId_, stateTopic_, 0, true);
    addPublication(brokerId_, brightnessStateTopic_, 0, true);
    addPublication(brokerId_, colorStateTopic_, 0, true);
    addPublication(brokerId_, effectStateTopic_, 0, true);
    
    addSubscription(brokerId_, commandTopic_, 0);
    addSubscription(brokerId_, brightnessCommandTopic_, 0);
    addSubscription(brokerId_, colorCommandTopic_, 0);
    addSubscription(brokerId_, effectCommandTopic_, 0);

    ESP_LOGI(TAG, "Setup MQTT topics for button %d with base: %s", getPosition(), baseTopic.c_str());
}

void LedButtonController::onMessage(uint8_t brokerId, std::string_view topic, std::string_view data) {
    if (brokerId != brokerId_) {
        return;
    }

    const std::string topicStr(topic);
    const std::string dataStr(data);

    ESP_LOGD(TAG, "Button %d received MQTT message on topic: %s, data: %s", 
             getPosition(), topicStr.c_str(), dataStr.c_str());

    if (topicStr == commandTopic_) {
        JsonDocument json;
        if (deserializeJson(json, dataStr) == DeserializationError::Ok) {
            handleStateCommand(json);
        } else {
            ESP_LOGW(TAG, "Failed to parse JSON command for button %d", getPosition());
        }
    } else if (topicStr == brightnessCommandTopic_) {
        handleBrightnessCommand(dataStr);
    } else if (topicStr == colorCommandTopic_) {
        JsonDocument json;
        if (deserializeJson(json, dataStr) == DeserializationError::Ok) {
            handleColorCommand(json);
        } else {
            ESP_LOGW(TAG, "Failed to parse JSON color command for button %d", getPosition());
        }
    } else if (topicStr == effectCommandTopic_) {
        handleEffectCommand(dataStr);
    }
}

void LedButtonController::handleStateCommand(const JsonDocument& json) {
    bool stateChanged = false;

    if (json.containsKey("state")) {
        const std::string state = json["state"].as<std::string>();
        const bool newState = (state == "ON");
        if (newState != ledState_.on) {
            ledState_.on = newState;
            stateChanged = true;
            ESP_LOGI(TAG, "Button %d LED state set to: %s", getPosition(), state.c_str());
        }
    }

    if (json.containsKey("brightness")) {
        const uint8_t brightness = json["brightness"].as<uint8_t>();
        if (brightness != ledState_.brightness) {
            ledState_.brightness = brightness;
            stateChanged = true;
            ESP_LOGI(TAG, "Button %d LED brightness set to: %d", getPosition(), brightness);
        }
    }

    if (json.containsKey("color")) {
        const auto color = json["color"];
        if (color.containsKey("r") && color.containsKey("g") && color.containsKey("b")) {
            const uint8_t r = color["r"].as<uint8_t>();
            const uint8_t g = color["g"].as<uint8_t>();
            const uint8_t b = color["b"].as<uint8_t>();
            
            if (r != ledState_.red || g != ledState_.green || b != ledState_.blue) {
                ledState_.red = r;
                ledState_.green = g;
                ledState_.blue = b;
                stateChanged = true;
                ESP_LOGI(TAG, "Button %d LED color set to RGB(%d,%d,%d)", getPosition(), r, g, b);
            }
        }
    }

    if (json.containsKey("effect")) {
        const std::string effect = json["effect"].as<std::string>();
        if (effect != ledState_.effect) {
            ledState_.effect = effect;
            stateChanged = true;
            ESP_LOGI(TAG, "Button %d LED effect set to: %s", getPosition(), effect.c_str());
        }
    }

    if (stateChanged) {
        updateLedDisplay();
        publishState();
    }
}

void LedButtonController::handleBrightnessCommand(const std::string& data) {
    const int brightness_val = std::atoi(data.c_str());
    if (brightness_val >= 0 && brightness_val <= 255) {
        const uint8_t brightness = static_cast<uint8_t>(brightness_val);
        if (brightness != ledState_.brightness) {
            ledState_.brightness = brightness;
            updateLedDisplay();
            publishState();
            ESP_LOGI(TAG, "Button %d LED brightness set to: %d", getPosition(), brightness);
        }
    } else {
        ESP_LOGW(TAG, "Invalid brightness value for button %d: %s", getPosition(), data.c_str());
    }
}

void LedButtonController::handleColorCommand(const JsonDocument& json) {
    if (json.containsKey("r") && json.containsKey("g") && json.containsKey("b")) {
        const uint8_t r = json["r"].as<uint8_t>();
        const uint8_t g = json["g"].as<uint8_t>();
        const uint8_t b = json["b"].as<uint8_t>();
        
        if (r != ledState_.red || g != ledState_.green || b != ledState_.blue) {
            ledState_.red = r;
            ledState_.green = g;  
            ledState_.blue = b;
            updateLedDisplay();
            publishState();
            ESP_LOGI(TAG, "Button %d LED color set to RGB(%d,%d,%d)", getPosition(), r, g, b);
        }
    } else {
        ESP_LOGW(TAG, "Invalid color command format for button %d", getPosition());
    }
}

void LedButtonController::handleEffectCommand(const std::string& data) {
    if (data != ledState_.effect) {
        ledState_.effect = data;
        updateLedDisplay();
        publishState();
        ESP_LOGI(TAG, "Button %d LED effect set to: %s", getPosition(), data.c_str());
    }
}

void LedButtonController::setLedState(bool on) {
    if (ledState_.on != on) {
        ledState_.on = on;
        updateLedDisplay();
        publishState();
    }
}

void LedButtonController::setLedColor(uint8_t red, uint8_t green, uint8_t blue) {
    if (ledState_.red != red || ledState_.green != green || ledState_.blue != blue) {
        ledState_.red = red;
        ledState_.green = green;
        ledState_.blue = blue;
        updateLedDisplay();
        publishState();
    }
}

void LedButtonController::setLedBrightness(uint8_t brightness) {
    if (ledState_.brightness != brightness) {
        ledState_.brightness = brightness;
        updateLedDisplay();
        publishState();
    }
}

void LedButtonController::setLedEffect(const std::string& effect) {
    if (ledState_.effect != effect) {
        ledState_.effect = effect;
        updateLedDisplay();
        publishState();
    }
}

int LedButtonController::getLedIndex() const {
    // Calculate LED index based on button position using existing pixelIndex method
    const int row = button_->row();
    const bool isRight = button_->isRight();
    const bool isWall = false;  // Assume not wall-mounted for now
    
    return neopixel_->pixelIndex(row, isRight, isWall);
}

void LedButtonController::updateLedDisplay() {
    if (!neopixel_) {
        ESP_LOGW(TAG, "No neopixel controller available for button %d", getPosition());
        return;
    }

    const int ledIndex = getLedIndex();
    
    if (ledState_.on) {
        // Apply brightness scaling
        const uint8_t scaledRed = (ledState_.red * ledState_.brightness) / 255;
        const uint8_t scaledGreen = (ledState_.green * ledState_.brightness) / 255;
        const uint8_t scaledBlue = (ledState_.blue * ledState_.brightness) / 255;
        
        const CRGB color = neopixel_->colorFromRGB(scaledRed, scaledGreen, scaledBlue);
        neopixel_->setPixel(ledIndex, color);
        
        // Handle effects (basic implementation)
        if (ledState_.effect == "glow") {
            neopixel_->startEffect("glow");
        } else if (ledState_.effect == "flash") {
            neopixel_->startEffect("flash");
        } else if (ledState_.effect != "none") {
            ESP_LOGW(TAG, "Unknown effect '%s' for button %d", ledState_.effect.c_str(), getPosition());
        }
    } else {
        // Turn off LED
        neopixel_->setPixel(ledIndex, CRGB::Black);
        
        // Stop any running effects
        if (ledState_.effect != "none") {
            neopixel_->stopEffect();
        }
    }
    
    neopixel_->show();
    
    ESP_LOGD(TAG, "Updated LED display for button %d at index %d: %s RGB(%d,%d,%d) brightness=%d", 
             getPosition(), ledIndex, ledState_.on ? "ON" : "OFF", 
             ledState_.red, ledState_.green, ledState_.blue, ledState_.brightness);
}

void LedButtonController::publishState() {
    // Publish main state
    const std::string stateJson = stateToJson();
    publish(brokerId_, stateTopic_, stateJson, 0, true);
    
    // Publish brightness
    publish(brokerId_, brightnessStateTopic_, std::to_string(ledState_.brightness), 0, true);
    
    // Publish color
    const std::string colorJson = colorToJson();
    publish(brokerId_, colorStateTopic_, colorJson, 0, true);
    
    // Publish effect
    publish(brokerId_, effectStateTopic_, ledState_.effect, 0, true);
    
    ESP_LOGD(TAG, "Published state for button %d LED", getPosition());
}

std::string LedButtonController::stateToJson() const {
    JsonDocument json;
    json["state"] = ledState_.on ? "ON" : "OFF";
    json["brightness"] = ledState_.brightness;
    json["color"]["r"] = ledState_.red;
    json["color"]["g"] = ledState_.green;
    json["color"]["b"] = ledState_.blue;
    json["effect"] = ledState_.effect;
    
    std::string result;
    serializeJson(json, result);
    return result;
}

std::string LedButtonController::colorToJson() const {
    JsonDocument json;
    json["r"] = ledState_.red;
    json["g"] = ledState_.green;
    json["b"] = ledState_.blue;
    
    std::string result;
    serializeJson(json, result);
    return result;
}

void LedButtonController::registerWithHomeAssistant() const {
    if (!HomeAssistantDiscovery::isDiscoveryEnabled(brokerId_)) {
        ESP_LOGD(TAG, "Home Assistant discovery disabled for broker %d", brokerId_);
        return;
    }

    const std::string buttonId = std::to_string(getPosition());
    const std::string objectId = deviceId_ + "_button" + buttonId + "_led";
    const std::string name = "Button " + buttonId + " LED";

    // Create discovery configuration for the LED light entity
    HomeAssistantDiscoveryConfig config;
    config.component_type = "light";
    config.object_id = objectId;
    config.name = name;
    config.device_class = "";  // No specific device class for button LEDs
    
    // MQTT topics for Home Assistant
    config.state_topic = stateTopic_;
    config.command_topic = commandTopic_;
    
    // Additional configuration for Home Assistant light component
    config.additional_config["brightness_state_topic"] = brightnessStateTopic_;
    config.additional_config["brightness_command_topic"] = brightnessCommandTopic_;
    config.additional_config["brightness_scale"] = "255";
    
    config.additional_config["rgb_state_topic"] = colorStateTopic_;
    config.additional_config["rgb_command_topic"] = colorCommandTopic_;
    config.additional_config["rgb_command_template"] = "{{ {\"r\": red, \"g\": green, \"b\": blue} | tojson }}";
    config.additional_config["rgb_value_template"] = "{{ value_json.r }},{{ value_json.g }},{{ value_json.b }}";
    
    config.additional_config["effect_state_topic"] = effectStateTopic_;
    config.additional_config["effect_command_topic"] = effectCommandTopic_;
    config.additional_config["effect_list"] = "[\"none\",\"glow\",\"flash\"]";
    
    config.additional_config["schema"] = "json";
    config.additional_config["retain"] = "true";
    
    // Register with Home Assistant discovery
    HomeAssistantDiscovery::registerComponent(brokerId_, const_cast<LedButtonController*>(this), config.component_type, config.object_id, config.name);
    
    ESP_LOGI(TAG, "Registered button %d LED with Home Assistant as: %s", getPosition(), name.c_str());
}

} // namespace ButtonPlus