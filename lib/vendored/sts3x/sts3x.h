#ifndef STS3X_H
#define STS3X_H

#include "sensirion_arch_config.h"
#include "sensirion_i2c.h"
#include "sts_git_version.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STATUS_OK 0
#define STATUS_ERR_BAD_DATA (-1)
#define STATUS_CRC_FAIL (-2)
#define STATUS_UNKNOWN_DEVICE (-3)

#define STS3X_MEASUREMENT_DURATION_USEC 15500

/**
 * Detects if a sensor is connected by reading out the ID register.
 * If the sensor does not answer or if the answer is not the expected value,
 * the test fails.
 *
 * @return 0 if a sensor was detected
 */
int16_t sts3x_probe(uint8_t address);

/**
 * Starts a measurement and then reads out the results. This function blocks
 * while the measurement is in progress. The duration of the measurement depends
 * on the sensor in use, please consult the datasheet.
 * Temperature is returned in [degree Celsius], multiplied by 1000
 *
 * @param temperature   the address for the result of the temperature
 * measurement
 * @return              0 if the command was successful, else an error code.
 */
int16_t sts3x_measure_blocking_read(uint8_t address, int32_t* temperature);

/**
 * Starts a measurement in high precision mode. Use sts3x_read() to read out the
 * values, once the measurement is done. The duration of the measurement depends
 * on the sensor in use, please consult the datasheet.
 *
 * @return     0 if the command was successful, else an error code.
 */
int16_t sts3x_measure(uint8_t address);

/**
 * Reads out the results of a measurement that was previously started by
 * sts3x_measure(). If the measurement is still in progress, this function
 * returns an error.
 * Temperature is returned in [degree Celsius], multiplied by 1000
 *
 * @param temperature   the address for the result of the temperature
 * measurement
 * @return              0 if the command was successful, else an error code.
 */
int16_t sts3x_read(uint8_t address, int32_t* temperature);

/**
 * Set repeatability of the STS
 *
 * @param repeatability 0 for high repeatability mode (default)
 *                      1 for medium repeatability mode
 *                      2 for low repeatability mode
 */
void sts3x_set_repeatability(uint8_t repeatability);

/**
 * Enable internal heater. The heater is meant for plausibility check only.
 *
 * @return 0 if the command was successful,
 *         1 if an error occured
 */
int16_t sts3x_heater_on(uint8_t address);

/**
 * Disable internal heater
 *
 * @return 0 if the command was successful,
 *         1 if an error occured
 */
int16_t sts3x_heater_off(uint8_t address);

/**
 * Read out the serial number
 *
 * @param serial    the address for the result of the serial number
 * @return          0 if the command was successful, else an error code.
 */
int16_t sts3x_read_serial(uint8_t address, uint32_t* serial);

/**
 * Return the driver version
 *
 * @return Driver version string
 */
const char* sts3x_get_driver_version(void);

/**
 * Returns the configured STSxx address.
 *
 * @return STSxx_ADDRESS
 */
uint8_t sts3x_get_configured_address(uint8_t address);

#ifdef __cplusplus
}
#endif

#endif /* STS3X_H */
