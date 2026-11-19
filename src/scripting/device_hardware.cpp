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
#include "components/ConfigManager.h"

extern freebutton::ConfigManager configManager;
#include "drivers/ButtonLed.h"
#include <esp_log.h>

extern "C" {
    #include <mquickjs.h>
    #include "js/fb_method_ids.h"
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

//
// Build the fb object tree entirely in C.
//
// Creates shared prototype objects with methods that are C closures
// (JS_NewCFunctionParams → js_fb_device_proxy). Instance objects use
// Object.create(proto) + _p property. Zero JS parsing overhead.
//
// Memory layout:
//   3 prototype objects (~320 bytes, shared)
//   one closure object per method (~16 bytes each, shared on prototypes;
//   js/fb_method_ids.h lists them)
//   Per-instance: ~32 bytes each (Object.create + _p integer)
//   Main + 1 bar ≈ 1.0 KB total heap
//
void device_hw_init_fb(void* ctx) {
    JSContext* c = static_cast<JSContext*>(ctx);

    const int P = FB_PROXY_FUNC_IDX;

    // GC safety: mquickjs has a moving GC, and every JS_New* / JS_Call /
    // JS_SetPropertyStr may run it. A JSValue held in a plain C local across
    // such a call can be left pointing at the old location (this tree used
    // to be built on an empty heap where no GC ran; it is now built after the
    // user bytecode is loaded). So every value used after another allocation
    // lives in a JSGCRef (pushed/popped LIFO), each new value is created in
    // its own statement, and objects are read through the ref only after it.
    // (JS_SetPropertyStr protects its own arguments.)

    // Store a fresh fb_proxy method closure on *obj.
    auto setMethod = [&](JSValue* obj, const char* name, int id) {
        JSValue fn = JS_NewCFunctionParams(c, P, JS_NewInt32(c, id));
        JS_SetPropertyStr(c, *obj, name, fn);
    };

    // --- Create shared prototype objects ---

    JSGCRef led_proto_ref, btn_proto_ref, disp_proto_ref, disp_items_proto_ref;
    JSValue* led_proto = JS_PushGCRef(c, &led_proto_ref);
    *led_proto = JS_NewObject(c);
    setMethod(led_proto, "setColor", FB_METHOD_LED_SET_COLOR);
    setMethod(led_proto, "on",       FB_METHOD_LED_ON);
    setMethod(led_proto, "off",      FB_METHOD_LED_OFF);

    JSValue* btn_proto = JS_PushGCRef(c, &btn_proto_ref);
    *btn_proto = JS_NewObject(c);
    setMethod(btn_proto, "onClick",     FB_METHOD_BTN_ON_CLICK);
    setMethod(btn_proto, "onLongPress", FB_METHOD_BTN_ON_LONG_PRESS);
    setMethod(btn_proto, "onRelease",   FB_METHOD_BTN_ON_RELEASE);

    JSValue* disp_proto = JS_PushGCRef(c, &disp_proto_ref);
    *disp_proto = JS_NewObject(c);
    setMethod(disp_proto, "setLabel",    FB_METHOD_DISP_SET_LABEL);
    setMethod(disp_proto, "setTopLabel", FB_METHOD_DISP_SET_TOP_LABEL);
    setMethod(disp_proto, "getLabel",    FB_METHOD_DISP_GET_LABEL);
    setMethod(disp_proto, "getTopLabel", FB_METHOD_DISP_GET_TOP_LABEL);
    setMethod(disp_proto, "setIcon",     FB_METHOD_DISP_SET_ICON);
    // The style methods are always there: the styles of the kinds always
    // exist, and a config may add styles.
    setMethod(disp_proto, "setStyle",    FB_METHOD_DISP_SET_STYLE);
    setMethod(disp_proto, "addStyle",    FB_METHOD_DISP_ADD_STYLE);
    setMethod(disp_proto, "removeStyle", FB_METHOD_DISP_REMOVE_STYLE);

    JSValue* disp_items_proto = JS_PushGCRef(c, &disp_items_proto_ref);
    *disp_items_proto = JS_NewObject(c);
    setMethod(disp_items_proto, "create",      FB_METHOD_DITEM_CREATE);
    setMethod(disp_items_proto, "remove",      FB_METHOD_DITEM_REMOVE);
    setMethod(disp_items_proto, "count",       FB_METHOD_DITEM_COUNT);
    setMethod(disp_items_proto, "get",         FB_METHOD_DITEM_GET);
    setMethod(disp_items_proto, "getAll",      FB_METHOD_DITEM_GET_ALL);
    setMethod(disp_items_proto, "setLabel",    FB_METHOD_DITEM_SET_LABEL);
    setMethod(disp_items_proto, "setValue",    FB_METHOD_DITEM_SET_VALUE);
    setMethod(disp_items_proto, "setUnit",     FB_METHOD_DITEM_SET_UNIT);
    setMethod(disp_items_proto, "setIcon",     FB_METHOD_DITEM_SET_ICON);
    setMethod(disp_items_proto, "setStyle",    FB_METHOD_DITEM_SET_STYLE);
    setMethod(disp_items_proto, "addStyle",    FB_METHOD_DITEM_ADD_STYLE);
    setMethod(disp_items_proto, "removeStyle", FB_METHOD_DITEM_REMOVE_STYLE);
    setMethod(disp_items_proto, "setPosition", FB_METHOD_DITEM_SET_POSITION);
    setMethod(disp_items_proto, "setPage",     FB_METHOD_DITEM_SET_PAGE);
    setMethod(disp_items_proto, "showPage",    FB_METHOD_DITEM_SHOW_PAGE);
    setMethod(disp_items_proto, "getShownPage", FB_METHOD_DITEM_GET_SHOWN_PAGE);

    // --- Get Object.create for prototype inheritance ---

    JSGCRef global_ref, object_ctor_ref, create_func_ref;
    JSValue* global = JS_PushGCRef(c, &global_ref);
    *global = JS_GetGlobalObject(c);
    JSValue* object_ctor = JS_PushGCRef(c, &object_ctor_ref);
    *object_ctor = JS_GetPropertyStr(c, *global, "Object");
    JSValue* create_func = JS_PushGCRef(c, &create_func_ref);
    *create_func = JS_GetPropertyStr(c, *object_ctor, "create");

    // Helper: Object.create(proto) with _p = pos
    // mquickjs JS_Call stack order is args-reversed, then func, then this_obj
    // (see JS_Call docs in mquickjs.c). Object.create takes one arg (proto),
    // so push: arg[0]=proto, func=create_func, this_obj=object_ctor.
    // Values on the JS stack are GC roots.
    auto makeInst = [&](const JSValue* proto, int p) -> JSValue {
        JSGCRef inst_ref;
        JSValue* inst = JS_PushGCRef(c, &inst_ref);
        JS_PushArg(c, *proto);        // arg[0]: prototype
        JS_PushArg(c, *create_func);  // func: Object.create
        JS_PushArg(c, *object_ctor);  // this: Object
        *inst = JS_Call(c, 1);
        JS_SetPropertyStr(c, *inst, "_p", JS_NewInt32(c, p));
        return JS_PopGCRef(c, &inst_ref);
    };

    // *obj.name = value (value created first, *obj read afterwards).
    auto setInst = [&](JSValue* obj, const char* name, const JSValue* proto, int p) {
        JSValue inst = makeInst(proto, p);
        JS_SetPropertyStr(c, *obj, name, inst);
    };

    // Helper: build a "side" {led?, backLed?, button, display}.
    // ledFront / ledWall are strip indices, or -1 to omit (main has no LEDs).
    auto makeSide = [&](int buttonPos, int ledFront, int ledWall) -> JSValue {
        JSGCRef side_ref;
        JSValue* side = JS_PushGCRef(c, &side_ref);
        *side = JS_NewObject(c);
        if (ledFront >= 0)
            setInst(side, "led", led_proto, ledFront);
        if (ledWall >= 0)
            setInst(side, "backLed", led_proto, ledWall);
        setInst(side, "button",  btn_proto,  buttonPos);
        setInst(side, "display", disp_proto, buttonPos);
        return JS_PopGCRef(c, &side_ref);
    };

    // --- Build the fb tree from hardware topology ---

    JSGCRef fb_ref, bars_ref;
    JSValue* fb = JS_PushGCRef(c, &fb_ref);
    *fb = JS_NewObject(c);
    JSValue* bars = JS_PushGCRef(c, &bars_ref);
    *bars = JS_NewArray(c, 0);
    int barCount = 0;

    int mainConn = device_hw_get_main_connector();

    for (int conn = 0; conn < 4; conn++) {
        int type = device_hw_get_connector_type(conn);
        if (type == 0) continue;  // EMPTY

        int leftPos  = conn * 2 + 1;  // 1-based
        int rightPos = conn * 2 + 2;

        if (type == 2 && mainConn == conn) {
            // LARGE connector → fb.main. The main display has NO LEDs, so its
            // sides expose only button + display (no led/backLed).
            JSGCRef main_ref;
            JSValue* main_obj = JS_PushGCRef(c, &main_ref);
            *main_obj = JS_NewObject(c);
            setInst(main_obj, "display", disp_items_proto, 0);
            JSValue left = makeSide(leftPos, -1, -1);
            JS_SetPropertyStr(c, *main_obj, "left", left);
            JSValue right = makeSide(rightPos, -1, -1);
            JS_SetPropertyStr(c, *main_obj, "right", right);
            // fb.main.list: only with lists in config.json (one object, seven
            // closures and their property names: ~400 B of JS heap measured
            // with six).
            if (configManager.findList(0) != nullptr) {
                JSGCRef list_ref;
                JSValue* list_obj = JS_PushGCRef(c, &list_ref);
                *list_obj = JS_NewObject(c);
                setMethod(list_obj, "next",        FB_METHOD_LIST_NEXT);
                setMethod(list_obj, "prev",        FB_METHOD_LIST_PREV);
                setMethod(list_obj, "select",      FB_METHOD_LIST_SELECT);
                setMethod(list_obj, "getSelected", FB_METHOD_LIST_GET_SELECTED);
                setMethod(list_obj, "count",       FB_METHOD_LIST_COUNT);
                setMethod(list_obj, "setItems",    FB_METHOD_LIST_SET_ITEMS);
                setMethod(list_obj, "setStyle",    FB_METHOD_LIST_SET_STYLE);
                JS_SetPropertyStr(c, *main_obj, "list", *list_obj);
                JS_PopGCRef(c, &list_ref);
            }
            // fb.main.image: only with images in config.json (one object,
            // two closures).
            if (configManager.findImage(0) != nullptr) {
                JSGCRef image_ref;
                JSValue* image_obj = JS_PushGCRef(c, &image_ref);
                *image_obj = JS_NewObject(c);
                setMethod(image_obj, "setIcon", FB_METHOD_IMAGE_SET_ICON);
                setMethod(image_obj, "setSrc",  FB_METHOD_IMAGE_SET_SRC);
                JS_SetPropertyStr(c, *main_obj, "image", *image_obj);
                JS_PopGCRef(c, &image_ref);
            }
            JS_SetPropertyStr(c, *fb, "main", *main_obj);
            JS_PopGCRef(c, &main_ref);
        } else if (type == 1) {
            // BAR connector → fb.bars[n]. Each bar owns a contiguous block of 4
            // strip LEDs, packed by bar order along the chain (main/empty
            // connectors contribute none). Within a bar:
            //   left.led = base+0, left.backLed = base+1,
            //   right.led = base+2, right.backLed = base+3.
            const int base = barCount * 4;
            JSGCRef bar_ref;
            JSValue* bar = JS_PushGCRef(c, &bar_ref);
            *bar = JS_NewObject(c);
            JSValue left = makeSide(leftPos, base + 0, base + 1);
            JS_SetPropertyStr(c, *bar, "left", left);
            JSValue right = makeSide(rightPos, base + 2, base + 3);
            JS_SetPropertyStr(c, *bar, "right", right);
            JS_SetPropertyUint32(c, *bars, barCount, *bar);
            JS_PopGCRef(c, &bar_ref);
            barCount++;
        }
    }

    JS_SetPropertyStr(c, *fb, "bars", *bars);
    // fb.button(name): a button by its config.json name (one closure, ~16 B;
    // the names stay in the firmware's name table, see NameTable.h).
    setMethod(fb, "button", FB_METHOD_BUTTON);
    // fb.style.set(style, property, value)
    {
        JSGCRef style_ref;
        JSValue* style_obj = JS_PushGCRef(c, &style_ref);
        *style_obj = JS_NewObject(c);
        setMethod(style_obj, "set", FB_METHOD_STYLE_SET);
        JS_SetPropertyStr(c, *fb, "style", *style_obj);
        JS_PopGCRef(c, &style_ref);
    }
    JS_SetPropertyStr(c, *global, "fb", *fb);

    // LIFO
    JS_PopGCRef(c, &bars_ref);
    JS_PopGCRef(c, &fb_ref);
    JS_PopGCRef(c, &create_func_ref);
    JS_PopGCRef(c, &object_ctor_ref);
    JS_PopGCRef(c, &global_ref);
    JS_PopGCRef(c, &disp_items_proto_ref);
    JS_PopGCRef(c, &disp_proto_ref);
    JS_PopGCRef(c, &btn_proto_ref);
    JS_PopGCRef(c, &led_proto_ref);

    ESP_LOGI(TAG, "fb object tree built in C (%d bars, main=%d)", barCount, mainConn);
}

} // extern "C"
