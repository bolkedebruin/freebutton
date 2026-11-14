//
// Sensor Hardware Abstraction Layer
// C-compatible interface to C++ Sensor components
//

#include <Arduino.h>
#include <esp_log.h>
#include <cmath>
#include "sensor_binding.h"

#include <ranges>

#include "drivers/Sensors.h"
#include "ScriptEngine.h"

static const char* TAG = "SensorBinding";

// Maximum number of sensors (must match freebutton_sensor.c)
#define MAX_SENSORS 8

// Static buffer for returning sensor info (to avoid dynamic allocation)
static SensorInfo cachedInfo;

// Minimal memory footprint: fixed-size arrays (72 bytes total for 8 sensors)
// Callback storage: 8 sensors * 4 bytes (function pointer) = 32 bytes
static SensorChangeCallback changeCallbacks[MAX_SENSORS] = {nullptr};
// Previous values: 8 sensors * 4 bytes (float) = 32 bytes
static float previousValues[MAX_SENSORS] = {0.0f};
// Tracking which sensors are registered: 8 sensors * 1 byte = 8 bytes
static bool sensorRegistered[MAX_SENSORS] = {false};

extern "C" {

// Get sensor value by ID
float sensor_hw_get_value(int sensorId) {
    auto it = FreeButton::sensorDevices.find(sensorId);
    if (it == FreeButton::sensorDevices.end()) {
        ESP_LOGW(TAG, "Sensor %d not found", sensorId);
        return 0.0f;
    }

    auto& sensor = it->second;
    if (!sensor || !sensor->online) {
        ESP_LOGW(TAG, "Sensor %d is offline", sensorId);
        return 0.0f;
    }

    float value = sensor->sample();
    ESP_LOGD(TAG, "Sensor %d value: %.2f", sensorId, value);
    return value;
}

// Get sensor type name by ID
const char* sensor_hw_get_type(int sensorId) {
    auto it = FreeButton::sensorDevices.find(sensorId);
    if (it == FreeButton::sensorDevices.end()) {
        ESP_LOGW(TAG, "Sensor %d not found", sensorId);
        return nullptr;
    }

    auto& sensor = it->second;
    if (!sensor) {
        return nullptr;
    }

    // Return device class (which is the sensor type: "temperature", "humidity", etc.)
    return sensor->deviceClass.data();
}

// Get sensor information by ID
const SensorInfo* sensor_hw_get_info(int sensorId) {
    auto it = FreeButton::sensorDevices.find(sensorId);
    if (it == FreeButton::sensorDevices.end()) {
        ESP_LOGW(TAG, "Sensor %d not found", sensorId);
        return nullptr;
    }

    auto& sensor = it->second;
    if (!sensor) {
        return nullptr;
    }

    // Populate the static info struct
    cachedInfo.id = sensorId;
    cachedInfo.name = sensor->name.data();
    cachedInfo.type = sensor->deviceClass.data();
    cachedInfo.unit = sensor->unitOfMeasurement.data();
    cachedInfo.online = sensor->online;

    return &cachedInfo;
}

// Get total number of registered sensors
int sensor_hw_get_count() {
    return static_cast<int>(FreeButton::sensorDevices.size());
}

// Get all sensor IDs
int sensor_hw_get_all_ids(int* ids, int maxCount) {
    if (!ids || maxCount <= 0) {
        ESP_LOGE(TAG, "Invalid parameters to sensor_hw_get_all_ids");
        return 0;
    }

    int count = 0;
    for (const auto &id: FreeButton::sensorDevices | std::views::keys) {
        if (count >= maxCount) {
            break;
        }
        ids[count++] = id;
    }

    ESP_LOGD(TAG, "Retrieved %d sensor IDs", count);
    return count;
}

// Register onChange callback for a specific sensor
void sensor_hw_register_change_callback(int sensorId, SensorChangeCallback callback) {
    if (sensorId < 0 || sensorId >= MAX_SENSORS) {
        ESP_LOGW(TAG, "Sensor ID %d out of range (0-%d)", sensorId, MAX_SENSORS - 1);
        return;
    }

    changeCallbacks[sensorId] = callback;
    sensorRegistered[sensorId] = (callback != nullptr);

    if (callback) {
        ESP_LOGD(TAG, "Registered onChange callback for sensor %d", sensorId);
    }
}

// Engine restart: as after boot
void sensor_hw_reset_change_callbacks(void) {
    for (int i = 0; i < MAX_SENSORS; i++) {
        sensorRegistered[i] = false;
        changeCallbacks[i] = nullptr;
        previousValues[i] = 0.0f;
    }
}

// Notify of sensor value - called from sampleSensors() after each sensor sample()
void sensor_hw_notify_value(int sensorId, float value) {
    // Only process if sensor ID is in valid range and has a registered callback
    if (sensorId < 0 || sensorId >= MAX_SENSORS) {
        return;  // Silently ignore out of range IDs
    }

    if (!sensorRegistered[sensorId]) {
        return;  // No callback registered for this sensor
    }

    const float previousValue = previousValues[sensorId];

    // Threshold: 0.1 means sensor must change by at least 0.1 units to trigger callback
    constexpr float CHANGE_THRESHOLD = 0.1f;

    // Check if value changed beyond threshold
    if (std::fabs(value - previousValue) >= CHANGE_THRESHOLD) {
        previousValues[sensorId] = value;

        // Invoke callback (loop task; the button task may be in the engine)
        if (changeCallbacks[sensorId]) {
            ESP_LOGD(TAG, "Invoking onChange callback for sensor %d", sensorId);
            const Scripting::EngineLock lock;
            // Read again under the lock: an engine restart clears the table.
            if (const SensorChangeCallback callback = changeCallbacks[sensorId]) {
                callback(sensorId, value);
            }
        }
    }
}

} // extern "C"