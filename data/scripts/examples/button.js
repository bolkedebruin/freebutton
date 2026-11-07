// @title Button Handler
// @description Handle click and long press events
fb.main.left.button.onClick(function() {
    print("Left button clicked!");
    fb.bars[0].left.led.setColor(0, 255, 0);
});

fb.main.left.button.onLongPress(function() {
    print("Left button long pressed!");
    fb.bars[0].left.led.setColor(255, 0, 0);
    // Returning false cancels this press's default action: nothing is
    // published to MQTT (no configured topics, no Home Assistant event).
    return false;
});
