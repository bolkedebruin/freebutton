//
// Connection guard: keeps the number of HTTP connections in progress within
// what the free heap can carry, and refuses request bodies nobody consumes.
// The rules are in GuardLogic.h.
//
// It works through the TCP stack's listen backlog: every accepted connection
// is counted against the listening socket until it closes (or is released),
// and the stack leaves the opening packet of a connection beyond the limit
// unanswered. Such a connection takes no memory; the client tries again.
//
#ifndef FREEBUTTON_CONNECTIONGUARD_H
#define FREEBUTTON_CONNECTIONGUARD_H

class AsyncWebServer;
class AsyncClient;

namespace guard {

// Before server.begin(). Takes over the server's accept callback and adds
// the handler that refuses unexpected bodies.
void install(AsyncWebServer& server);

// This connection no longer counts: it is long-lived or waits cheaply (the
// console WebSocket, a download that has its own admission rule, an upload).
void release(AsyncClient* client);

// From loop(), about twice a second: lets the limit follow the free heap and
// stops counting connections the server has closed.
void tick();

}  // namespace guard

#endif  // FREEBUTTON_CONNECTIONGUARD_H
