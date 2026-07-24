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

if (tempId < 0) {
    print("No temperature sensor found");
} else {
    print("Temperature sensor id=" + tempId);

    // Main TFT: one display item for the temperature.
    // create(x, y, width, boxType, fontSize, alignment, label, unit, round, page)
    //   alignment: 0=left 1=center 2=right | round: decimals | page: 1-based
    var item = fb.main.display.create(20, 20, 220, 0, 40, 1, "Temperature", "C", 1, 1);

    var hasBar = fb.bars && fb.bars.length > 0;
    if (hasBar) fb.bars[0].left.display.setTopLabel("Temp");

    // Push a reading to both displays.
    function show(value) {
        var rounded = Math.round(value * 10) / 10;                   // 1 decimal
        fb.main.display.setValue(item, rounded);                     // main: numeric, by id
        if (hasBar) fb.bars[0].left.display.setLabel(rounded + " C"); // bar: label text
    }

    // Show the current reading immediately...
    show(sensor.getValue(tempId));

    // ...then update ONLY when the temperature changes (event-driven).
    sensor.onChange(tempId, function(value) {
        show(value);
        print("Temperature: " + value);
    });

    print("Temperature display running — updates on sensor change");
}
