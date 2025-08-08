//
// Created by Bolke de Bruin on 06/08/2025.
//
#pragma once

#include <map>
#include <string_view>
#include <memory>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#include <sts3x.h>

namespace ButtonPlus {

// Forward declarations
class SensorDevice;

enum class SensorType {
    STS35 = 1,
    STS35_2 = 2,
    BME280_PRESSURE = 3,
    BME280_HUMIDITY = 4,
    BME280_TEMPERATURE = 5,
    ESP32S3_INTERNAL_TEMPERATURE = 6,
    VIRTUAL = 100,
};

// Virtual sensor IDs (100+)
enum VirtualSensorIds {
    // Memory sensors (100-109)
    FREE_HEAP_ID = 100,
    LV_USED_MEM_ID = 101,
    LV_FREE_MEM_ID = 102,

    // Network sensors (110-119)
    // System sensors (120-129)
};


class MovingAverage {
private:
    int windowSize;
    int sampleCount;
    double sum;
    double average;
    std::string lastPublishedValue;

public:
    explicit MovingAverage(const int size)
        : windowSize(size), sampleCount(0), sum(0), average(0) {}

    void addSample(const double sample) {
        if (sampleCount < windowSize) {
            sum += sample;
            sampleCount++;
        } else {
            sum = sum - (sum / windowSize) + sample;
        }
        average = sum / sampleCount;
    }

    [[nodiscard]] double getAverage() const { return average; }
    [[nodiscard]] bool isWindowFull() const { return sampleCount >= windowSize; }

    bool hasValueChanged(const std::string& currentValue) {
        if (lastPublishedValue != currentValue) {
            lastPublishedValue = currentValue;
            return true;
        }
        return false;
    }

    void setWindowSize(const int newSize) {
        windowSize = newSize;
        reset();
    }

    void reset() {
        sum = 0;
        sampleCount = 0;
        average = 0;
        lastPublishedValue.clear();
    }
};

class SensorDevice {
public:
    const SensorType type;
    const std::string_view name;
    const std::string_view description;
    const std::string_view deviceClass;
    const std::string_view unitOfMeasurement;
    const sensors_type_t sensorType;
    bool online;

    SensorDevice(
        const SensorType type_,
        std::string_view name_,
        std::string_view description_,
        std::string_view deviceClass_,
        std::string_view unitOfMeasurement_,
        const sensors_type_t sensorType_
    ) : type(type_)
        , name(name_)
        , description(description_)
        , deviceClass(deviceClass_)
        , unitOfMeasurement(unitOfMeasurement_)
        , sensorType(sensorType_)
        , online(false) {}

    virtual ~SensorDevice() = default;
    virtual float sample() = 0;
};

class STS35Sensor final : public SensorDevice {
public:
    STS35Sensor(
        const SensorType type_,
        std::string_view name_,
        std::string_view description_,
        const uint8_t address_
    ) : SensorDevice(
            type_,
            name_,
            description_,
            "temperature",
            "°C",
            SENSOR_TYPE_AMBIENT_TEMPERATURE
        )
        , address(address_) {
        online = !sts3x_probe(address);
    }

    float sample() override {
        int32_t temperature;
        if (const int16_t ret = sts3x_measure_blocking_read(address, &temperature); ret == STATUS_OK) {
            return temperature / 1000.0f - 1.5;
        }
        return 0.0f;
    }

private:
    uint8_t address;
};

class BME280Sensor final : public SensorDevice {
public:
    BME280Sensor(
        const SensorType type_,
        std::string_view name_,
        std::string_view description_,
        std::string_view deviceClass_,
        std::string_view unitOfMeasurement_,
        const sensors_type_t sensorType_,
        const uint8_t address_
    ) : SensorDevice(
            type_,
            name_,
            description_,
            deviceClass_,
            unitOfMeasurement_,
            sensorType_
        )
        , address(address_) {
        extern Adafruit_BME280 bme;
        online = bme.begin(address, &Wire1);
        if (online) {
            bme.setSampling(
                Adafruit_BME280::MODE_FORCED,
                Adafruit_BME280::SAMPLING_X1,
                Adafruit_BME280::SAMPLING_X1,
                Adafruit_BME280::SAMPLING_X1,
                Adafruit_BME280::FILTER_OFF
            );
        }
    }

    float sample() override {
        if (!online) return 0.0f;

        extern Adafruit_BME280 bme;
        bme.takeForcedMeasurement();
        switch (type) {
            case SensorType::BME280_PRESSURE:
                return bme.readPressure() / 100.0f;
            case SensorType::BME280_HUMIDITY:
                return bme.readHumidity();
            default:
                return 0.0f;
        }
    }

private:
    uint8_t address;
};

class VirtualSensor final : public SensorDevice {
public:
    using SamplingFunction = std::function<float()>;

    VirtualSensor(
        int id_,
        std::string_view name_,
        std::string_view deviceClass_,
        std::string_view unitOfMeasurement_,
        SamplingFunction sampler_
    ) : SensorDevice(
            SensorType::VIRTUAL,
            name_,
            name_,
            deviceClass_,
            unitOfMeasurement_,
            SENSOR_TYPE_AMBIENT_TEMPERATURE
        )
        , virtualId(id_)
        , sampler(std::move(sampler_)) {
        online = true;
    }

    float sample() override {
        return sampler ? sampler() : 0.0f;
    }

    [[nodiscard]] int getId() const { return virtualId; }

private:
    int virtualId;
    SamplingFunction sampler;
};

// Global instances
extern std::map<int, std::shared_ptr<SensorDevice>> sensorDevices;
extern Adafruit_BME280 bme;

void addSensor(int address);
void sampleSensors();

}
