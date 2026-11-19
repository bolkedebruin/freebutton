#include "RequestGuard.h"

#include <ESPAsyncWebServer.h>
#include <esp_log.h>

#include "GuardLogic.h"

namespace {

const char* TAG = "guard";

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
    server.addHandler(new BodyGuard());
}

}  // namespace guard
