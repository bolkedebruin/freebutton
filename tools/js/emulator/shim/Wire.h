// Emulator shim: the two I2C buses, answered by the virtual hardware of the
// host (which modules are on which connector, which sensors exist).
#pragma once
#include <stdint.h>
#include <string>
#include "Arduino.h"
#include "fbh.h"

class TwoWire {
public:
    explicit TwoWire(const int bus) : bus_(bus) {}
    bool begin(int sda, int scl) { (void)sda; (void)scl; return true; }
    void beginTransmission(const uint8_t address) { address_ = address; }
    // 0 = a device acknowledged the address.
    uint8_t endTransmission() const { return fbh_i2c_probe(bus_, address_) ? 0 : 2; }
    int bus() const { return bus_; }

private:
    int bus_;
    uint8_t address_ = 0;
};

// Wire: the sensor bus; Wire1: the bus of the connectors' port expanders.
extern TwoWire Wire;
extern TwoWire Wire1;
