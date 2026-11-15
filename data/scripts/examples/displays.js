// @title Temperature Display
// @description Show the temperature on the main TFT and a bar, updated on sensor change (event-driven)
//
// Uses sensor.onChange() so the displays refresh only when the temperature
// actually changes — event-driven, not polled on a timer.
//
// Two display APIs are shown together:
//   fb.main.display          -> main TFT "display items": create by geometry,
//                               then update by id (setValue / setLabel / ...).
//   fb.bars[n].left.display  -> small per-button bar display: setLabel + setTopLabel.

// --- Find the temperature sensor. sensor.forEach() iterates every sensor,
//     calling back with (id, type) — no array is allocated. ---
var tempId = -1;
sensor.forEach(function(id, type) {
    if (type === "temperature") tempId = id;
});

// The display item on the main TFT and whether a bar is fitted: set below,
// used by show().
var item = -1;
var hasBar = false;

// Push a reading to both displays. Declared here, at the top level: a
// function declared inside an if / else block is not defined on the device.
function show(value) {
    var rounded = Math.round(value * 10) / 10;                   // 1 decimal
    fb.main.display.setValue(item, rounded);                     // main: numeric, by id
    if (hasBar) fb.bars[0].left.display.setLabel(rounded + " C"); // bar: label text
}

if (tempId < 0) {
    print("No temperature sensor found");
} else {
    print("Temperature sensor id=" + tempId);

    // Main TFT: one display item for the temperature.
    // create(x, y, width, boxType, fontSize, alignment, label, unit, round, page)
    //   alignment: 0=left 1=center 2=right | round: decimals | page: 1-based
    item = fb.main.display.create(20, 20, 220, 0, 40, 1, "Temperature", "C", 1, 1);

    hasBar = fb.bars && fb.bars.length > 0;
    if (hasBar) fb.bars[0].left.display.setTopLabel("Temp");

    // Show the current reading immediately...
    show(sensor.getValue(tempId));

    // ...then update ONLY when the temperature changes (event-driven).
    sensor.onChange(tempId, function(value) {
        show(value);
        print("Temperature: " + value);
    });

    print("Temperature display running — updates on sensor change");
}
