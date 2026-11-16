//
// Refuses request bodies nobody consumes (rule in GuardLogic.h). The limits
// on what the server library keeps of a request are a patch of the library:
// tools/scripts/patch_webserver.py.
//
#ifndef FREEBUTTON_REQUESTGUARD_H
#define FREEBUTTON_REQUESTGUARD_H

class AsyncWebServer;

namespace guard {

// Before the routes are registered.
void install(AsyncWebServer& server);

}  // namespace guard

#endif  // FREEBUTTON_REQUESTGUARD_H
