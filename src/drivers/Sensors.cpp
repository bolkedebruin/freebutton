#include "Sensors.h"
#include <string_view>
#include <lvgl.h>
#include "LvglDisplay.h"
#include "../mqtt/Registry.h"
#include <cstdio>
#include "../scripting/sensor_binding.h"
#include "../scripting/ScriptEngine.h"

#include "../../lib/vendored/stscommon/sensirion_arch_config.h"
#include "../../lib/vendored/stscommon/sensirion_i2c.h"
#include "../../lib/vendored/sts3x/sts3x.h" // Vendored library

using namespace std::string_view_literals;

namespace FreeButton {

std::map<int, std::shared_ptr<SensorDevice>> sensorDevices;
Adafruit_BME280 bme;

class SensorMqttComponent : public MqttPublisher {
private:
    int sensorId;
    std::string stateTopic;  // "<prefix>/state"

public:
    SensorMqttComponent(const int id, const std::string& prefix)
        : sensorId(id), stateTopic(prefix + "/state") {
        // Add this sensor's topic as a publication
        addPublication(0, stateTopic, 0, false); // Will be updated with actual broker ID
    }

    // Every second for every sensor: no heap. The topic is kept, the value
    // is formatted on the stack (as std::to_string did: "%f").
    void publishSensorValue(const uint8_t brokerId, const float value) const {
        const auto* broker = MqttBrokerRegistry::getBroker(brokerId);
        if (!broker) return;
        char data[48];
        const int n = snprintf(data, sizeof data, "%f", static_cast<double>(value));
        if (n <= 0 || n >= static_cast<int>(sizeof data)) return;
        broker->publish(stateTopic.c_str(), data, static_cast<size_t>(n), 0, 0);
    }

    [[nodiscard]] const std::string getComponentType() const override {
        return "SensorComponent";
    }
};

std::map<int, std::unique_ptr<SensorMqttComponent>> sensorComponents;

// STS35Sensor Implementation
STS35Sensor::STS35Sensor(uint8_t i2cAddress, TwoWire* wire, int avgWindowSize)
    : SensorDevice(
        (i2cAddress == 0x4A) ? SensorType::STS35 : SensorType::STS35_2,
        (i2cAddress == 0x4A) ? "STS35 Temperature 1"sv : "STS35 Temperature 2"sv,
        (i2cAddress == 0x4A) ? "STS35 Temperature Sensor 1"sv : "STS35 Temperature Sensor 2"sv,
        "temperature"sv,
        "°C"sv,
        SENSOR_TYPE_AMBIENT_TEMPERATURE
    )
    , address_(i2cAddress)
    , wire_(wire)
    , movingAvg_(avgWindowSize)
    , lastReadTime_(0) {
    
    online = initializeSensor();
    ESP_LOGI(TAG, "STS35 sensor at 0x%02X %s", address_, online ? "initialized" : "failed");
}

float STS35Sensor::sample() {
    const unsigned long currentTime = millis();
    
    // Rate limiting to avoid overwhelming the sensor
    if (currentTime - lastReadTime_ < READ_INTERVAL_MS) {
        return static_cast<float>(movingAvg_.getAverage());
    }
    
    lastReadTime_ = currentTime;
    
    int32_t temperature = 0;

    if (const int16_t result = sts3x_measure_blocking_read(address_, &temperature); result != STATUS_OK) {
        ESP_LOGW(TAG, "Failed to read STS35 at 0x%02X, error: %d", address_, result);
        online = false;
        return static_cast<float>(movingAvg_.getAverage()); // Return last known good value
    }
    
    // Convert from (temperature * 1000) to actual temperature
    const float temperatureFloat = static_cast<float>(temperature) / 1000.0f;
    movingAvg_.addSample(temperatureFloat);
    online = true;
    
    ESP_LOGD(TAG, "STS35 (0x%02X): %.2f°C", address_, movingAvg_.getAverage());
    
    return static_cast<float>(movingAvg_.getAverage());
}

bool STS35Sensor::initializeSensor() const {
    if (!wire_) {
        ESP_LOGE(TAG, "Wire interface is null");
        return false;
    }
    
    // Initialize Sensirion I2C library (one time initialization)
    static bool sensirion_initialized = false;
    if (!sensirion_initialized) {
        sensirion_i2c_init();
        sensirion_initialized = true;
    }
    
    // Probe the sensor to check if it's responding
    if (const int16_t result = sts3x_probe(address_); result != STATUS_OK) {
        ESP_LOGE(TAG, "STS35 probe failed at 0x%02X, error: %d", address_, result);
        return false;
    }
    
    ESP_LOGI(TAG, "STS35 sensor initialized at address 0x%02X", address_);
    return true;
}

void addSensor(int address) {
    ESP_LOGI("Sensors", "Adding sensor at address 0x%02X", address);
    
    // STS35 Temperature sensors (addresses 0x4A, 0x4B)
    if (address == 0x4A || address == 0x4B) {
        const int sensorId = static_cast<int>((address == 0x4A) ? SensorType::STS35 : SensorType::STS35_2);
        
        // Create STS35 sensor
        auto sts35Sensor = std::make_shared<STS35Sensor>(address, &Wire);
        sensorDevices[sensorId] = sts35Sensor;
        
        // Create MQTT component  
        const std::string sensorName = (address == 0x4A) ? "sts35_1" : "sts35_2";
        const std::string topicPrefix = std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/" + sensorName;
        auto component = std::make_unique<SensorMqttComponent>(sensorId, topicPrefix);
        sensorComponents[sensorId] = std::move(component);
        
        ESP_LOGI("Sensors", "STS35 sensor added at address 0x%02X (ID: %d)", address, sensorId);
    }
    // BME280 sensor (address 0x76)
    else if (address == 0x76) {
        constexpr int sensorId = static_cast<int>(SensorType::BME280_TEMPERATURE);
        
        // Initialize BME280
        if (bme.begin(address)) {
            // Create temperature sensor
            auto bme280TempSensor = std::make_shared<VirtualSensor>(
                sensorId,
                "BME280 Temperature"sv,
                "temperature"sv,
                "°C"sv,
                []() {
                    return bme.readTemperature();
                }
            );
            
            sensorDevices[sensorId] = bme280TempSensor;
            
            // Create pressure sensor
            constexpr int pressureSensorId = static_cast<int>(SensorType::BME280_PRESSURE);
            auto bme280PressureSensor = std::make_shared<VirtualSensor>(
                pressureSensorId,
                "BME280 Pressure"sv,
                "pressure"sv,
                "hPa"sv,
                []() {
                    return bme.readPressure() / 100.0f;
                }
            );
            
            sensorDevices[pressureSensorId] = bme280PressureSensor;
            
            // Create humidity sensor
            constexpr int humiditySensorId = static_cast<int>(SensorType::BME280_HUMIDITY);
            auto bme280HumiditySensor = std::make_shared<VirtualSensor>(
                humiditySensorId,
                "BME280 Humidity"sv,
                "humidity"sv,
                "%"sv,
                []() {
                    return bme.readHumidity();
                }
            );
            
            sensorDevices[humiditySensorId] = bme280HumiditySensor;
            
            // Create MQTT components
            auto tempComponent = std::make_unique<SensorMqttComponent>(
                sensorId, std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/bme280_temperature"
            );
            auto pressureComponent = std::make_unique<SensorMqttComponent>(
                pressureSensorId, std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/bme280_pressure"  
            );
            auto humidityComponent = std::make_unique<SensorMqttComponent>(
                humiditySensorId, std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/bme280_humidity"
            );
            
            sensorComponents[sensorId] = std::move(tempComponent);
            sensorComponents[pressureSensorId] = std::move(pressureComponent);
            sensorComponents[humiditySensorId] = std::move(humidityComponent);
            
            ESP_LOGI("Sensors", "BME280 sensor added at address 0x%02X (3 sensors)", address);
        } else {
            ESP_LOGE("Sensors", "Failed to initialize BME280 at address 0x%02X", address);
        }
    } else {
        ESP_LOGW("Sensors", "Unknown sensor address 0x%02X", address);
    }
}

void sampleSensors() {
    uint8_t brokerId = 255;
    MqttBrokerRegistry::forEachBroker([&brokerId](uint8_t id, MqttBroker* broker) {
        if (broker && broker->isConfigured() && brokerId == 255) {
            brokerId = id;
        }
    });

    // Skip if no configured broker found
    if (brokerId == 255) {
        return;
    }

    for (auto& [id, sensor] : sensorDevices) {
        if (sensor && sensor->online) {
            const float value = sensor->sample();

            // find(), not operator[]: the maps are immutable after init
            // (discovery iterates them from the same loop task).
            if (const auto component = sensorComponents.find(id);
                component != sensorComponents.end() && component->second) {
                component->second->publishSensorValue(brokerId, value);
            }

            // Dispatch JavaScript onChange events (if registered)
            sensor_hw_notify_value(id, value);
        }
    }
}

void initializeSensors() {
    const auto freeMem = std::make_shared<VirtualSensor>(
        VirtualSensorIds::FREE_HEAP_ID,
        "Free Memory"sv,
        "data_size"sv,
        "B"sv,
        []() {
            return static_cast<float>(esp_get_free_heap_size());
        }
    );

    sensorDevices[VirtualSensorIds::FREE_HEAP_ID] = freeMem;

    // Create MQTT component and register with Home Assistant Discovery
    auto component = std::make_unique<SensorMqttComponent>(
        VirtualSensorIds::FREE_HEAP_ID,
        std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/free_memory"
    );

    sensorComponents[VirtualSensorIds::FREE_HEAP_ID] = std::move(component);

    // LVGL Used Memory sensor
    const auto lvglUsedMem = std::make_shared<VirtualSensor>(
        VirtualSensorIds::LV_USED_MEM_ID,
        "LVGL Used Memory"sv,
        "data_size"sv,
        "B"sv,
        []() {
            // Snapshot from the LVGL task (lv_mem_monitor needs the LVGL lock).
            size_t free = 0, total = 0;
            getLvglDisplay().getLvglMemory(free, total);
            return static_cast<float>(total - free);
        }
    );

    sensorDevices[VirtualSensorIds::LV_USED_MEM_ID] = lvglUsedMem;

    auto lvglUsedComponent = std::make_unique<SensorMqttComponent>(
        VirtualSensorIds::LV_USED_MEM_ID,
        std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/lvgl_used_memory"
    );

    sensorComponents[VirtualSensorIds::LV_USED_MEM_ID] = std::move(lvglUsedComponent);

    // LVGL Free Memory sensor
    const auto lvglFreeMem = std::make_shared<VirtualSensor>(
        VirtualSensorIds::LV_FREE_MEM_ID,
        "LVGL Free Memory"sv,
        "data_size"sv,
        "B"sv,
        []() {
            // Snapshot from the LVGL task (lv_mem_monitor needs the LVGL lock).
            size_t free = 0, total = 0;
            getLvglDisplay().getLvglMemory(free, total);
            return static_cast<float>(free);
        }
    );

    sensorDevices[VirtualSensorIds::LV_FREE_MEM_ID] = lvglFreeMem;

    auto lvglFreeComponent = std::make_unique<SensorMqttComponent>(
        VirtualSensorIds::LV_FREE_MEM_ID,
        std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/lvgl_free_memory"
    );

    sensorComponents[VirtualSensorIds::LV_FREE_MEM_ID] = std::move(lvglFreeComponent);

    // JS heap used / free. heapUsage() collects garbage under the engine lock
    // (short wait; while the engine is busy it gives the last measurement).
    // Read one after the other, so the second one measures right after the
    // first: the GC then has little to do.
    const auto jsUsedMem = std::make_shared<VirtualSensor>(
        VirtualSensorIds::JS_USED_MEM_ID,
        "JS Used Memory"sv,
        "data_size"sv,
        "B"sv,
        []() {
            return static_cast<float>(Scripting::ScriptEngine::getInstance().heapUsage().used);
        }
    );
    sensorDevices[VirtualSensorIds::JS_USED_MEM_ID] = jsUsedMem;
    sensorComponents[VirtualSensorIds::JS_USED_MEM_ID] = std::make_unique<SensorMqttComponent>(
        VirtualSensorIds::JS_USED_MEM_ID,
        std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/js_used_memory"
    );

    const auto jsFreeMem = std::make_shared<VirtualSensor>(
        VirtualSensorIds::JS_FREE_MEM_ID,
        "JS Free Memory"sv,
        "data_size"sv,
        "B"sv,
        []() {
            return static_cast<float>(Scripting::ScriptEngine::getInstance().heapUsage().free);
        }
    );
    sensorDevices[VirtualSensorIds::JS_FREE_MEM_ID] = jsFreeMem;
    sensorComponents[VirtualSensorIds::JS_FREE_MEM_ID] = std::make_unique<SensorMqttComponent>(
        VirtualSensorIds::JS_FREE_MEM_ID,
        std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/js_free_memory"
    );
}

namespace {

// Sensors with an MQTT component, in map order: entry `index` of the source.
const SensorDevice* discoverableSensor(const size_t index, int& id) {
    size_t n = 0;
    for (const auto& [sensorId, sensor] : sensorDevices) {
        const auto component = sensorComponents.find(sensorId);
        if (!sensor || component == sensorComponents.end() || !component->second) {
            continue;
        }
        if (n++ == index) {
            id = sensorId;
            return sensor.get();
        }
    }
    return nullptr;
}

class SensorDiscoverySource final : public freebutton::ha::DiscoverySource {
public:
    [[nodiscard]] size_t discoveryCount() const override {
        size_t n = 0;
        int id;
        while (discoverableSensor(n, id)) ++n;
        return n;
    }

    bool writeDiscovery(const size_t index, uint8_t, freebutton::ha::DiscoveryOut& out) const override {
        int id = 0;
        const SensorDevice* sensor = discoverableSensor(index, id);
        if (!sensor) {
            return false;
        }
        // object id <name>_<sensor id>; state on buttonplus/<id>/sensor/<name>/state
        const char* name;
        switch (sensor->type) {
            case SensorType::STS35:              name = "sts35_1"; break;
            case SensorType::STS35_2:            name = "sts35_2"; break;
            case SensorType::BME280_TEMPERATURE: name = "bme280_temperature"; break;
            case SensorType::BME280_PRESSURE:    name = "bme280_pressure"; break;
            case SensorType::BME280_HUMIDITY:    name = "bme280_humidity"; break;
            case SensorType::VIRTUAL:
                name = id == VirtualSensorIds::FREE_HEAP_ID     ? "free_memory"
                     : id == VirtualSensorIds::LV_USED_MEM_ID   ? "lvgl_used_memory"
                     : id == VirtualSensorIds::LV_FREE_MEM_ID   ? "lvgl_free_memory"
                     : id == VirtualSensorIds::JS_USED_MEM_ID   ? "js_used_memory"
                     : id == VirtualSensorIds::JS_FREE_MEM_ID   ? "js_free_memory"
                     : nullptr;
                break;
            default:
                name = nullptr;
                break;
        }

        char objectId[48];
        char topicName[32];
        if (name) {
            std::snprintf(objectId, sizeof(objectId), "%s_%d", name, id);
            std::snprintf(topicName, sizeof(topicName), "%s", name);
        } else if (sensor->type == SensorType::VIRTUAL) {
            std::snprintf(objectId, sizeof(objectId), "virtual_%d", id);
            std::snprintf(topicName, sizeof(topicName), "virtual_%d", id);
        } else {
            std::snprintf(objectId, sizeof(objectId), "sensor_%d", id);
            std::snprintf(topicName, sizeof(topicName), "unknown");
        }

        freebutton::ha::writeSensor(out, objectId, sensor->name, sensor->deviceClass,
                                    sensor->unitOfMeasurement, topicName);
        return true;
    }
};

} // namespace

const freebutton::ha::DiscoverySource& sensorDiscoverySource() {
    static SensorDiscoverySource source;
    return source;
}

}