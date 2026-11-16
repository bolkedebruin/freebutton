//
// Pure rules of the request guard (RequestGuard): no Arduino, so the native
// tests cover them (test/test_guard_logic).
//
#ifndef FREEBUTTON_GUARDLOGIC_H
#define FREEBUTTON_GUARDLOGIC_H

#include <cstring>

namespace guard {

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
