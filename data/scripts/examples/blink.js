// @title LED Blink
// @description Toggle an LED on and off
function blink() {
    fb.main.left.led.setColor(255, 0, 0);
    setTimeout(function() {
        fb.main.left.led.off();
        setTimeout(blink, 500);
    }, 500);
}

blink();
