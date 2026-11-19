// Emulator shim: a BME280 whose readings the host gives (what: 0 temperature
// in degrees C, 1 pressure in Pa, 2 relative humidity in %).
#pragma once
#include <stdint.h>
#include "fbh.h"

class Adafruit_BME280 {
public:
    bool begin(const uint8_t address) { return fbh_i2c_probe(0, address) != 0; }
    float readTemperature() { return fbh_sensor_read(0); }
    float readPressure() { return fbh_sensor_read(1); }
    float readHumidity() { return fbh_sensor_read(2); }
};
