// @title LED Blink
// @description Toggle an LED on and off
function blink() {
    fb.bars[0].left.led.setColor(255, 0, 0);
    setTimeout(function() {
        fb.bars[0].left.led.off();
        setTimeout(blink, 500);
    }, 500);
}

blink();
