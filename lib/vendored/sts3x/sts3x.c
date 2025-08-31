#include "sts3x.h"
#include "../stscommon/sensirion_arch_config.h"
#include "../stscommon/sensirion_common.h"
#include "../stscommon/sensirion_i2c.h"

/* all measurement commands return T (CRC) RH (CRC) */
#if USE_SENSIRION_CLOCK_STRETCHING
#define STS3X_CMD_MEASURE_HPM 0x2C06
#define STS3X_CMD_MEASURE_MPM 0x2C0D
#define STS3X_CMD_MEASURE_LPM 0x2C10
#else
#define STS3X_CMD_MEASURE_HPM 0x2400
#define STS3X_CMD_MEASURE_MPM 0x240B
#define STS3X_CMD_MEASURE_LPM 0x2416
#endif /* USE_SENSIRION_CLOCK_STRETCHING */
static const uint16_t STS3X_CMD_READ_STATUS_REG = 0xF32D;
static const uint16_t STS3X_CMD_READ_SERIAL_ID = 0x3780;
static const uint16_t STS3X_CMD_DURATION_USEC = 1000;
static const uint16_t STS3X_CMD_HEATER_ON = 0x306D;
static const uint16_t STS3X_CMD_HEATER_OFF = 0x3066;

static uint16_t sts3x_cmd_measure = STS3X_CMD_MEASURE_HPM;

int16_t sts3x_measure_blocking_read(uint8_t address, int32_t* temperature) {
    int8_t ret = sts3x_measure(address);
    if (ret == STATUS_OK) {
#if !defined(USE_SENSIRION_CLOCK_STRETCHING) || !USE_SENSIRION_CLOCK_STRETCHING
        sensirion_sleep_usec(STS3X_MEASUREMENT_DURATION_USEC);
#endif /* USE_SENSIRION_CLOCK_STRETCHING */
        ret = sts3x_read(address, temperature);
    }
    return ret;
}

int16_t sts3x_measure(uint8_t address) {
    return sensirion_i2c_write_cmd(address, sts3x_cmd_measure);
}

int16_t sts3x_read(uint8_t address, int32_t* temperature) {
    uint16_t word;
    int16_t ret = sensirion_i2c_read_words(address, &word, 1);
    /**
     * formula for conversion of the sensor signals, optimized for fixed point
     * algebra: Temperature = 175 * S_T / 2^16 - 45
     */
    *temperature = ((21875 * (int32_t)word) >> 13) - 45000;

    return ret;
}

int16_t sts3x_probe(uint8_t address) {
    uint16_t status;
    return sensirion_i2c_delayed_read_cmd(address, STS3X_CMD_READ_STATUS_REG, STS3X_CMD_DURATION_USEC, &status, 1);
}

void sts3x_set_repeatability(uint8_t repeatability) {
    switch (repeatability) {
        case 2:
            sts3x_cmd_measure = STS3X_CMD_MEASURE_LPM;
            break;
        case 1:
            sts3x_cmd_measure = STS3X_CMD_MEASURE_MPM;
            break;
        case 0:
        default:
            sts3x_cmd_measure = STS3X_CMD_MEASURE_HPM;
            break;
    }
}

int16_t sts3x_heater_on(uint8_t address) {
    return sensirion_i2c_write_cmd(address, STS3X_CMD_HEATER_ON);
}

int16_t sts3x_read_serial(uint8_t address, uint32_t* serial) {
    int16_t ret;
    uint8_t serial_bytes[4];

    ret = sensirion_i2c_write_cmd(address, STS3X_CMD_READ_SERIAL_ID);
    if (ret)
        return ret;

    sensirion_sleep_usec(STS3X_CMD_DURATION_USEC);

    ret = sensirion_i2c_read_words_as_bytes(address, serial_bytes, SENSIRION_NUM_WORDS(serial_bytes));
    *serial = sensirion_bytes_to_uint32_t(serial_bytes);
    return ret;
}

int16_t sts3x_heater_off(uint8_t address) {
    return sensirion_i2c_write_cmd(address, STS3X_CMD_HEATER_OFF);
}

uint8_t sts3x_get_configured_address(uint8_t address) {
    return address;
}
