//
// seam_discovery.cpp - Home Assistant discovery, for fb-emulator.wasm.
//
// src/mqtt/HomeAssistantDiscovery.cpp is the firmware's, included unchanged.
// It publishes one config per loop() pass, after the connect burst settled;
// the runner has no loop that turns by itself, so this file tells it while a
// round is pending, and marks what poll() publishes as discovery.
//
#include <Arduino.h>

#include "emu.h"
#include "fbh.h"

#include "mqtt/HomeAssistantDiscovery.cpp"

extern "C" int fbe_discovery_poll(void) {
    const int scope = fbe_publish_scope(FBH_ORIGIN_FIRMWARE, FBH_PUBLISH_DISCOVERY);
    HomeAssistantDiscovery::poll();
    fbe_publish_scope_end(scope);
    for (const auto& state : brokerStates) {
        if (state.enabled.load() && state.pending) {
            return 1;
        }
    }
    return 0;
}
