//
// Home Assistant Discovery Support
// Created by Bolke de Bruin on 24/08/2025.
//
// Discovery runs as a non-blocking poll from Arduino loop(): each poll()
// publishes at most one config, and only when the broker is connected, its
// outbox is drained and the heap has room. Nothing is stored per entity:
// registered DiscoverySources (a few pointers) generate each config from the
// hardware when it is published, straight into buffers on the caller's stack
// (see HaEntities.h).
//

#ifndef FREEBUTTON_HOMEASSISTANTDISCOVERY_H
#define FREEBUTTON_HOMEASSISTANTDISCOVERY_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "HaEntities.h"

struct HomeAssistantDevice {
    std::string name;                   // friendly name, may change (config)
    // Fixed for the device's lifetime: must point to static storage.
    std::string_view manufacturer;
    std::string_view model;
    std::string_view sw_version;
    std::string_view identifier;        // Unique device identifier
};

class HomeAssistantDiscovery {
public:
    // Broker ids 0..kMaxBrokers-1 can use discovery.
    static constexpr uint8_t kMaxBrokers = 8;

    // Configure discovery for a specific broker ("hassdiscovery").
    static void configureBroker(uint8_t brokerId, bool enabled = true,
                                const std::string& discovery_prefix = "homeassistant");

    // Check if discovery is enabled for a broker
    static bool isDiscoveryEnabled(uint8_t brokerId);

    // Register a source of entities, once (not per broker); the source must
    // outlive discovery. Schedules a round on every enabled broker. Call
    // from one task only (main).
    static void addSource(const freebutton::ha::DiscoverySource& source);

    // Set global device information (used for all entities)
    static void setDeviceInfo(const HomeAssistantDevice& device);
    static void setDeviceName(std::string name);
    // Copies the friendly name into buf (at most cap bytes, no terminator)
    // and returns the number of bytes copied. Any task.
    static size_t copyDeviceName(char* buf, size_t cap);

    // Callback for when a broker connects: a round starts once the connect
    // burst (resubscriptions, retained messages) has settled.
    static void onBrokerConnected(uint8_t brokerId);

    // Publish all discovery for a broker, starting no earlier than delayMs
    // from now. Requests for the same broker coalesce (earliest time wins)
    // and restart the round. Used for the Home Assistant birth message and
    // registration. Any task.
    static void scheduleRepublish(uint8_t brokerId, uint32_t delayMs);

    // Call often from ONE task (Arduino loop()). Never blocks; publishes at
    // most one message per call.
    static void poll();
};

#endif //FREEBUTTON_HOMEASSISTANTDISCOVERY_H
