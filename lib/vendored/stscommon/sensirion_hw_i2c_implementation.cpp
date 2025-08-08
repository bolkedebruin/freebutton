#include "sensirion_arch_config.h"
#include "sensirion_i2c.h"

// needed for delay() routine
#include <Arduino.h>
#include <Wire.h>

/*
 * INSTRUCTIONS
 * ============
 *
 * See this folder's README.md
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize all hard- and software components that are needed for the I2C
 * communication. After this function has been called, the functions
 * i2c_read() and i2c_write() must succeed.
 */
void sensirion_i2c_init(void) {
    Wire.begin();
}

/**
 * Release all resources initialized by sensirion_i2c_init().
 */
void sensirion_i2c_release(void) {
}

int8_t sensirion_i2c_read(uint8_t address, uint8_t *data, uint16_t count) {
    uint8_t readData[count];
    uint8_t rxByteCount = 0;

    // 2 bytes RH, 1 CRC, 2 bytes T, 1 CRC
    Wire.requestFrom(address, count);

    while (Wire.available()) {  // wait till all arrive
        readData[rxByteCount++] = Wire.read();
        if (rxByteCount >= count)
            break;
    }

    memcpy(data, readData, count);

    return 0;
}

int8_t sensirion_i2c_write(uint8_t address, const uint8_t *data,
                           uint16_t count) {
    Wire.beginTransmission(address);
    Wire.write(data, count);
    Wire.endTransmission();

    return 0;
}

/**
 * Sleep for a given number of microseconds. The function should delay the
 * execution for at least the given time, but may also sleep longer.
 *
 * @param useconds the sleep time in microseconds
 */
void sensirion_sleep_usec(uint32_t useconds) {
    delay((useconds / 1000) + 1);
}

#ifdef __cplusplus
}  // extern "C"
#endif
