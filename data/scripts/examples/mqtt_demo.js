// @title MQTT Demo
// @description Full MQTT demo with LED, button, and sensor integration
/*
 * MQTT Demo Script for FreeButton
 *
 * This script demonstrates the MQTT JavaScript API:
 * - Publishing messages
 * - Subscribing to topics with callbacks
 * - Connection/disconnection events
 * - Integrating with LED and button controls
 *
 * Prerequisites:
 * - At least one MQTT broker configured
 * - Broker ID 0 should be your primary broker
 */

// Build sides array from fb topology. Only bars have LEDs/buttons; the main
// display has none, so it is not included here.
var sides = [];
if (fb.bars) {
    for (var b = 0; b < fb.bars.length; b++) {
        sides.push(fb.bars[b].left);
        sides.push(fb.bars[b].right);
    }
}

// Print banner
print("=".repeat(60));
print("MQTT Demo Script - FreeButton");
print("=".repeat(60));

// Check broker availability
var brokerCount = mqtt.getBrokerCount();
print("Number of MQTT brokers: " + brokerCount);

if (brokerCount === 0) {
    print("ERROR: No MQTT brokers configured!");
    throw new Error("No MQTT brokers available");
}

// Print broker information
for (var i = 0; i < brokerCount; i++) {
    var name = mqtt.getBrokerName(i);
    var connected = mqtt.isConnected(i);
    print("Broker " + i + ": " + name + " (connected: " + connected + ")");
}

// Use broker 0 for this demo
var BROKER_ID = 0;
var DEVICE_TOPIC = "freebutton/demo";

print("\nUsing broker: " + mqtt.getBrokerName(BROKER_ID));

// Register connection event handlers
mqtt.onConnect(BROKER_ID, function(brokerId) {
    print("✓ Connected to broker " + brokerId);

    // Publish connection announcement
    mqtt.publish(BROKER_ID, DEVICE_TOPIC + "/status", "online", 1, 1);

    // Flash all LEDs green to indicate connection
    for (var i = 0; i < sides.length; i++) {
        sides[i].led.setColor(0, 255, 0);
        setTimeout(function() { sides[i].led.off(); }, 500);
    }
});

mqtt.onDisconnect(BROKER_ID, function(brokerId) {
    print("✗ Disconnected from broker " + brokerId);

    // Flash all LEDs red to indicate disconnection
    for (var i = 0; i < sides.length; i++) {
        sides[i].led.setColor(255, 0, 0);
    }
});

// Subscribe to command topic
mqtt.subscribe(BROKER_ID, DEVICE_TOPIC + "/command", function(topic, payload) {
    print("Received command: " + payload);

    // Parse simple commands
    if (payload === "status") {
        // Publish device status
        var status = {
            uptime: system.uptime(),
            freeHeap: system.freeHeap(),
            version: system.version(),
            ledCount: sides.length,
            sensorCount: sensor.count()
        };
        mqtt.publish(BROKER_ID, DEVICE_TOPIC + "/status", JSON.stringify(status));
    }
    else if (payload.indexOf("led:") === 0) {
        // LED control commands: "led:0:255:0:0" = turn side 0 LED red
        var parts = payload.split(":");
        if (parts.length === 5) {
            var ledIdx = parseInt(parts[1]);
            var r = parseInt(parts[2]);
            var g = parseInt(parts[3]);
            var b = parseInt(parts[4]);
            if (ledIdx >= 0 && ledIdx < sides.length) {
                sides[ledIdx].led.setColor(r, g, b);
                print("Set LED " + ledIdx + " to RGB(" + r + "," + g + "," + b + ")");
            }
        }
    }
    else if (payload === "led:off") {
        // Turn all LEDs off
        for (var i = 0; i < sides.length; i++) {
            sides[i].led.off();
        }
        print("All LEDs off");
    }
    else {
        print("Unknown command: " + payload);
    }
});

// Subscribe to LED control topic with wildcard
mqtt.subscribe(BROKER_ID, DEVICE_TOPIC + "/led/#", function(topic, payload) {
    print("LED topic: " + topic + " = " + payload);

    // Extract LED index from topic: freebutton/demo/led/0
    var parts = topic.split("/");
    if (parts.length >= 4) {
        var ledIdx = parseInt(parts[3]);

        // Parse color from payload (format: "255,0,0" for red)
        var colors = payload.split(",");
        if (colors.length === 3 && ledIdx >= 0 && ledIdx < sides.length) {
            var r = parseInt(colors[0]);
            var g = parseInt(colors[1]);
            var b = parseInt(colors[2]);
            sides[ledIdx].led.setColor(r, g, b);
        }
    }
});

// Subscribe to sensor request topic
mqtt.subscribe(BROKER_ID, DEVICE_TOPIC + "/sensors/get", function(topic, payload) {
    print("Sensor data requested");

    sensor.forEach(function(id, type) {
        var value = sensor.getValue(id);
        var sensorTopic = DEVICE_TOPIC + "/sensors/" + type + "/" + id;
        mqtt.publish(BROKER_ID, sensorTopic, value.toString());
        print("Published sensor " + id + " (" + type + "): " + value);
    });
});

// Button integration - publish button events to MQTT
for (var btnIdx = 0; btnIdx < sides.length; btnIdx++) {
    (function(idx) {
        sides[idx].button.onClick(function() {
            var eventTopic = DEVICE_TOPIC + "/button/" + idx + "/click";
            mqtt.publish(BROKER_ID, eventTopic, "clicked");
            print("Button " + idx + " clicked - published to MQTT");

            // Visual feedback
            sides[idx].led.setColor(0, 0, 255);  // Blue flash
            setTimeout(function() { sides[idx].led.off(); }, 300);
        });

        sides[idx].button.onLongPress(function() {
            var eventTopic = DEVICE_TOPIC + "/button/" + idx + "/longpress";
            mqtt.publish(BROKER_ID, eventTopic, "longpress");
            print("Button " + idx + " long pressed - published to MQTT");

            // Visual feedback
            sides[idx].led.setColor(255, 165, 0);  // Orange flash
            setTimeout(function() { sides[idx].led.off(); }, 300);
        });
    })(btnIdx);
}

// Sensor monitoring - publish changes to MQTT. forEach gives each callback its
// own id/type, so no IIFE is needed to capture them per iteration.
sensor.forEach(function(sensorId, sensorType) {
    // forEach also gives the device's own memory sensors (type "data_size",
    // ids from 100). onChange takes the hardware sensors only (ids 0-7) and
    // throws a RangeError for the others.
    if (sensorType === "data_size") return;
    sensor.onChange(sensorId, function(value) {
        var sensorTopic = DEVICE_TOPIC + "/sensors/" + sensorType + "/" + sensorId;
        mqtt.publish(BROKER_ID, sensorTopic, value.toString());
        print("Sensor " + sensorId + " changed: " + value);
    });
});

// Periodic heartbeat using recursive setTimeout
function heartbeat() {
    if (mqtt.isConnected(BROKER_ID)) {
        var data = {
            timestamp: Date.now(),
            uptime: system.uptime(),
            freeHeap: system.freeHeap()
        };
        mqtt.publish(BROKER_ID, DEVICE_TOPIC + "/heartbeat", JSON.stringify(data), 0, 0);
    }
    setTimeout(heartbeat, 30000);
}
heartbeat();

print("\n" + "=".repeat(60));
print("MQTT Demo Script initialized!");
print("=".repeat(60));
print("\nTry these MQTT commands:");
print("  mosquitto_pub -t '" + DEVICE_TOPIC + "/command' -m 'status'");
print("  mosquitto_pub -t '" + DEVICE_TOPIC + "/command' -m 'led:0:255:0:0'");
print("  mosquitto_pub -t '" + DEVICE_TOPIC + "/led/0' -m '0,255,0'");
print("  mosquitto_pub -t '" + DEVICE_TOPIC + "/sensors/get' -m '1'");
print("\nPress any button to publish event to MQTT");
print("=".repeat(60));
