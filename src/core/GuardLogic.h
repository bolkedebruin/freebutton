//
// Pure rules of the connection guard (ConnectionGuard): no Arduino, no lwIP,
// so the native tests cover them (test/test_guard_logic).
//
#ifndef FREEBUTTON_GUARDLOGIC_H
#define FREEBUTTON_GUARDLOGIC_H

#include <cstdint>
#include <cstring>

namespace guard {

// ---------------------------------------------------------------------------
// How many connections may be in progress
// ---------------------------------------------------------------------------
//
// A connection whose request is being received or answered costs 5 to 7 KB
// for a moment, whatever the size of the answer (TCP control block, the
// request and its headers, the response, a segment-sized block of the TCP
// stack, the Wi-Fi driver's frame; measured, web/TODO.md B39). A browser
// opens six at once. So the number allowed follows the free heap: what is
// free beyond a reserve, divided by the cost of one. At least one always
// (nothing else would ever let the next one in), and never more than kMax.
//
// A connection beyond the limit is not refused: its opening packet is left
// unanswered, and the client sends it again (after about a second). It costs
// nothing meanwhile.

constexpr uint32_t kPerConnection = 6 * 1024;
constexpr uint32_t kReserve = 12 * 1024;
constexpr uint8_t kMaxConnections = 8;

inline uint8_t connectionLimit(uint32_t freeHeap) {
    if (freeHeap < kReserve + 2 * kPerConnection) return 1;
    const uint32_t n = (freeHeap - kReserve) / kPerConnection;
    return n > kMaxConnections ? kMaxConnections : static_cast<uint8_t>(n);
}

// ---------------------------------------------------------------------------
// Which requests may have a body
// ---------------------------------------------------------------------------
//
// The server library collects a form body (and a text body that looks like
// one) in memory before any handler sees it, as large as the client says.
// Only these take a body, and each consumes it as it arrives: an object
// upload (PUT), the firmware / filesystem / bytecode uploads, and the WiFi
// setup form. Any other request with a body is refused before the body is
// read.

inline bool bodyAllowed(bool isPut, const char* url) {
    if (isPut) return true;
    return strcmp(url, "/upload") == 0 || strcmp(url, "/api/bytecode/upload") == 0 || strcmp(url, "/connect") == 0;
}

}  // namespace guard

#endif  // FREEBUTTON_GUARDLOGIC_H
