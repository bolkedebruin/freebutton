//
// device.cpp - the device around the script engine, for fb-emulator.wasm.
//
// This file is src/main.cpp for the emulator: it boots the firmware's own
// code in the order setup() does (hardware scan, config.json, brokers, Home
// Assistant discovery sources) and runs, when the runner asks, what the
// firmware's tasks and loop() do (the key scan, the sensor sample, the MQTT
// task's events, discovery, the LVGL task's pending renders).
//
// It also tells the host what the device shows: after every pass, one JSON
// record per display item, key, list, image and clock that changed
// (fbh_state), read from the firmware's components and from the virtual
// widgets they drew (ui_shim.cpp). The records carry what the firmware
// decided: texts cut to their slots, resolved runs with font and colour,
// positions in pixels.
//
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Arduino.h>
#include <ArduinoJson.h>
#include <rapidjson/writer.h>

// The components keep what they show in private members (the widget, the
// text slots). The emulator only reads them, here, to report the state; the
// firmware sources stay as they are.
#define private public
#define protected public
#include "components/ConfigManager.h"
#include "core/HardwareInitializer.h"
#include "drivers/ButtonManager.h"
#include "drivers/NeoPixel.h"
#include "drivers/Sensors.h"
#include "mqtt/Broker.h"
#include "mqtt/HomeAssistantDiscovery.h"
#include "mqtt/Registry.h"
#include "scripting/ScriptEngine.h"
#undef private
#undef protected

#include "config.h"
#include "emu.h"
#include "fbh.h"
#include "pins.h"
#include "ui/MainPage.h"
#include "ui/PendingRender.h"
#include "ui_shim.h"

extern "C" {
#include "../../lib/vendored/sts3x/sts3x.h"
#include "../../lib/vendored/stscommon/sensirion_i2c.h"
#include "ui/fonts/fonts.h"
#include "ui/ui_style.h"
}

namespace emu {
bool clockBuilt(int& x, int& y, int& width, int& height, int& align);
}

static const char* TAG = "Emulator";

// ---------------------------------------------------------------------------
// The globals of main.cpp
// ---------------------------------------------------------------------------

// The id is the emulator's virtual identity, never a real device's: the host
// gives it when the module's constructors run, as the object is const.
static std::string readDeviceId() {
    char id[64];
    const size_t n = fbh_device_id(id, sizeof(id));
    return std::string(id, n < sizeof(id) ? n : sizeof(id));
}
const std::string deviceId(readDeviceId());

NeoPixelController neopixels(NEOPIN);
freebutton::ConfigManager configManager;
TwoWire Wire(0);
TwoWire Wire1(1);

// ---------------------------------------------------------------------------
// What is not compiled in: the engine owner, the filesystem, the LED strip,
// the STS35 driver
// ---------------------------------------------------------------------------

// One thread: the engine lock is always free.
namespace Scripting {
EngineLock::EngineLock(const TickType_t wait) : owned_(true) { (void)wait; }
EngineLock::~EngineLock() = default;

// The engine itself is runner.c; the firmware's sensors ask this object for
// the JS heap.
ScriptEngine::ScriptEngine(const size_t heapSize) : heapSize_(heapSize) {}
ScriptEngine::~ScriptEngine() = default;
ScriptEngine& ScriptEngine::getInstance(const size_t heapSize) {
    static ScriptEngine instance(heapSize);
    return instance;
}
ScriptEngine::HeapUsage ScriptEngine::heapUsage() const {
    HeapUsage usage;
    fbe_js_heap(&usage.used, &usage.free);
    usage.size = usage.used + usage.free;
    return usage;
}
} // namespace Scripting

namespace FreeButton {
BusAwareFileSystem& getBusAwareFileSystem() {
    static BusAwareFileSystem fs;
    return fs;
}

BusAwareFile BusAwareFileSystem::open(const char* path, const char* mode) {
    (void)mode;
    uint32_t len = 0;
    uint8_t* data = fbh_file_read(path, std::strlen(path), &len);
    if (!data) {
        return BusAwareFile();
    }
    auto text = std::make_shared<const std::string>(reinterpret_cast<const char*>(data), len);
    std::free(data);
    return BusAwareFile(std::move(text));
}
} // namespace FreeButton

namespace {
CRGB* g_leds = nullptr;
int g_ledCount = 0;
CRGB g_ledsShown[32];
} // namespace

CFastLED FastLED;

void CFastLED::attach(CRGB* leds, const int count) {
    g_leds = leds;
    g_ledCount = count < 32 ? count : 32;
}

void CFastLED::show() {
    for (int i = 0; i < g_ledCount; ++i) {
        const CRGB& led = g_leds[i];
        if (led.r != g_ledsShown[i].r || led.g != g_ledsShown[i].g || led.b != g_ledsShown[i].b) {
            g_ledsShown[i] = led;
            fbh_led(i, led.r, led.g, led.b);
        }
    }
}

extern "C" {
void sensirion_i2c_init(void) {}
int16_t sts3x_probe(const uint8_t address) { return fbh_i2c_probe(0, address) ? STATUS_OK : -1; }
int16_t sts3x_measure_blocking_read(const uint8_t address, int32_t* temperature) {
    if (!fbh_i2c_probe(0, address)) return -1;
    *temperature = static_cast<int32_t>(std::lround(fbh_sensor_read(address == 0x4A ? 3 : 4) * 1000.0f));
    return STATUS_OK;
}
}

// ---------------------------------------------------------------------------
// State records (fbh.h FBH_STATE_*)
// ---------------------------------------------------------------------------

namespace {

using emu::Json;

// What was last sent, per record, so that only changes go to the host.
std::map<std::string, std::string> g_sent;
std::map<unsigned long, bool> g_itemsSent;
int g_pageSent = -1;
std::string g_timezone;

void send(const int kind, const std::string& key, const std::string& json) {
    auto& last = g_sent[key];
    if (last == json) {
        return;
    }
    last = json;
    fbh_state(kind, json.data(), json.size());
}

std::string_view nameOf(const freebutton::NameTable::Kind kind, const uint32_t id) {
    return configManager.names().nameOf(kind, id);
}

// The text a slot holds, or `configured` when nothing was written to it.
template <class Slot>
std::string slotText(const Slot& slot, const std::string& configured) {
    char text[Slot::kBufferSize];
    return slot.read(text) ? std::string(text) : configured;
}

void writeName(Json& json, const std::string_view name) {
    json.key("name");
    if (name.empty()) json.null(); else json.value(std::string(name));
}

// A display item. "order": its place among the config's display items (file
// order, both spellings of the list), -1 for one a script made. "box": what
// is drawn, null while the item's page is not shown (the firmware then keeps
// no widget; "label" / "unit" are the configured texts).
void sendItem(freebutton::DisplayItem& item) {
    Json json;
    json.beginObject();
    json.key("id").value(item.id);
    json.key("order").value(static_cast<int>(item.configOrder_));
    writeName(json, nameOf(freebutton::NameTable::Kind::DisplayItem, static_cast<uint32_t>(item.id)));
    json.key("x").value(item.x);
    json.key("y").value(item.y);
    json.key("width").value(item.width);
    json.key("boxType").value(item.boxType);
    json.key("fontSize").value(item.fontSize);
    json.key("align").value(item.alignment);
    json.key("round").value(item.round);
    json.key("page").value(item.page);
    json.key("label").value(item.label);
    json.key("unit").value(item.unit);
    // What the item holds now, drawn or not: the text last set at runtime
    // (a config topic, a script), else the configured one.
    json.key("now").beginObject();
    json.key("label").value(slotText(item.captionSlot_, item.label));
    json.key("value").value(slotText(item.valueSlot_, std::string()));
    json.key("unit").value(slotText(item.unitSlot_, item.unit));
    json.key("icon");
    if (const auto* icon = item.icon_.get()) json.value(slotText(icon->name, std::string())); else json.value("");
    json.endObject();
    json.key("box");
    if (const lv_obj_t* box = item.box) {
        json.beginObject();
        json.key("x").value(box->x);
        json.key("y").value(box->y);
        json.key("width").value(box->width);
        json.key("align").value(box->align);
        json.key("underline").value(box->underline);
        json.key("captionPad");
        if (box->hasPadding) json.value(box->captionPad); else json.null();
        json.key("boxPad");
        if (box->hasPadding) json.value(box->boxPad); else json.null();
        json.key("caption");
        emu::writeText(json, box->caption);
        json.key("value");
        emu::writeText(json, box->value);
        json.key("unit");
        emu::writeText(json, box->unit);
        json.key("icon");
        emu::writeIcon(json, box->icon);
        json.endObject();
    } else {
        json.null();
    }
    json.endObject();
    send(FBH_STATE_ITEM, "item" + std::to_string(item.id), json.str());
}

// A key of config.json "buttons". "order": its place among the config's
// keys. "display": what its own display shows; null for a key without one (a
// key of the main display, a key on a connector without a module).
void sendKey(freebutton::Button& button, const int order) {
    Json json;
    json.beginObject();
    json.key("order").value(order);
    json.key("position").value(static_cast<int>(button.getPosition()));
    writeName(json, nameOf(freebutton::NameTable::Kind::Key, button.getPosition()));
    json.key("page").value(static_cast<int>(button.page_));
    json.key("label").value(button.label);
    json.key("topLabel").value(button.topLabel);
    json.key("display");
    if (const lv_obj_t* b = button.button) {
        json.beginObject();
        json.key("index").value(b->display);
        json.key("label");
        emu::writeText(json, b->text);
        json.key("topLabel");
        emu::writeText(json, b->title);
        json.key("icon");
        emu::writeIcon(json, b->icon);
        json.endObject();
    } else {
        json.null();
    }
    json.endObject();
    send(FBH_STATE_KEY, "key" + std::to_string(order), json.str());
}

void sendList(freebutton::List& list, const int index) {
    Json json;
    json.beginObject();
    json.key("index").value(index);
    writeName(json, nameOf(freebutton::NameTable::Kind::List, static_cast<uint32_t>(index)));
    json.key("page").value(list.page_);
    json.key("selected").value(list.selected());
    json.key("items").beginArray();
    if (list.count() > 0) {
        const std::string options = list.optionsCopy();
        size_t at = 0;
        for (;;) {
            const size_t end = options.find('\n', at);
            json.value(options.substr(at, end == std::string::npos ? std::string::npos : end - at));
            if (end == std::string::npos) break;
            at = end + 1;
        }
    }
    json.endArray();
    json.key("roller");
    if (const lv_obj_t* r = list.roller_) {
        const style_props_t* style = ui_style_resolved(STYLE_EL_LIST);
        const style_props_t* selected = ui_style_resolved(STYLE_EL_LIST_SELECTED);
        json.beginObject();
        json.key("x").value(r->x);
        json.key("y").value(r->y);
        json.key("width").value(r->width);
        json.key("rows").value(r->rows);
        json.key("selected").value(r->selected);
        json.key("font");
        emu::writeFont(json, emu::rollerFont(r));
        json.key("lineSpace");
        if (r->hasLineSpace) json.value(r->lineSpace); else json.value(static_cast<int>(style->line_space));
        json.key("color").value(emu::hexColor(r->hasTextColor ? r->textColor : style->color));
        json.key("selectedBg").value(emu::hexColor(r->hasSelectedBg ? r->selectedBg : selected->bg_color));
        json.key("selectedColor").value(emu::hexColor(r->hasSelectedText ? r->selectedText : selected->color));
        json.endObject();
    } else {
        json.null();
    }
    json.endObject();
    send(FBH_STATE_LIST, "list" + std::to_string(index), json.str());
}

// "kind" / "value": what the image wants to show (an icon name, a key of the
// images bucket); "raster": what it draws, null when its page is not shown
// or the bucket has no such raster.
void sendImage(freebutton::ImageItem& image, const int index) {
    Json json;
    json.beginObject();
    json.key("index").value(index);
    writeName(json, nameOf(freebutton::NameTable::Kind::Image, static_cast<uint32_t>(index)));
    json.key("page").value(image.page_);
    json.key("x").value(static_cast<int>(image.x_));
    json.key("y").value(static_cast<int>(image.y_));
    json.key("width").value(static_cast<int>(image.width_));
    json.key("height").value(static_cast<int>(image.height_));
    char want[freebutton::TextSlot<freebutton::ImageItem::kWantSlotBytes>::kBufferSize] = "";
    const bool has = image.want_.read(want) && want[0] != '\0';
    json.key("kind").value(!has ? "none" : want[0] == freebutton::icon::kWantIcon ? "icon" : "src");
    json.key("value").value(has ? want + 1 : "");
    json.key("raster");
    if (image.image_ && !image.image_->hidden) {
        emu::writeIcon(json, image.image_->icon);
    } else {
        json.null();
    }
    json.endObject();
    send(FBH_STATE_IMAGE, "image" + std::to_string(index), json.str());
}

// The analog clock: "shown" while its page is shown (only the first clock of
// a config is drawn). Its hands are the device's local time.
void sendClock(freebutton::Clock& clock, const int index) {
    int x, y, width, height, align;
    const bool drawn = clock.built_ && emu::clockBuilt(x, y, width, height, align);
    Json json;
    json.beginObject();
    json.key("index").value(index);
    json.key("id").value(clock.id);
    json.key("page").value(clock.page);
    json.key("x").value(clock.x);
    json.key("y").value(clock.y);
    json.key("width").value(clock.width);
    json.key("height").value(clock.height);
    json.key("align").value(static_cast<int>(clock.align));
    json.key("shown").value(drawn);
    json.endObject();
    send(FBH_STATE_CLOCK, "clock" + std::to_string(index), json.str());
}

// The firmware's sensor ids are its SensorType values (drivers/Sensors.cpp addSensor).
int sensorInput(const int id) {
    switch (static_cast<FreeButton::SensorType>(id)) {
        case FreeButton::SensorType::BME280_TEMPERATURE: return 0;
        case FreeButton::SensorType::BME280_PRESSURE: return 1;
        case FreeButton::SensorType::BME280_HUMIDITY: return 2;
        case FreeButton::SensorType::STS35: return 3;
        case FreeButton::SensorType::STS35_2: return 4;
        default: return -1;
    }
}

// The device: name, time zone, brokers, modules, sensors, fonts, styles.
// "input" of a sensor: which reading of the host it samples (fbh_sensor_read),
// -1 for one the firmware computes (memory).
void sendDevice() {
    Json json;
    json.beginObject();
    json.key("id").value(deviceId);
    char name[96];
    const size_t n = HomeAssistantDiscovery::copyDeviceName(name, sizeof(name));
    json.key("name").value(std::string(name, n));
    json.key("firmware").value(FIRMWARE);
    json.key("model").value(MODEL);
    json.key("timezone").value(g_timezone);
    json.key("connectors").beginArray();
    for (int c = 0; c < 4; ++c) {
        const auto type = FreeButton::getHardwareInitializer().getScanner().getConnectorType(c);
        json.value(type ? static_cast<int>(*type) : 0);
    }
    json.endArray();
    json.key("keys").beginArray();
    for (int position = 1; position <= BUTTON_COUNT; ++position) {
        if (FreeButton::getHardwareInitializer().getButtonManager().getButton(position)) json.value(position);
    }
    json.endArray();
    json.key("keyDisplays").beginArray();
    for (const auto& d : FreeButton::getDisplayManager().smallDisplays()) {
        json.value(d.connector * 2 + 1 + d.position);
    }
    json.endArray();
    json.key("leds").value(NeoPixelController::NUMBER_OF_LEDS);
    json.key("brokers").beginArray();
    MqttBrokerRegistry::forEachBroker([&json](const uint8_t id, const MqttBroker* broker) {
        json.beginObject();
        json.key("index").value(static_cast<int>(id));
        json.key("name").value(std::string(broker->getName()));
        json.key("url").value(broker->url);
        json.key("configured").value(broker->isConfigured());
        json.key("discovery").value(broker->hasAutoDiscovery());
        json.endObject();
    });
    json.endArray();
    json.key("sensors").beginArray();
    for (const auto& [id, sensor] : FreeButton::sensorDevices) {
        if (!sensor) continue;
        json.beginObject();
        json.key("id").value(id);
        json.key("name").value(std::string(sensor->name));
        json.key("type").value(std::string(sensor->deviceClass));
        json.key("unit").value(std::string(sensor->unitOfMeasurement));
        json.key("online").value(sensor->online);
        json.key("input").value(sensorInput(id));
        json.endObject();
    }
    json.endArray();
    json.key("fonts");
    emu::writeFonts(json);
    json.key("styles");
    emu::writeStyles(json);
    json.endObject();
    send(FBH_STATE_DEVICE, "device", json.str());
}

void syncState() {
    sendDevice();

    const int page = freebutton::ui::shownPage();
    if (page != g_pageSent) {
        g_pageSent = page;
        Json json;
        json.beginObject().key("page").value(page).endObject();
        fbh_state(FBH_STATE_PAGE, json.str().data(), json.str().size());
    }

    for (auto& [id, seen] : g_itemsSent) seen = false;
    configManager.forEachDisplayItem([](freebutton::DisplayItem& item) {
        sendItem(item);
        g_itemsSent[item.id] = true;
    });
    for (auto it = g_itemsSent.begin(); it != g_itemsSent.end();) {
        if (it->second) {
            ++it;
            continue;
        }
        Json json;
        json.beginObject().key("id").value(it->first).endObject();
        fbh_state(FBH_STATE_ITEM_GONE, json.str().data(), json.str().size());
        g_sent.erase("item" + std::to_string(it->first));
        it = g_itemsSent.erase(it);
    }

    int order = 0;
    configManager.forEachButton([&order](freebutton::Button& button) { sendKey(button, order++); });
    for (size_t i = 0;; ++i) {
        freebutton::List* list = configManager.findList(i);
        if (!list) break;
        sendList(*list, static_cast<int>(i));
    }
    for (size_t i = 0;; ++i) {
        freebutton::ImageItem* image = configManager.findImage(i);
        if (!image) break;
        sendImage(*image, static_cast<int>(i));
    }
    int clock = 0;
    configManager.forEachClock([&clock](freebutton::Clock& c) { sendClock(c, clock++); });
}

FreeButton::ButtonManager& buttons() {
    return FreeButton::getHardwareInitializer().getButtonManager();
}

bool g_booted = false;

} // namespace

// ---------------------------------------------------------------------------
// For the runner (emu.h)
// ---------------------------------------------------------------------------
extern "C" {

void fbe_device_restore_config_state(void) {
    if (g_booted) configManager.restoreConfigState();
}

void fbe_device_after_entry(void) {
    if (!g_booted) return;
    // The MQTT task: what the script's subscribe / unsubscribe asked for.
    fbe_mqtt_pump();
    // The LVGL task: draw what changed.
    freebutton::ui::applyPendingRenders();
    FastLED.show();
    syncState();
}

int fbe_device_pass(void) {
    if (!g_booted) return 0;
    fbe_mqtt_pump();
    // The button task also turns a press sent from Home Assistant into a key event.
    const int scope = fbe_publish_scope(FBH_ORIGIN_FIRMWARE, FBH_PUBLISH_KEY);
    buttons().processHomeAssistantCommands();
    fbe_publish_scope_end(scope);
    return fbe_discovery_poll();
}

// ---------------------------------------------------------------------------
// For the host
// ---------------------------------------------------------------------------

// setup() of src/main.cpp, up to the script engine: the hardware scan, the
// configuration ("/config.json" of the host's files), the brokers and the
// Home Assistant discovery sources. Once per instance. 0 = ok.
FBE_EXPORT(fbe_device_boot) int fbe_device_boot(void) {
    if (g_booted) return -1;
    ESP_LOGI(TAG, "FreeButton Firmware: %s  Id: %s starting!", FIRMWARE, deviceId.c_str());
    HomeAssistantDiscovery::setDeviceInfo({deviceId, "ButtonPlus", MODEL, FIRMWARE, deviceId});
    neopixels.begin();

    if (!FreeButton::initializeAllHardware()) {
        // As on the device: without a module there is nothing to run on.
        ESP_LOGE(TAG, "Hardware initialization failed!");
    }

    configManager.loadFromFile("/config.json");

    // NtpClient::initialize(): the zone of config.json, else the firmware's default.
    g_timezone = configManager.getTimezone().empty() ? "CET-1CEST,M3.5.0,M10.5.0/3" : configManager.getTimezone();
    fbe_tz_set(g_timezone.c_str());

    configManager.startBrokers(deviceId);
    HomeAssistantDiscovery::addSource(FreeButton::sensorDiscoverySource());
    HomeAssistantDiscovery::addSource(buttons());
    MqttBrokerRegistry::forEachBroker([](const uint8_t brokerId, const MqttBroker* broker) {
        if (broker && broker->isConfigured() && broker->hasAutoDiscovery()) {
            buttons().subscribeHomeAssistantCommands(brokerId);
        }
    });

    g_booted = true;
    fbe_device_after_entry();
    return 0;
}

// One pass of the button task (ButtonManager::updateAllButtons, every
// FB_BUTTON_UPDATE_INTERVAL_MS on the device): the key switches are read
// from the host's port expanders (fbh_mcp_gpio). Returns the positions that
// are down, as a bit mask (bit position - 1).
FBE_EXPORT(fbe_button_tick) uint32_t fbe_button_tick(void) {
    if (!g_booted) return 0;
    fbe_enter_engine();
    const int scope = fbe_publish_scope(FBH_ORIGIN_FIRMWARE, FBH_PUBLISH_KEY);
    buttons().updateAllButtons();
    fbe_publish_scope_end(scope);
    uint32_t down = 0;
    for (const auto& button : buttons().buttons_) {
        if (button->buttonDown()) down |= 1u << (button->position() - 1);
    }
    fbe_device_after_entry();
    return down;
}

// A short press without timing: click, then release (what a press sent from
// Home Assistant does). 0, or -1 when there is no such key.
FBE_EXPORT(fbe_button_inject) int fbe_button_inject(const int position) {
    if (!g_booted) return -1;
    const auto* button = buttons().getButton(position);
    if (!button) return -1;
    fbe_enter_engine();
    const int scope = fbe_publish_scope(FBH_ORIGIN_FIRMWARE, FBH_PUBLISH_KEY);
    button->injectEvent(CLICK);
    button->injectEvent(RELEASE);
    fbe_publish_scope_end(scope);
    fbe_device_after_entry();
    return 0;
}

// sampleSensors() of loop(), every fbe_sensor_interval_ms(): each sensor's
// state is published and the script's onChange handlers hear of a change.
FBE_EXPORT(fbe_sample_sensors) void fbe_sample_sensors(void) {
    if (!g_booted) return;
    fbe_enter_engine();
    const int scope = fbe_publish_scope(FBH_ORIGIN_FIRMWARE, FBH_PUBLISH_SENSOR);
    FreeButton::sampleSensors();
    fbe_publish_scope_end(scope);
    fbe_device_after_entry();
}

FBE_EXPORT(fbe_sensor_interval_ms) uint32_t fbe_sensor_interval_ms(void) {
    return static_cast<uint32_t>(SENSOR_UPDATE_INTERVAL);
}

// The reading of a sensor as the firmware samples it now (sensor.getValue()).
FBE_EXPORT(fbe_sensor_value) float fbe_sensor_value(const int id) {
    const auto it = FreeButton::sensorDevices.find(id);
    return it != FreeButton::sensorDevices.end() && it->second && it->second->online ? it->second->sample() : 0.0f;
}

// The pin of a key's switch on its connector's port expander (include/pins.h),
// for the host's fbh_mcp_gpio: side 0 = left, 1 = right.
FBE_EXPORT(fbe_switch_pin) int fbe_switch_pin(const int side) {
    return side == 0 ? MCP_SWITCH_LEFT : MCP_SWITCH_RIGHT;
}

} // extern "C"
