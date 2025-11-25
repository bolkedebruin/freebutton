//
// ButtonLed - Lightweight LED controller with Home Assistant integration
// Created by Claude Code on 25/11/2025.
//

#pragma once

#include <string>
#include <string_view>
#include <cstdint>

// LED Position enumeration for type safety
enum class LedPosition : uint8_t {
    FRONT = 0,
    WALL = 1,
    BOTH = 2  // For buttons with both front and wall LEDs
};

// Convert JSON string to enum
constexpr LedPosition ledPositionFromString(std::string_view str) noexcept {
    if (str == "front") return LedPosition::FRONT;
    if (str == "wall") return LedPosition::WALL;
    if (str == "both") return LedPosition::BOTH;
    return LedPosition::FRONT;  // Default
}

// Lightweight LED controller per button with Home Assistant integration
class ButtonLed {
private:
    const uint8_t buttonPosition_;
    const LedPosition ledPosition_;
    const uint8_t ledNumber_;      // LED index within button (0 for front, 1 for wall)
    const std::string deviceId_;   // Global device identifier
    const uint8_t brokerId_;
    const int ledIndex_;           // Physical LED strip index

    // LED state - using C++23 aggregate initialization
    struct LedState {
        bool on{false};
        uint8_t brightness{255};
        uint8_t red{255}, green{255}, blue{255};
        std::string effect{"none"};
    } state_;

    // Use the BASE_TOPIC from config.h where needed in topic generation

public:
    explicit ButtonLed(uint8_t buttonPosition, LedPosition ledPosition,
                      uint8_t ledNumber, const std::string& deviceId,
                      uint8_t brokerId) noexcept
        : buttonPosition_(buttonPosition)
        , ledPosition_(ledPosition)
        , ledNumber_(ledNumber)
        , deviceId_(deviceId)
        , brokerId_(brokerId)
        , ledIndex_(calculateLedIndex(buttonPosition, ledPosition, ledNumber)) {}

    // Home Assistant Discovery Integration
    void registerWithHomeAssistant() const;

    // Get state for publishing (to be called by Button component)
    [[nodiscard]] std::string getStateJson() const;
    [[nodiscard]] std::string getBrightnessString() const;
    [[nodiscard]] std::string getColorJson() const;
    [[nodiscard]] std::string getEffectString() const;

    // LED control methods (display only, Button component handles MQTT publishing)
    void setColor(uint8_t r, uint8_t g, uint8_t b) noexcept {
        if (state_.red != r || state_.green != g || state_.blue != b) {
            state_.red = r; state_.green = g; state_.blue = b;
            updateDisplay();
        }
    }

    void setState(bool on) noexcept {
        if (state_.on != on) {
            state_.on = on;
            updateDisplay();
        }
    }

    void setBrightness(uint8_t brightness) noexcept {
        if (state_.brightness != brightness) {
            state_.brightness = brightness;
            updateDisplay();
        }
    }

    void setEffect(const std::string& effect) noexcept {
        if (state_.effect != effect) {
            state_.effect = effect;
            updateDisplay();
        }
    }

    // MQTT message handling for HA integration
    void handleMqttMessage(std::string_view topic, std::string_view data);
    [[nodiscard]] bool matchesTopic(std::string_view topic) const noexcept;

    // Match front/wall position for LED control
    [[nodiscard]] bool matches(const std::string& frontWall) const noexcept {
        if (frontWall == "both") return true;
        if (frontWall == "front" && (ledPosition_ == LedPosition::FRONT ||
            (ledPosition_ == LedPosition::BOTH && ledNumber_ == 0))) return true;
        if (frontWall == "wall" && (ledPosition_ == LedPosition::WALL ||
            (ledPosition_ == LedPosition::BOTH && ledNumber_ == 1))) return true;
        return false;
    }

    // Getters for integration and identification
    [[nodiscard]] const auto& getState() const noexcept { return state_; }
    [[nodiscard]] std::string getUniqueId() const noexcept;
    [[nodiscard]] std::string getEntityName() const noexcept;
    [[nodiscard]] constexpr uint8_t getButtonPosition() const noexcept { return buttonPosition_; }
    [[nodiscard]] constexpr LedPosition getLedPosition() const noexcept { return ledPosition_; }
    [[nodiscard]] constexpr uint8_t getLedNumber() const noexcept { return ledNumber_; }

private:
    void updateDisplay() const noexcept;

    // Calculate physical LED strip index from button position and LED location
    [[nodiscard]] static constexpr int calculateLedIndex(uint8_t buttonPosition,
                                                       LedPosition ledPosition,
                                                       uint8_t ledNumber) noexcept {
        const int row = buttonPosition / 4;
        const bool isRight = (buttonPosition % 4) >= 2;

        // Determine if this is a wall LED based on position and number
        bool isWall = false;
        if (ledPosition == LedPosition::WALL) {
            isWall = true;
        } else if (ledPosition == LedPosition::BOTH) {
            isWall = (ledNumber == 1);  // 0 = front, 1 = wall
        }

        return row * 4 + (isRight ? 2 : 0) + (isWall ? 1 : 0);
    }

    // JSON serialization for MQTT publishing
    [[nodiscard]] std::string stateToJson() const;
    [[nodiscard]] std::string colorToJson() const;

    // Helper for position string conversion
    [[nodiscard]] std::string_view positionToString() const noexcept {
        switch (ledPosition_) {
            case LedPosition::FRONT: return "front";
            case LedPosition::WALL: return "wall";
            case LedPosition::BOTH: return ledNumber_ == 0 ? "front" : "wall";
            default: return "front";
        }
    }

    // Runtime MQTT topic generation - memory efficient, no storage
    [[nodiscard]] std::string generateBaseTopic() const noexcept;
    [[nodiscard]] std::string getStateTopic() const noexcept;
    [[nodiscard]] std::string getCommandTopic() const noexcept;
    [[nodiscard]] std::string getBrightnessStateTopic() const noexcept;
    [[nodiscard]] std::string getBrightnessCommandTopic() const noexcept;
    [[nodiscard]] std::string getColorStateTopic() const noexcept;
    [[nodiscard]] std::string getColorCommandTopic() const noexcept;
    [[nodiscard]] std::string getEffectStateTopic() const noexcept;
    [[nodiscard]] std::string getEffectCommandTopic() const noexcept;
};

// Implementation of key methods
inline std::string ButtonLed::getUniqueId() const noexcept {
    // Device ID: deviceId_buttonPos_ledNum (e.g., "freebutton01_7_1" for button 7, LED 1)
    return deviceId_ + "_" + std::to_string(buttonPosition_) + "_" + std::to_string(ledNumber_);
}

inline std::string ButtonLed::getEntityName() const noexcept {
    // Human-readable: "Button 7 Front LED" or "Button 7 Wall LED"
    return "Button " + std::to_string(buttonPosition_) + " " +
           std::string(positionToString()) + " LED";
}