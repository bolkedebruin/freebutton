//
// Counters of the download path (StreamResponse.cpp), for /api/device. A
// plain header: the device info code must not pull in the web server.
//
#ifndef FREEBUTTON_STREAMSTATS_H
#define FREEBUTTON_STREAMSTATS_H

#include <cstdint>

namespace stream {

struct Stats {
    uint8_t sending = 0;     // downloads that hold a slot now
    uint8_t waiting = 0;     // downloads that wait for one now
    uint8_t waitingMax = 0;  // most that waited at once, since boot
    uint32_t served = 0;     // downloads sent completely, since boot
    uint32_t stalled = 0;    // senders that lost their slot for silence, since boot
};

Stats stats();

}  // namespace stream

#endif  // FREEBUTTON_STREAMSTATS_H
