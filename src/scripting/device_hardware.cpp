//
// Device Hardware Abstraction Layer
// C-compatible interface to HardwareInitializer for device topology discovery
//
// The fb object tree is built entirely in C at init time — no JS source
// parsing, no bytecode in heap. Prototype methods are JS_NewCFunctionParams
// closures that dispatch through js_fb_device_proxy in freebutton_device.c.
//

#include <Arduino.h>
#include "device_hardware.h"
#include "core/HardwareInitializer.h"
#include "drivers/ButtonLed.h"
#include <esp_log.h>

extern "C" {
    #include <mquickjs.h>
}

static const char* TAG = "DeviceHardware";

extern "C" {

int device_hw_get_connector_type(int connector) {
    auto type = FreeButton::getHardwareInitializer().getScanner().getConnectorType(connector);
    if (!type.has_value()) return 0; // EMPTY
    return static_cast<int>(type.value());
}

int device_hw_get_main_connector(void) {
    return FreeButton::getHardwareInitializer().getTft320Bar();
}

int device_hw_led_index(int buttonPos, int isWall) {
    // Same formula as ButtonLed::calculateLedIndex (buttonPos is 1-based)
    const int row = buttonPos / 4;
    const bool isRight = (buttonPos % 4) >= 2;
    return row * 4 + (isRight ? 2 : 0) + (isWall ? 1 : 0);
}

//
// Build the fb object tree entirely in C.
//
// Creates shared prototype objects with methods that are C closures
// (JS_NewCFunctionParams → js_fb_device_proxy). Instance objects use
// Object.create(proto) + _p property. Zero JS parsing overhead.
//
// Memory layout:
//   3 prototype objects (~320 bytes, shared)
//   8 closure objects for methods (~128 bytes, shared on prototypes)
//   Per-instance: ~32 bytes each (Object.create + _p integer)
//   Main + 1 bar ≈ 1.0 KB total heap
//
void device_hw_init_fb(void* ctx) {
    JSContext* c = static_cast<JSContext*>(ctx);

    // Method IDs matching the enum in freebutton_device.c
    enum {
        LED_SET_COLOR = 0, LED_ON = 1, LED_OFF = 2,
        BTN_ON_CLICK = 3, BTN_ON_LONG_PRESS = 4, BTN_ON_RELEASE = 5,
        DISP_SET_LABEL = 6, DISP_SET_TOP_LABEL = 7,
    };

    const int P = FB_PROXY_FUNC_IDX;

    // --- Create shared prototype objects ---

    JSValue led_proto = JS_NewObject(c);
    JS_SetPropertyStr(c, led_proto, "setColor", JS_NewCFunctionParams(c, P, JS_NewInt32(c, LED_SET_COLOR)));
    JS_SetPropertyStr(c, led_proto, "on",       JS_NewCFunctionParams(c, P, JS_NewInt32(c, LED_ON)));
    JS_SetPropertyStr(c, led_proto, "off",      JS_NewCFunctionParams(c, P, JS_NewInt32(c, LED_OFF)));

    JSValue btn_proto = JS_NewObject(c);
    JS_SetPropertyStr(c, btn_proto, "onClick",     JS_NewCFunctionParams(c, P, JS_NewInt32(c, BTN_ON_CLICK)));
    JS_SetPropertyStr(c, btn_proto, "onLongPress", JS_NewCFunctionParams(c, P, JS_NewInt32(c, BTN_ON_LONG_PRESS)));
    JS_SetPropertyStr(c, btn_proto, "onRelease",   JS_NewCFunctionParams(c, P, JS_NewInt32(c, BTN_ON_RELEASE)));

    JSValue disp_proto = JS_NewObject(c);
    JS_SetPropertyStr(c, disp_proto, "setLabel",    JS_NewCFunctionParams(c, P, JS_NewInt32(c, DISP_SET_LABEL)));
    JS_SetPropertyStr(c, disp_proto, "setTopLabel", JS_NewCFunctionParams(c, P, JS_NewInt32(c, DISP_SET_TOP_LABEL)));

    // --- Get Object.create for prototype inheritance ---

    JSValue global = JS_GetGlobalObject(c);
    JSValue object_ctor = JS_GetPropertyStr(c, global, "Object");
    JSValue create_func = JS_GetPropertyStr(c, object_ctor, "create");

    // Helper: Object.create(proto) with _p = pos
    auto makeInst = [&](JSValue proto, int p) -> JSValue {
        JS_PushArg(c, create_func);
        JS_PushArg(c, object_ctor);  // this = Object
        JS_PushArg(c, proto);        // arg: prototype
        JSValue inst = JS_Call(c, 1);
        JS_SetPropertyStr(c, inst, "_p", JS_NewInt32(c, p));
        return inst;
    };

    // Helper: build a "side" {led, backLed, button, display}
    auto makeSide = [&](int buttonPos) -> JSValue {
        int frontIdx = device_hw_led_index(buttonPos, 0);
        int wallIdx  = device_hw_led_index(buttonPos, 1);

        JSValue side = JS_NewObject(c);
        JS_SetPropertyStr(c, side, "led",     makeInst(led_proto, frontIdx));
        JS_SetPropertyStr(c, side, "backLed", makeInst(led_proto, wallIdx));
        JS_SetPropertyStr(c, side, "button",  makeInst(btn_proto, buttonPos));
        JS_SetPropertyStr(c, side, "display", makeInst(disp_proto, buttonPos));
        return side;
    };

    // --- Build the fb tree from hardware topology ---

    JSValue fb = JS_NewObject(c);
    JSValue bars = JS_NewArray(c, 0);
    int barCount = 0;

    int mainConn = device_hw_get_main_connector();

    for (int conn = 0; conn < 4; conn++) {
        int type = device_hw_get_connector_type(conn);
        if (type == 0) continue;  // EMPTY

        int leftPos  = conn * 2 + 1;  // 1-based
        int rightPos = conn * 2 + 2;

        if (type == 2 && mainConn == conn) {
            // LARGE connector → fb.main
            JSValue main_obj = JS_NewObject(c);
            JS_SetPropertyStr(c, main_obj, "display", JS_GetPropertyStr(c, global, "display"));
            JS_SetPropertyStr(c, main_obj, "left",  makeSide(leftPos));
            JS_SetPropertyStr(c, main_obj, "right", makeSide(rightPos));
            JS_SetPropertyStr(c, fb, "main", main_obj);
        } else if (type == 1) {
            // BAR connector → fb.bars[n]
            JSValue bar = JS_NewObject(c);
            JS_SetPropertyStr(c, bar, "left",  makeSide(leftPos));
            JS_SetPropertyStr(c, bar, "right", makeSide(rightPos));
            JS_SetPropertyUint32(c, bars, barCount++, bar);
        }
    }

    JS_SetPropertyStr(c, fb, "bars", bars);
    JS_SetPropertyStr(c, global, "fb", fb);

    ESP_LOGI(TAG, "fb object tree built in C (%d bars, main=%d)", barCount, mainConn);
}

} // extern "C"
