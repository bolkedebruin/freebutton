// Emulator shim: the sensor type names src/drivers/Sensors.h uses.
#pragma once
#include "Wire.h"
typedef enum {
    SENSOR_TYPE_AMBIENT_TEMPERATURE = 13,
    SENSOR_TYPE_PRESSURE = 6,
    SENSOR_TYPE_RELATIVE_HUMIDITY = 12,
    SENSOR_TYPE_VOLTAGE = 15,
    SENSOR_TYPE_CURRENT = 16,
} sensors_type_t;
