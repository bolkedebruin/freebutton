// @title MQTT Simple
// @description Basic MQTT publish and subscribe
/*
 * Simple MQTT Example for FreeButton
 *
 * Minimal example showing basic MQTT publish/subscribe
 */

print("Simple MQTT Example");

// Subscribe to a test topic
mqtt.subscribe(0, "test/message", function(topic, payload) {
    print("Received on " + topic + ": " + payload);

    // Flash left LED green when message received
    fb.bars[0].left.led.setColor(0, 255, 0);
    setTimeout(function() { fb.bars[0].left.led.off(); }, 500);
});

// Left button: Publish a test message
fb.main.left.button.onClick(function() {
    mqtt.publish(0, "test/message", "Hello from FreeButton!");
    print("Published test message");

    // Flash left LED blue
    fb.bars[0].left.led.setColor(0, 0, 255);
    setTimeout(function() { fb.bars[0].left.led.off(); }, 300);
});

print("Press left button to send test message");
print("Subscribed to: test/message");
