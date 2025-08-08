#include <stdio.h>  // printf
#include "sts3x.h"

int mainnietmain(void) {
    /* Initialize I2C bus */
    sensirion_i2c_init();

    /* Busy loop for initialization, because the main loop does not work without
     * a sensor.
     */
    while (sts3x_probe(0x4A) != STATUS_OK) {
        printf("STS sensor probing failed\n");
        sensirion_sleep_usec(1000000);
    }
    printf("STS sensor probing successful\n");

    while (1) {
        int32_t temperature;
        float temperature_degree;
        /* Measure temperature  and store into variable temperature
         * (output is multiplied by 1000).
         */
        int16_t ret = sts3x_measure_blocking_read(0x4A, &temperature);
        if (ret == STATUS_OK) {
            temperature_degree = temperature / 1000.0f;
            printf("measured temperature: %0.2f degreeCelsius\n",
                   temperature_degree);
        } else {
            printf("error reading measurement\n");
        }

        sensirion_sleep_usec(1000000);
    }
    return 0;
}
