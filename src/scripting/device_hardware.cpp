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
        DITEM_CREATE = 8, DITEM_REMOVE = 9, DITEM_COUNT = 10,
        DITEM_GET = 11, DITEM_GET_ALL = 12, DITEM_SET_LABEL = 13,
        DITEM_SET_VALUE = 14, DITEM_SET_UNIT = 15, DITEM_SET_POSITION = 16,
        DITEM_SET_PAGE = 17,
        DISP_GET_LABEL = 18, DISP_GET_TOP_LABEL = 19,
        DITEM_SHOW_PAGE = 20, DITEM_GET_SHOWN_PAGE = 21,
        FB_KEY = 22,
        LIST_NEXT = 23, LIST_PREV = 24, LIST_SELECT = 25,
        LIST_GET_SELECTED = 26, LIST_COUNT = 27, LIST_SET_ITEMS = 28,
    };

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
    setMethod(led_proto, "setColor", LED_SET_COLOR);
    setMethod(led_proto, "on",       LED_ON);
    setMethod(led_proto, "off",      LED_OFF);

    JSValue* btn_proto = JS_PushGCRef(c, &btn_proto_ref);
    *btn_proto = JS_NewObject(c);
    setMethod(btn_proto, "onClick",     BTN_ON_CLICK);
    setMethod(btn_proto, "onLongPress", BTN_ON_LONG_PRESS);
    setMethod(btn_proto, "onRelease",   BTN_ON_RELEASE);

    JSValue* disp_proto = JS_PushGCRef(c, &disp_proto_ref);
    *disp_proto = JS_NewObject(c);
    setMethod(disp_proto, "setLabel",    DISP_SET_LABEL);
    setMethod(disp_proto, "setTopLabel", DISP_SET_TOP_LABEL);
    setMethod(disp_proto, "getLabel",    DISP_GET_LABEL);
    setMethod(disp_proto, "getTopLabel", DISP_GET_TOP_LABEL);

    JSValue* disp_items_proto = JS_PushGCRef(c, &disp_items_proto_ref);
    *disp_items_proto = JS_NewObject(c);
    setMethod(disp_items_proto, "create",      DITEM_CREATE);
    setMethod(disp_items_proto, "remove",      DITEM_REMOVE);
    setMethod(disp_items_proto, "count",       DITEM_COUNT);
    setMethod(disp_items_proto, "get",         DITEM_GET);
    setMethod(disp_items_proto, "getAll",      DITEM_GET_ALL);
    setMethod(disp_items_proto, "setLabel",    DITEM_SET_LABEL);
    setMethod(disp_items_proto, "setValue",    DITEM_SET_VALUE);
    setMethod(disp_items_proto, "setUnit",     DITEM_SET_UNIT);
    setMethod(disp_items_proto, "setPosition", DITEM_SET_POSITION);
    setMethod(disp_items_proto, "setPage",     DITEM_SET_PAGE);
    setMethod(disp_items_proto, "showPage",    DITEM_SHOW_PAGE);
    setMethod(disp_items_proto, "getShownPage", DITEM_GET_SHOWN_PAGE);

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
            // fb.main.list: only with lists in config.json (one object and
            // six closures, ~120 B of JS heap).
            if (configManager.findList(0) != nullptr) {
                JSGCRef list_ref;
                JSValue* list_obj = JS_PushGCRef(c, &list_ref);
                *list_obj = JS_NewObject(c);
                setMethod(list_obj, "next",        LIST_NEXT);
                setMethod(list_obj, "prev",        LIST_PREV);
                setMethod(list_obj, "select",      LIST_SELECT);
                setMethod(list_obj, "getSelected", LIST_GET_SELECTED);
                setMethod(list_obj, "count",       LIST_COUNT);
                setMethod(list_obj, "setItems",    LIST_SET_ITEMS);
                JS_SetPropertyStr(c, *main_obj, "list", *list_obj);
                JS_PopGCRef(c, &list_ref);
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
    // fb.key(name): a key by its config.json name (one closure, ~16 B; the
    // names stay in the firmware's name table, see NameTable.h).
    setMethod(fb, "key", FB_KEY);
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
