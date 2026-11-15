//
// Sensor Hardware Abstraction Layer - Header
// C-compatible interface to C++ Sensor components
//

#ifndef SENSOR_BINDING_H
#define SENSOR_BINDING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of sensors a script can watch: sensor IDs 0..MAX_SENSORS-1
// (sensor_binding.cpp and js/freebutton_sensor.c size their tables with it).
#define MAX_SENSORS 8

// Sensor information structure (returned by sensor_hw_get_info)
typedef struct {
    int id;
    const char* name;
    const char* type;  // "temperature", "humidity", "pressure", "data_size"
    const char* unit;  // "°C", "%", "hPa", "B"
    bool online;
} SensorInfo;

// Callback type for sensor value change events
typedef void (*SensorChangeCallback)(int sensorId, float value);

// Get sensor value by ID
// Returns the current sensor reading, or 0.0 if sensor not found/offline
float sensor_hw_get_value(int sensorId);

// Get sensor type name by ID
// Returns sensor type string ("temperature", "humidity", etc.) or NULL if not found
const char* sensor_hw_get_type(int sensorId);

// Get sensor information by ID
// Returns pointer to static SensorInfo struct, or NULL if not found
// WARNING: Do not free() the returned pointer or its string fields
const SensorInfo* sensor_hw_get_info(int sensorId);

// Get total number of registered sensors
int sensor_hw_get_count();

// Get all sensor IDs
// Fills the provided array with sensor IDs, up to maxCount
// Returns the actual number of sensors copied
int sensor_hw_get_all_ids(int* ids, int maxCount);

// Register onChange callback for a specific sensor
// The callback will be invoked when the sensor value changes
void sensor_hw_register_change_callback(int sensorId, SensorChangeCallback callback);

// Engine restart: no sensor has a change callback, and the remembered values
// are back at their boot state (0). Engine lock held.
void sensor_hw_reset_change_callbacks(void);

// Notify of sensor value - called from sampleSensors() for each sensor
// Handles change detection and dispatches callback if value changed beyond threshold
void sensor_hw_notify_value(int sensorId, float value);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_BINDING_H