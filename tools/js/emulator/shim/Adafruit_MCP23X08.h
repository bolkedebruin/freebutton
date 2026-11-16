// Emulator shim for the connectors' port expanders: the module signature the
// hardware scanner reads, and the two key switches, from the host.
#pragma once
#include <stdint.h>
#include "Wire.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "fbh.h"

class Adafruit_MCP23X08 {
public:
    bool begin_I2C(const uint8_t address, TwoWire* wire) {
        address_ = address;
        return wire != nullptr && fbh_i2c_probe(wire->bus(), address) != 0;
    }
    void pinMode(uint8_t pin, uint8_t mode) { (void)pin; (void)mode; }
    void digitalWrite(uint8_t pin, uint8_t value) { (void)pin; (void)value; }
    // All eight pins: a pressed key reads 0 on its switch pin.
    uint8_t readGPIO() const { return static_cast<uint8_t>(fbh_mcp_gpio(address_)); }
    uint8_t digitalRead(const uint8_t pin) const { return (readGPIO() >> pin) & 1; }

private:
    uint8_t address_ = 0;
};
