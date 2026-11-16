#include "ConnectionGuard.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/priv/tcpip_priv.h>
#include <lwip/tcp.h>

#include "GuardLogic.h"

// Bench testing: pretend this much less heap is free.
#ifndef FB_GUARD_HEAP_OFFSET
#define FB_GUARD_HEAP_OFFSET 0
#endif

namespace {

const char* TAG = "guard";

// The server's listening socket, known from the first accepted connection.
// Written and used on the TCP/IP thread only.
tcp_pcb_listen* s_listener = nullptr;
// What the backlog was last set to (0: not yet). loop task and async_tcp
// task; a lost update only delays the next change by one tick.
volatile uint8_t s_limit = 0;

uint8_t limitNow() {
    uint32_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    freeHeap = freeHeap > FB_GUARD_HEAP_OFFSET ? freeHeap - FB_GUARD_HEAP_OFFSET : 0;
    return guard::connectionLimit(freeHeap);
}

enum class Op : uint8_t { Count, Release, Limit };

struct Call {
    tcpip_api_call_data base;
    AsyncClient* client;
    Op op;
    uint8_t limit;
};

// On the TCP/IP thread: the only place where a connection's control block
// may be looked at (it can go away at any time elsewhere).
err_t run(tcpip_api_call_data* data) {
    auto* call = reinterpret_cast<Call*>(data);
    tcp_pcb* pcb = call->client ? call->client->pcb() : nullptr;
    if (pcb && pcb->listener) {
        if (call->op == Op::Count && !(pcb->flags & TF_BACKLOGPEND)) {
            s_listener = pcb->listener;
            tcp_backlog_delayed(pcb);  // undone by the stack when the connection closes
        } else if (call->op == Op::Release && (pcb->flags & TF_BACKLOGPEND)) {
            tcp_backlog_accepted(pcb);
        }
    }
    if (!s_listener) return ERR_OK;
    if (call->limit) tcp_backlog_set(s_listener, call->limit);
    // A connection we have closed, and whose client has acknowledged all we
    // sent (FIN_WAIT_2: nothing of it is held any more), is no longer in
    // progress, though the stack keeps it and its count until the client
    // closes too: up to 20 s for a client that never does.
    for (tcp_pcb* p = tcp_active_pcbs; p; p = p->next) {
        if ((p->flags & TF_BACKLOGPEND) && p->listener == s_listener && p->state == FIN_WAIT_2) {
            tcp_backlog_accepted(p);
        }
    }
    return ERR_OK;
}

void call(Op op, AsyncClient* client, uint8_t limit) {
    Call c{};
    c.client = client;
    c.op = op;
    c.limit = limit;
    tcpip_api_call(run, &c.base);
    if (limit) s_limit = limit;
}

// Reaches the server's protected listening socket.
struct ServerAccess : AsyncWebServer {
    static AsyncServer& tcp(AsyncWebServer& server) { return server.*(&ServerAccess::_server); }
};

// A request with a body that nothing consumes is answered before the body
// is read. As a handler without body callbacks it makes the server drop the
// body bytes instead of collecting them.
class BodyGuard final : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        if (request->contentLength() == 0 && !request->hasHeader("Transfer-Encoding")) return false;
        return !guard::bodyAllowed(request->method() == HTTP_PUT, request->url().c_str());
    }

    void handleRequest(AsyncWebServerRequest* request) override {
        ESP_LOGW(TAG, "%s %s refused: unexpected body of %u bytes", request->methodToString(), request->url().c_str(),
                 static_cast<unsigned>(request->contentLength()));
        request->send(413, "text/plain", "This request takes no body.");
    }
};

}  // namespace

namespace guard {

void install(AsyncWebServer& server) {
    // As the server library's own callback, plus the count.
    ServerAccess::tcp(server).onClient(
        [](void* s, AsyncClient* c) {
            if (!c) return;
            call(Op::Count, c, limitNow());
            c->setRxTimeout(3);
            const std::shared_ptr<AsyncWebServerRequest> r = AsyncWebServerRequest::create(static_cast<AsyncWebServer*>(s), c);
            if (!r) {
                c->abort();
                delete c;
            }
        },
        &server);
    server.addHandler(new BodyGuard());
}

void release(AsyncClient* client) {
    if (client) call(Op::Release, client, 0);
}

void tick() {
    if (s_limit == 0) return;  // no connection yet: the listening socket is not known
    call(Op::Limit, nullptr, limitNow());
}

}  // namespace guard
