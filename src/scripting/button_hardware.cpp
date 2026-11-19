//
// Button Hardware Abstraction Layer
// C-compatible interface to C++ Button components
//

#include <Arduino.h>
#include <esp_log.h>
#include "config.h"  // For BUTTON_COUNT
#include "button_hardware.h"
#include <algorithm>
#include <cstring>
#include "components/ConfigManager.h"
#include "drivers/LvglDisplay.h"
#include "ScriptEngine.h"

extern "C" {
#include "ui/ui_button.h"
}

static const char* TAG = "ButtonHAL";

// Access the global config manager (extern declared in main)
extern freebutton::ConfigManager configManager;

// Storage for callbacks - use BUTTON_COUNT from config.h
static struct {
    ButtonClickCallback click;
    ButtonLongPressCallback longPress;
    ButtonReleaseCallback release;
} callbacks[BUTTON_COUNT] = {{nullptr, nullptr, nullptr}};

extern "C" {

// Set button label / top label. Like the MQTT path, these only store the text
// in the button; the LVGL task renders it on its next tick. No LVGL lock is
// taken: script code (loop or button task, engine lock held) need not wait
// for the display.
int button_hw_set_label(int position, const char* label) {
    if (!label) {
        ESP_LOGE(TAG, "NULL label pointer");
        return -1;
    }

    // Get button from global config manager
    auto* button = configManager.getButtonByPosition(position);

    if (!button) {
        ESP_LOGW(TAG, "Button at position %d not found", position);
        return -1;
    }

    if (!button->getButtonObject()) {
        ESP_LOGW(TAG, "Button at position %d has no UI element", position);
        return -1;
    }

    button->setLabelText(label);

    ESP_LOGD(TAG, "Set label for button %d to: %s", position, label);
    return 0;
}

namespace {
int copyLabel(int position, char* out, size_t capacity, bool top) {
    if (!out || capacity == 0) {
        return -1;
    }
    out[0] = '\0';
    const auto* button = configManager.getButtonByPosition(position);
    if (!button) {
        return -1;
    }
    char text[freebutton::Button::kLabelTextBytes];
    top ? button->currentTopLabel(text) : button->currentLabel(text);
    const size_t n = std::min(std::strlen(text), capacity - 1);
    std::memcpy(out, text, n);
    out[n] = '\0';
    return 0;
}
} // namespace

int button_hw_get_label(int position, char* out, size_t capacity) {
    return copyLabel(position, out, capacity, false);
}

int button_hw_get_top_label(int position, char* out, size_t capacity) {
    return copyLabel(position, out, capacity, true);
}

int button_hw_set_top_label(int position, const char* topLabel) {
    if (!topLabel) {
        ESP_LOGE(TAG, "NULL topLabel pointer");
        return -1;
    }

    // Get button from global config manager
    auto* button = configManager.getButtonByPosition(position);

    if (!button) {
        ESP_LOGW(TAG, "Button at position %d not found", position);
        return -1;
    }

    if (!button->getButtonObject()) {
        ESP_LOGW(TAG, "Button at position %d has no UI element", position);
        return -1;
    }

    button->setTopLabelText(topLabel);

    ESP_LOGD(TAG, "Set top label for button %d to: %s", position, topLabel);
    return 0;
}

int button_hw_set_icon(int position, const char* name, size_t len) {
    auto* button = configManager.getButtonByPosition(position);
    if (!button || !button->getButtonObject()) {
        ESP_LOGW(TAG, "Button at position %d not found or has no UI element", position);
        return -1;
    }
    switch (button->setIconName(std::string_view(name ? name : "", name ? len : 0))) {
        case freebutton::Button::SetIconResult::Ok:
            return 0;
        case freebutton::Button::SetIconResult::Invalid:
            return -2;
        case freebutton::Button::SetIconResult::NoMemory:
            break;
    }
    return -3;
}

int button_hw_set_styles(int position, uint32_t list, bool restore) {
    auto* button = configManager.getButtonByPosition(position);
    if (!button) {
        ESP_LOGW(TAG, "Button at position %d not found", position);
        return -1;
    }
    button->setStyles(restore ? button->configStyles() : list);
    return 0;
}

int button_hw_add_style(int position, int ref) {
    auto* button = configManager.getButtonByPosition(position);
    if (!button) {
        ESP_LOGW(TAG, "Button at position %d not found", position);
        return -1;
    }
    return button->addStyle(freebutton::styleref::toRef(ref)) ? 0 : -2;
}

int button_hw_remove_style(int position, int ref) {
    auto* button = configManager.getButtonByPosition(position);
    if (!button) {
        ESP_LOGW(TAG, "Button at position %d not found", position);
        return -1;
    }
    button->removeStyle(freebutton::styleref::toRef(ref));
    return 0;
}

// Get button count from config.h
int button_hw_get_count() {
    return BUTTON_COUNT;
}

bool button_hw_find_by_name(const char* name, size_t len, int* position) {
    uint32_t found = 0;
    if (name == nullptr || position == nullptr ||
        !configManager.names().find(freebutton::NameTable::Kind::Key, std::string_view(name, len), found)) {
        return false;
    }
    *position = static_cast<int>(found);
    return true;
}

// Register click callback (positions are 1-based: 1-8)
void button_hw_register_click_callback(int position, ButtonClickCallback callback) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        callbacks[position - 1].click = callback;
        ESP_LOGD(TAG, "Registered click callback for button %d", position);
    }
}

// Register long press callback (positions are 1-based: 1-8)
void button_hw_register_long_press_callback(int position, ButtonLongPressCallback callback) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        callbacks[position - 1].longPress = callback;
        ESP_LOGD(TAG, "Registered long press callback for button %d", position);
    }
}

// Register release callback (positions are 1-based: 1-8)
void button_hw_register_release_callback(int position, ButtonReleaseCallback callback) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        callbacks[position - 1].release = callback;
        ESP_LOGD(TAG, "Registered release callback for button %d", position);
    }
}

// Dispatch to JavaScript. Runs in the button task, synchronously: the
// handler's return value decides whether the press is published. Waits for
// the JS engine (a timer or MQTT callback may be running on the loop task),
// but not forever: after kEngineWait the event is not given to the script
// and the default action proceeds.
static constexpr TickType_t kEngineWait = pdMS_TO_TICKS(1000);

static bool dispatchToScript(const ButtonClickCallback callback, const int position, const char* what) {
    if (!callback) {
        return false;
    }
    const Scripting::EngineLock lock(kEngineWait);
    if (!lock.owned()) {
        ESP_LOGW(TAG, "Button %d %s: script engine busy, not dispatched to JS", position, what);
        return false;
    }
    return callback(position);
}

// Dispatch click event to JavaScript (positions are 1-based: 1-8)
bool button_hw_dispatch_click(int position) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        return dispatchToScript(callbacks[position - 1].click, position, "click");
    }
    return false;
}

// Dispatch long press event to JavaScript (positions are 1-based: 1-8)
bool button_hw_dispatch_long_press(int position) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        return dispatchToScript(callbacks[position - 1].longPress, position, "long press");
    }
    return false;
}

// Dispatch release event to JavaScript (positions are 1-based: 1-8)
bool button_hw_dispatch_release(int position) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        return dispatchToScript(callbacks[position - 1].release, position, "release");
    }
    return false;
}

} // extern "C"
