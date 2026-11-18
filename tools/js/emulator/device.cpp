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
// (fbh_state), read from the firmware's components and from the LVGL
// objects they made (records.cpp), plus where LVGL laid each widget out.
// The pixels themselves are display.cpp's.
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

// The components keep in private members which LVGL object is theirs and
// what they hold while no widget exists (the text slots of an item whose
// page is not shown, a list's rows, page numbers). LVGL does not know these,
// so the emulator reads them, here only, to report the state; what a widget
// shows is read from LVGL. The firmware sources stay as they are.
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
#include "emu_ui.h"
#include "records.h"
#include "ui/ui_button.h"
#include "media/media_store.h"
#include "ui/MediaImage.h"

extern "C" {
#include "../../lib/vendored/sts3x/sts3x.h"
#include "../../lib/vendored/stscommon/sensirion_i2c.h"
#include "ui/fonts/fonts.h"
#include "ui/ui_style.h"
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
    if (lv_obj_t* box = item.box) {
        lv_obj_t* caption = fbe_box_part(box, FBE_BOX_CAPTION);
        lv_obj_t* value = fbe_box_part(box, FBE_BOX_VALUE);
        lv_obj_t* unit = fbe_box_part(box, FBE_BOX_UNIT);
        json.beginObject();
        // As the firmware placed it (ui_box_update): position and width in
        // pixels, with the alignment they are relative to.
        json.key("x").value(static_cast<int>(lv_obj_get_style_x(box, LV_PART_MAIN)));
        json.key("y").value(static_cast<int>(lv_obj_get_style_y(box, LV_PART_MAIN)));
        json.key("width").value(static_cast<int>(lv_obj_get_style_width(box, LV_PART_MAIN)));
        json.key("align").value(static_cast<int>(lv_obj_get_style_align(box, LV_PART_MAIN)));
        json.key("underline").value(lv_obj_get_style_border_width(box, LV_PART_MAIN) > 0);
        lv_style_value_t pad;
        json.key("captionPad");
        if (caption && lv_obj_get_local_style_prop(caption, LV_STYLE_PAD_BOTTOM, &pad, 0) == LV_STYLE_RES_FOUND) {
            json.value(static_cast<int>(pad.num));
        } else {
            json.null();
        }
        json.key("boxPad");
        if (lv_obj_get_local_style_prop(box, LV_STYLE_PAD_BOTTOM, &pad, 0) == LV_STYLE_RES_FOUND) {
            json.value(static_cast<int>(pad.num));
        } else {
            json.null();
        }
        json.key("caption");
        emu::writeText(json, caption, slotText(item.captionSlot_, item.label), false,
                       fbe_box_part_font(box, FBE_BOX_CAPTION));
        json.key("value");
        emu::writeText(json, value, slotText(item.valueSlot_, std::string()), false,
                       fbe_box_part_font(box, FBE_BOX_VALUE));
        json.key("unit");
        emu::writeText(json, unit, slotText(item.unitSlot_, item.unit), false, fbe_box_part_font(box, FBE_BOX_UNIT));
        json.key("icon");
        emu::writeIcon(json, fbe_box_part(box, FBE_BOX_ICON));
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
    const auto* b = button.button ? static_cast<const button_dsc_t*>(lv_obj_get_user_data(button.button)) : nullptr;
    if (b) {
        json.beginObject();
        json.key("index").value(static_cast<int>(
            reinterpret_cast<intptr_t>(lv_display_get_user_data(lv_obj_get_display(button.button)))));
        // A text in one format is its label; one in several formats a row of
        // labels beside it (ui_button.c set_markup).
        json.key("label");
        // The text's own font: the label's local one (ui_button_set_fonts: an item font, or a
        // named style with one of font / size), else its base (ui_button_set_base_fonts: the
        // item's, else the named style's, else the element style's, Button::applyFonts).
        emu::writeText(json, b->rich_text ? b->rich_text : b->text, slotText(button.labelSlot_, button.label),
                       b->rich_text != nullptr, b->text_font ? b->text_font : fonts_get(b->text_face, b->text_px));
        json.key("topLabel");
        emu::writeText(json, b->rich_title ? b->rich_title : b->title,
                       slotText(button.topLabelSlot_, button.topLabel), b->rich_title != nullptr,
                       b->title_font ? b->title_font : fonts_get(b->title_face, b->title_px));
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
    if (lv_obj_t* r = list.roller_) {
        const lv_font_t* font = lv_obj_get_style_text_font(r, LV_PART_MAIN);
        lv_obj_update_layout(r);  // the height follows the font, the line space and the row count
        json.beginObject();
        json.key("x").value(static_cast<int>(lv_obj_get_style_x(r, LV_PART_MAIN)));
        json.key("y").value(static_cast<int>(lv_obj_get_style_y(r, LV_PART_MAIN)));
        json.key("width").value(static_cast<int>(lv_obj_get_style_width(r, LV_PART_MAIN)));
        json.key("height").value(static_cast<int>(lv_obj_get_height(r)));
        json.key("rows").value(static_cast<int>(list.rows_));
        json.key("selected").value(static_cast<int>(lv_roller_get_selected(r)));
        json.key("font");
        emu::writeFont(json, font);
        // The selected row's own font, as LVGL resolves it (LV_PART_SELECTED: a local font, its
        // named style, list.selected, else the rows').
        json.key("selectedFont");
        emu::writeFont(json, lv_obj_get_style_text_font(r, LV_PART_SELECTED));
        json.key("lineSpace").value(static_cast<int>(lv_obj_get_style_text_line_space(r, LV_PART_MAIN)));
        json.key("color").value(emu::hexColor(lv_obj_get_style_text_color(r, LV_PART_MAIN)));
        json.key("selectedBg").value(emu::hexColor(lv_obj_get_style_bg_color(r, LV_PART_SELECTED)));
        json.key("selectedColor").value(emu::hexColor(lv_obj_get_style_text_color(r, LV_PART_SELECTED)));
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
    emu::writeIcon(json, image.image_);
    json.endObject();
    send(FBH_STATE_IMAGE, "image" + std::to_string(index), json.str());
}

// The analog clock: "shown" while its page is shown (only the first clock of
// a config is drawn). Its hands are the device's local time.
void sendClock(freebutton::Clock& clock, const int index) {
    const bool drawn = clock.built_ && fbe_clock_container() != nullptr;
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

// Where the widgets of one display are, as LVGL laid them out (display: 0 =
// the main display, 1 + i = key display i). "kind" and the id fields are
// those of the widget's own record; "visible": drawn now (not hidden itself
// or by a parent).
void layoutEntry(Json& json, const char* kind, const char* idKey, const int id, lv_obj_t* obj, const int page) {
    json.beginObject();
    json.key("kind").value(kind);
    json.key(idKey).value(id);
    emu::writeCoords(json, obj);
    json.key("page").value(page);
    json.key("visible").value(lv_obj_is_visible(obj));
    json.endObject();
}

void sendLayout() {
    auto& lvgl = FreeButton::getLvglDisplay();
    if (!lvgl.isInitialized()) return;
    {
        lv_obj_update_layout(lvgl.getMainScreen());
        Json json;
        json.beginObject();
        json.key("display").value(0);
        json.key("rects").beginArray();
        configManager.forEachDisplayItem([&json](freebutton::DisplayItem& item) {
            if (item.box) layoutEntry(json, "item", "id", static_cast<int>(item.id), item.box, item.page);
        });
        for (size_t i = 0;; ++i) {
            freebutton::List* list = configManager.findList(i);
            if (!list) break;
            if (list->roller_) layoutEntry(json, "list", "index", static_cast<int>(i), list->roller_, list->page_);
        }
        for (size_t i = 0;; ++i) {
            freebutton::ImageItem* image = configManager.findImage(i);
            if (!image) break;
            if (image->image_) layoutEntry(json, "image", "index", static_cast<int>(i), image->image_, image->page_);
        }
        int clock = 0;
        configManager.forEachClock([&json, &clock](freebutton::Clock& c) {
            // Only the first clock of a config is drawn (ui_clock.c has one).
            if (c.built_ && fbe_clock_container()) layoutEntry(json, "clock", "index", clock, fbe_clock_container(), c.page);
            ++clock;
        });
        json.endArray();
        json.endObject();
        send(FBH_STATE_LAYOUT, "layout0", json.str());
    }
    const size_t smalls = lvgl.getSmallDisplayCount();
    for (size_t d = 0; d < smalls; ++d) {
        lv_display_t* display = lvgl.getSmallDisplay(static_cast<int>(d));
        Json json;
        json.beginObject();
        json.key("display").value(static_cast<int>(d + 1));
        json.key("rects").beginArray();
        int order = 0;
        configManager.forEachButton([&](freebutton::Button& button) {
            if (button.button && lv_obj_get_display(button.button) == display) {
                lv_obj_update_layout(button.button);
                layoutEntry(json, "key", "order", order, button.button, static_cast<int>(button.page_));
            }
            ++order;
        });
        json.endArray();
        json.endObject();
        send(FBH_STATE_LAYOUT, "layout" + std::to_string(d + 1), json.str());
    }
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
    sendLayout();
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
    // The LVGL task (LvglDisplay::task): the pending renders, then LVGL's
    // timers (refresh, animations, the clock, the font reload poll).
    FreeButton::getLvglDisplay().task();
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

// One turn of the firmware's LVGL task (every 10 ms on the device; here when
// fbe_display_wait() says a timer is due), then the records.
FBE_EXPORT(fbe_display_task) void fbe_display_task(void) {
    if (!g_booted) return;
    FreeButton::getLvglDisplay().task();
    syncState();
}

// Draws what is pending now (the firmware's screenshot path), then the records.
FBE_EXPORT(fbe_display_render) void fbe_display_render(void) {
    if (!g_booted) return;
    fbe_display_refresh_now();
    syncState();
}

// A file of the media store changed on the host (bucket 0 fonts, 1 images):
// what the firmware does after an upload or a delete (core/S3Server.cpp). The
// fonts are read again 1.5 s after the last change; the images at the next
// turn of the LVGL task.
FBE_EXPORT(fbe_media_changed) void fbe_media_changed(const int bucket, const char* key) {
    fbe_media_forget_key(bucket, key);
    if (!g_booted) return;
    if (bucket == MEDIA_BUCKET_FONTS) fonts_request_reload();
    else if (bucket == MEDIA_BUCKET_IMAGES) freebutton::ui::mediaImageNotifyChanged();
}

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

    // setup(): LVGL before the configuration (the fonts are open when the
    // config names its faces; the widgets are made on the screens).
    if (!FreeButton::initializeLvgl(FreeButton::getDisplayManager())) {
        ESP_LOGE(TAG, "LVGL initialization failed!");
        return -2;
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
