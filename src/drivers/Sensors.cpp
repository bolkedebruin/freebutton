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
        MqttPublisher::publish(brokerId, topic, std::to_string(value), 0, false);
    }

    [[nodiscard]] const std::string getComponentType() const override {
        return "SensorComponent";
    }
};

std::map<int, std::unique_ptr<SensorMqttComponent>> sensorComponents;

void addSensor(int address) {
    // Implementation for adding sensors by address
}

void sampleSensors() {
    // For now, we'll use a hardcoded broker ID since we only have one broker
    // In the future, this could be made more dynamic
    const uint8_t brokerId = MqttBrokerRegistry::getBrokerId("homeserver");
    
    for (auto& [id, sensor] : sensorDevices) {
        if (sensor && sensor->online) {
            float value = sensor->sample();
            
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
            HomeAssistantDiscoveryConfig config;
            config.component_type = "sensor";
            config.object_id = "free_memory_" + std::to_string(id);
            config.name = std::string(sensor->name);
            config.device_class = std::string(sensor->deviceClass);
            config.unit_of_measurement = std::string(sensor->unitOfMeasurement);
            config.state_topic = std::string(PUBLICMQTTID) + "/" + deviceId + "/sensor/free_memory/state";
            config.enabled = true;
            
            HomeAssistantDiscovery::registerComponent(brokerId, config);
        }
    }
}

}