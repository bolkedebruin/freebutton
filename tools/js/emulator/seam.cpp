//
// seam.cpp - the device behind the script bindings, for fb-emulator.wasm.
//
// The firmware's bindings (src/scripting/*_hardware.cpp, *_binding.cpp) talk
// to ConfigManager, LVGL and the drivers. Here the device state lives in the
// host (web/src/js/emulator/device-state.ts), so the seam functions that only
// read or write state are wasm imports under their own name. This file has
// the ones in front of which the firmware applies a rule, and applies it with
// the firmware's own code:
//
//   keys      callback registry and dispatch as button_hardware.cpp; the
//             press / long press / repeat timing is src/drivers/PushButton.cpp
//             (compiled in), fed with the GPIO byte the host gives; the event
//             path is ButtonManager's (script first, default action unless
//             the handler returned false)
//   icons     freebutton::icon (components/IconLogic.h)
//   lists     freebutton::list (components/ListLogic.h)
//   sensors   src/scripting/sensor_binding.cpp (compiled in) over
//             FreeButton::sensorDevices
//   system    FIRMWARE / MODEL of include/config.h
//
// MQTT is in seam_mqtt.cpp.
//
#include <Arduino.h>
#include <esp_log.h>

#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <string_view>

#include "config.h"
#include "fbh.h"

#include "components/IconLogic.h"
#include "components/ListLogic.h"
#include "drivers/PushButton.h"
#include "drivers/Sensors.h"
#include "scripting/ScriptEngine.h"
#include "scripting/button_hardware.h"
#include "scripting/displayitem_binding.h"
#include "scripting/image_binding.h"
#include "scripting/list_binding.h"
#include "scripting/sensor_binding.h"
#include "scripting/system_binding.h"
#include "components/ConfigManager.h"

#define FBE_EXPORT(name) __attribute__((export_name(#name), used))

static const char* TAG = "Emulator";

// include/config.h declares both; the firmware defines them in main.cpp
// (deviceId from the MAC address). Here the id is the emulator's virtual
// identity, never a real device's: the host gives it when the module's
// constructors run, as the object is const.
static std::string readDeviceId() {
    char id[64];
    const size_t n = fbh_device_id(id, sizeof(id));
    return std::string(id, n < sizeof(id) ? n : sizeof(id));
}
const std::string deviceId(readDeviceId());
freebutton::ConfigManager configManager;

namespace FreeButton {
std::map<int, std::shared_ptr<SensorDevice>> sensorDevices;
}

// One thread: the engine lock is always free.
namespace Scripting {
EngineLock::EngineLock(const TickType_t wait) : owned_(true) { (void)wait; }
EngineLock::~EngineLock() = default;
} // namespace Scripting

extern "C" {
// runner.c
void fbe_report_pending_exception(void);
void fbe_enter_engine(void);
}

namespace {

struct {
    ButtonClickCallback click;
    ButtonLongPressCallback longPress;
    ButtonReleaseCallback release;
} callbacks[BUTTON_COUNT] = {};

std::unique_ptr<FreeButton::PushButton> pushButtons[BUTTON_COUNT];

bool dispatchToScript(const ButtonClickCallback callback, const int position) {
    if (!callback) {
        return false;
    }
    const bool prevented = callback(position);
    // The glue ignores an exception in a key handler (the default action
    // proceeds); the host still wants to show it.
    fbe_report_pending_exception();
    return prevented;
}

// drivers/ButtonManager.cpp addButton(): the script first; a handler that
// returned false cancels the default action (the configured topics and the
// Home Assistant event), which the host is told about.
void onButtonEvent(const int pos, const EventType event) {
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
    fbh_key_event(pos, event, defaultPrevented ? 1 : 0);
}

bool keyExists(const int position) {
    char text[4];
    return button_hw_get_label(position, text, sizeof(text)) == 0;
}

} // namespace

extern "C" {

// --- identity ------------------------------------------------------------------

uint32_t system_hw_get_uptime(void) { return fbh_uptime_seconds(); }
uint32_t system_hw_get_free_heap(void) { return fbh_free_heap(); }
const char* system_hw_get_version(void) { return FIRMWARE; }
const char* system_hw_get_id(void) { return deviceId.c_str(); }
const char* system_hw_get_model(void) { return MODEL; }
// config.json "core.name", the id without one
// (HomeAssistantDiscovery::copyDeviceName): the host has the config.
size_t system_hw_get_name(char* buf, const size_t cap) {
    const size_t n = fbh_device_name(buf, cap);
    return n < cap ? n : cap;
}

// --- keys: scripting/button_hardware.cpp ------------------------------------------

int button_hw_get_count() { return BUTTON_COUNT; }

int button_hw_set_icon(const int position, const char* name, const size_t len) {
    if (!keyExists(position)) {
        ESP_LOGW(TAG, "Button at position %d not found or has no UI element", position);
        return -1;
    }
    const std::string_view icon(name ? name : "", name ? len : 0);
    if (!icon.empty() && !freebutton::icon::validName(icon)) {
        return -2;
    }
    return fbh_key_set_icon(position, icon.data(), icon.size());
}

void button_hw_register_click_callback(const int position, const ButtonClickCallback callback) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        callbacks[position - 1].click = callback;
    }
}

void button_hw_register_long_press_callback(const int position, const ButtonLongPressCallback callback) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        callbacks[position - 1].longPress = callback;
    }
}

void button_hw_register_release_callback(const int position, const ButtonReleaseCallback callback) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        callbacks[position - 1].release = callback;
    }
}

bool button_hw_dispatch_click(const int position) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        return dispatchToScript(callbacks[position - 1].click, position);
    }
    return false;
}

bool button_hw_dispatch_long_press(const int position) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        return dispatchToScript(callbacks[position - 1].longPress, position);
    }
    return false;
}

bool button_hw_dispatch_release(const int position) {
    if (position >= 1 && position <= BUTTON_COUNT) {
        return dispatchToScript(callbacks[position - 1].release, position);
    }
    return false;
}

// A physical key at position (1-based): the module on its connector exists.
FBE_EXPORT(fbe_button_add) int fbe_button_add(const int position) {
    if (position < 1 || position > BUTTON_COUNT) {
        return -1;
    }
    const int id = position - 1;
    pushButtons[id] = std::make_unique<FreeButton::PushButton>(id, id, nullptr);
    pushButtons[id]->onButtonEvent(onButtonEvent);
    return 0;
}

FBE_EXPORT(fbe_buttons_clear) void fbe_buttons_clear(void) {
    for (auto& button : pushButtons) {
        button.reset();
    }
}

// One pass of ButtonManager::updateAllButtons() (every
// FB_BUTTON_UPDATE_INTERVAL_MS on the device). gpio: bit (position - 1) is 0
// while that key is held, as the port expander reads. Returns the keys that
// are down, as a bit mask.
FBE_EXPORT(fbe_button_tick) uint32_t fbe_button_tick(const uint32_t gpio) {
    fbe_enter_engine();
    uint32_t down = 0;
    for (int id = 0; id < BUTTON_COUNT; ++id) {
        auto* button = pushButtons[id].get();
        if (!button) {
            continue;
        }
        button->updateState(static_cast<uint8_t>(gpio));
        button->processEvents();
        if (button->buttonDown()) {
            down |= 1u << id;
        }
    }
    return down;
}

// A press sent from Home Assistant (ButtonManager::processHomeAssistantCommands):
// click, then release, no timing. 0, or -1 when there is no such key.
FBE_EXPORT(fbe_button_inject) int fbe_button_inject(const int position) {
    if (position < 1 || position > BUTTON_COUNT || !pushButtons[position - 1]) {
        return -1;
    }
    fbe_enter_engine();
    pushButtons[position - 1]->injectEvent(CLICK);
    pushButtons[position - 1]->injectEvent(RELEASE);
    return 0;
}

// --- display items: scripting/displayitem_binding.cpp ---------------------------

int displayitem_set_icon(const unsigned long id, const char* name, const size_t len) {
    if (displayitem_get_info(id) == nullptr) {
        return 0;
    }
    const std::string_view icon(name ? name : "", name ? len : 0);
    if (!icon.empty() && !freebutton::icon::validName(icon)) {
        return -1;
    }
    return fbh_item_set_icon(id, icon.data(), icon.size());
}

// --- images: scripting/image_binding.cpp + ImageItem::store -----------------------

bool image_hw_exists(const int index) {
    return index >= 0 && fbh_image_exists(index) != 0;
}

static int imageSet(const int index, const char tag, const char* value, const size_t len) {
    if (!image_hw_exists(index)) {
        return -1;
    }
    char want[freebutton::icon::kWantMax + 1];
    const std::string_view text(value ? value : "", value ? len : 0);
    if (!freebutton::icon::encodeWant(want, sizeof(want), tag, text)) {
        return -2;
    }
    fbh_image_set(index, tag, text.data(), text.size());
    return 0;
}

int image_hw_set_icon(const int index, const char* name, const size_t len) {
    return imageSet(index, freebutton::icon::kWantIcon, name, len);
}

int image_hw_set_src(const int index, const char* key, const size_t len) {
    return imageSet(index, freebutton::icon::kWantSrc, key, len);
}

// --- lists: scripting/list_binding.cpp + List::move / select / setItems ---------

namespace {
struct ItemsBuilder {
    std::string options;
    int count = 0;
};
} // namespace

int list_hw_count(const int index) {
    return index < 0 ? -1 : fbh_list_count(index);
}

int list_hw_get_selected(const int index) {
    return list_hw_count(index) < 0 ? -1 : fbh_list_get_selected(index);
}

int list_hw_move(const int index, const int step) {
    const int count = list_hw_count(index);
    if (count < 0) {
        return -1;
    }
    const int now = fbh_list_get_selected(index);
    const int next = freebutton::list::clampSelected(now + step, count);
    if (next != now) {
        fbh_list_set_selected(index, next);
    }
    return next;
}

bool list_hw_select(const int index, const int item) {
    const int count = list_hw_count(index);
    if (count < 0 || item < 0 || item >= count) {
        return false;
    }
    if (fbh_list_get_selected(index) != item) {
        fbh_list_set_selected(index, item);
    }
    return true;
}

void* list_hw_items_new(void) {
    return new (std::nothrow) ItemsBuilder();
}

bool list_hw_items_add(void* builder, const char* text, const size_t len) {
    auto* b = static_cast<ItemsBuilder*>(builder);
    return b != nullptr && freebutton::list::appendItem(b->options, b->count, std::string_view(text, len));
}

bool list_hw_items_commit(const int index, void* builder) {
    auto* b = static_cast<ItemsBuilder*>(builder);
    if (b == nullptr) {
        return false;
    }
    const bool exists = list_hw_count(index) >= 0;
    if (exists) {
        const int selected = freebutton::list::clampSelected(fbh_list_get_selected(index), b->count);
        fbh_list_set_items(index, b->options.data(), b->options.size(), b->count, selected);
    }
    delete b;
    return exists;
}

void list_hw_items_free(void* builder) {
    delete static_cast<ItemsBuilder*>(builder);
}

// --- sensors ---------------------------------------------------------------------

FBE_EXPORT(fbe_sensors_clear) void fbe_sensors_clear(void) {
    FreeButton::sensorDevices.clear();
}

FBE_EXPORT(fbe_sensor_add)
void fbe_sensor_add(const int id, const char* name, const char* deviceClass, const char* unit, const int online) {
    FreeButton::sensorDevices[id] =
        std::make_shared<FreeButton::SensorDevice>(id, name, deviceClass, unit, online != 0);
}

FBE_EXPORT(fbe_sensor_set_online) void fbe_sensor_set_online(const int id, const int online) {
    const auto it = FreeButton::sensorDevices.find(id);
    if (it != FreeButton::sensorDevices.end()) {
        it->second->online = online != 0;
    }
}

// sampleSensors() (every SENSOR_UPDATE_INTERVAL on the device): the firmware
// decides whether the change is large enough for the script's onChange.
FBE_EXPORT(fbe_sensor_notify) void fbe_sensor_notify(const int id, const float value) {
    fbe_enter_engine();
    sensor_hw_notify_value(id, value);
}

FBE_EXPORT(fbe_sensor_interval_ms) uint32_t fbe_sensor_interval_ms(void) {
    return static_cast<uint32_t>(SENSOR_UPDATE_INTERVAL);
}

} // extern "C"
