/*
 * FreeButton Standard Library for MicroQuickJS
 *
 * This file defines a custom JavaScript stdlib that includes:
 * - Full ES5 JavaScript standard library (from mqjs_stdlib.c)
 * - FreeButton sensor and MQTT APIs (as globals)
 * - fb proxy dispatcher (for C-built fb object tree)
 *
 * Build process:
 * 1. Compile this file with mquickjs_build.c as a host executable
 * 2. Run the executable with -m32 flag to generate 32-bit headers
 * 3. Output is redirected to freebutton_stdlib.h
 * 4. The firmware includes this header and uses &js_stdlib
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mquickjs_build.h"

/*
 * FreeButton Sensor API
 *
 * JavaScript bindings for sensor reading and monitoring:
 *   sensor.count()               - Get number of available sensors
 *   sensor.getValue(sensorId)    - Read current sensor value
 *   sensor.getType(sensorId)     - Get sensor type name
 *   sensor.getInfo(sensorId)     - Get sensor information object
 *   sensor.forEach(callback)     - Iterate sensors: callback(id, type) per sensor
 *   sensor.onChange(sensorId, callback) - Register change event handler
 *
 * The actual function implementations are in freebutton_sensor.c which is
 * compiled separately as part of the ESP32 firmware build.
 */
static const JSPropDef js_freebutton_sensor[] = {
    JS_CFUNC_DEF("count", 0, js_freebutton_sensor_count),
    JS_CFUNC_DEF("getValue", 1, js_freebutton_sensor_getValue),
    JS_CFUNC_DEF("getType", 1, js_freebutton_sensor_getType),
    JS_CFUNC_DEF("getInfo", 1, js_freebutton_sensor_getInfo),
    JS_CFUNC_DEF("forEach", 1, js_freebutton_sensor_forEach),
    JS_CFUNC_DEF("onChange", 2, js_freebutton_sensor_onChange),
    JS_PROP_END,
};

static const JSClassDef js_freebutton_sensor_obj =
    JS_OBJECT_DEF("Sensor", js_freebutton_sensor);

/*
 * FreeButton MQTT API
 *
 * JavaScript bindings for MQTT publish/subscribe:
 *   mqtt.publish(brokerId, topic, payload[, qos, retain])  - Publish message
 *   mqtt.subscribe(brokerId, topic, callback[, qos])       - Subscribe to topic
 *   mqtt.unsubscribe(brokerId, topic)                      - Unsubscribe from topic
 *   mqtt.onConnect(brokerId, callback)                     - Register connect callback
 *   mqtt.onDisconnect(brokerId, callback)                  - Register disconnect callback
 *   mqtt.isConnected(brokerId)                             - Check connection status
 *   mqtt.getBrokerName(brokerId)                           - Get broker name
 *   mqtt.getBrokerCount()                                  - Get number of brokers
 *
 * The actual function implementations are in freebutton_mqtt.c which is
 * compiled separately as part of the ESP32 firmware build.
 */
static const JSPropDef js_freebutton_mqtt[] = {
    JS_CFUNC_DEF("getBrokerCount", 0, js_freebutton_mqtt_getBrokerCount),
    JS_CFUNC_DEF("getBrokerName", 1, js_freebutton_mqtt_getBrokerName),
    JS_CFUNC_DEF("isConnected", 1, js_freebutton_mqtt_isConnected),
    JS_CFUNC_DEF("publish", 5, js_freebutton_mqtt_publish),
    JS_CFUNC_DEF("subscribe", 4, js_freebutton_mqtt_subscribe),
    JS_CFUNC_DEF("unsubscribe", 2, js_freebutton_mqtt_unsubscribe),
    JS_CFUNC_DEF("onConnect", 2, js_freebutton_mqtt_onConnect),
    JS_CFUNC_DEF("onDisconnect", 2, js_freebutton_mqtt_onDisconnect),
    JS_PROP_END,
};

static const JSClassDef js_freebutton_mqtt_obj =
    JS_OBJECT_DEF("MQTT", js_freebutton_mqtt);

/*
 * FreeButton System API
 *
 * JavaScript bindings for system information:
 *   system.uptime()    - Get device uptime in seconds
 *   system.freeHeap()  - Get free heap memory in bytes
 *   system.version()   - Get firmware version string
 *   system.id()        - Get the device id
 *   system.name()      - Get the device name
 *   system.model()     - Get the hardware model
 *
 * The actual function implementations are in freebutton_system.c which is
 * compiled separately as part of the ESP32 firmware build.
 */
static const JSPropDef js_freebutton_system[] = {
    JS_CFUNC_DEF("uptime", 0, js_freebutton_system_uptime),
    JS_CFUNC_DEF("freeHeap", 0, js_freebutton_system_freeHeap),
    JS_CFUNC_DEF("version", 0, js_freebutton_system_version),
    JS_CFUNC_DEF("id", 0, js_freebutton_system_id),
    JS_CFUNC_DEF("name", 0, js_freebutton_system_name),
    JS_CFUNC_DEF("model", 0, js_freebutton_system_model),
    JS_PROP_END,
};

static const JSClassDef js_freebutton_system_obj =
    JS_OBJECT_DEF("System", js_freebutton_system);

/*
 * Include the full standard library
 *
 * We define CONFIG_FREEBUTTON_SENSOR, CONFIG_FREEBUTTON_MQTT, and
 * CONFIG_FREEBUTTON_SYSTEM to add sensor/mqtt/system globals, and
 * CONFIG_FB_PROXY for the fb proxy dispatcher.
 * LED, button, display, and device globals are removed — the fb object
 * tree (built in C by device_hardware.cpp) provides these via the proxy.
 */
#define CONFIG_FREEBUTTON_SENSOR
#define CONFIG_FREEBUTTON_MQTT
#define CONFIG_FREEBUTTON_SYSTEM
#define CONFIG_FB_PROXY
#include "mqjs_stdlib.c"
