// @title Button Handler
// @description Handle click and long press events
fb.main.left.button.onClick(function() {
    print("Left button clicked!");
    fb.main.left.led.setColor(0, 255, 0);
});

fb.main.left.button.onLongPress(function() {
    print("Left button long pressed!");
    fb.main.left.led.setColor(255, 0, 0);
});
