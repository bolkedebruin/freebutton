#include "Sensors.h"
#include <esp_heap_caps.h>
#include <string_view>
#include "../mqtt/Registry.h"
#include "../mqtt/HomeAssistantDiscovery.h"

using namespace std::string_view_literals;

namespace ButtonPlus {

std::map<int, std::shared_ptr<SensorDevice>> sensorDevices;
Adafruit_BME280 bme;

class SensorMqttComponent : public MqttPublisher {
private:
    int sensorId;
    std::string topicPrefix;

public:
    SensorMqttComponent(const int id, const std::string& prefix)
        : sensorId(id), topicPrefix(prefix) {
        // Add this sensor's topic as a publication
        addPublication(0, prefix + "/state", 0, false); // Will be updated with actual broker ID
    }

    void publishSensorValue(const uint8_t brokerId, const float value) const {
        const std::string topic = topicPrefix + "/state";
        const std::string data = std::to_string(value);
        MqttPublisher::publish(brokerId, topic, data, 0, false);
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
    movingAvg_.addSample(static_cast<double>(temperatureFloat));
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
    // For now, we'll use a hardcoded broker ID since we only have one broker
    // In the future, this could be made more dynamic
    const uint8_t brokerId = MqttBrokerRegistry::getBrokerId("homeserver");

    for (auto& [id, sensor] : sensorDevices) {
        if (sensor && sensor->online) {
            const float value = sensor->sample();
            
            if (sensorComponents[id]) {
                sensorComponents[id]->publishSensorValue(brokerId, value);
            }
        }
    }
}

void initializeSensors() {
    const auto freeMem = std::make_shared<VirtualSensor>(
        VirtualSensorIds::FREE_HEAP_ID,
        "Free Memory"sv,
        "memory"sv,
        "bytes"sv,
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
}

void registerSensorsWithDiscovery(const uint8_t brokerId) {
    for (auto& [id, sensor] : sensorDevices) {
        if (sensor && sensorComponents[id]) {
            // Generate appropriate object_id based on sensor type
            std::string objectId;
            std::string stateTopic;
            
            switch (sensor->type) {
                case SensorType::STS35:
                    objectId = "sts35_1_" + std::to_string(id);
                    stateTopic = std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/sts35_1/state";
                    break;
                case SensorType::STS35_2:
                    objectId = "sts35_2_" + std::to_string(id);
                    stateTopic = std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/sts35_2/state";
                    break;
                case SensorType::BME280_TEMPERATURE:
                    objectId = "bme280_temperature_" + std::to_string(id);
                    stateTopic = std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/bme280_temperature/state";
                    break;
                case SensorType::BME280_PRESSURE:
                    objectId = "bme280_pressure_" + std::to_string(id);
                    stateTopic = std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/bme280_pressure/state";
                    break;
                case SensorType::BME280_HUMIDITY:
                    objectId = "bme280_humidity_" + std::to_string(id);
                    stateTopic = std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/bme280_humidity/state";
                    break;
                case SensorType::VIRTUAL:
                    objectId = "virtual_" + std::to_string(id);
                    stateTopic = std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/free_memory/state";
                    break;
                default:
                    objectId = "sensor_" + std::to_string(id);
                    stateTopic = std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/unknown/state";
                    break;
            }
            
            HomeAssistantDiscoveryConfig config;
            config.component_type = "sensor";
            config.object_id = objectId;
            config.name = std::string(sensor->name);
            config.device_class = std::string(sensor->deviceClass);
            config.unit_of_measurement = std::string(sensor->unitOfMeasurement);
            config.state_topic = stateTopic;
            config.enabled = true;
            
            ESP_LOGI("Sensors", "Registering sensor '%s' with Home Assistant", config.name.c_str());
            HomeAssistantDiscovery::registerComponent(brokerId, config);
        }
    }
}

}