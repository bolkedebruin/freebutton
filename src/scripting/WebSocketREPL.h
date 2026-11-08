//
// WebSocketREPL - WebSocket-based JavaScript REPL for remote debugging
// Broadcasts console output to all connected clients
//

#pragma once

#include <atomic>
#include <string_view>
#include <string>
#include <map>
#include <cstdint>
#include <ESPAsyncWebServer.h>

namespace Scripting {

class WebSocketREPL {
public:
    // Singleton access
    [[nodiscard]] static WebSocketREPL& getInstance();

    // Initialize and attach to web server at /ws/repl
    void init(AsyncWebServer& server);

    // Broadcast console output to all connected WebSocket clients
    // Called from the JS console callback - zero buffering, streams directly
    void broadcastConsole(std::string_view message);

    // Check if any clients are connected
    [[nodiscard]] bool hasClients() const noexcept;

    // Get number of connected clients
    [[nodiscard]] size_t clientCount() const noexcept;

    // Clean up stale/disconnected clients - call periodically from main loop
    void cleanupClients();

    // Loop task: run a parked eval request and send its result. The
    // WebSocket handler (async_tcp task) never runs script code itself.
    void processPending();

private:
    WebSocketREPL() = default;
    ~WebSocketREPL() = default;

    // WebSocket event handler
    void onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                 AwsEventType type, void* arg, uint8_t* data, size_t len);

    // Handle incoming message from client
    void handleMessage(AsyncWebSocketClient* client, std::string_view data);

    // Send evaluation result back to client. With client == nullptr (loop
    // task) the client is looked up by id under the server's client lock; it
    // may have gone away meanwhile.
    void sendResult(AsyncWebSocketClient* client, uint32_t clientId, std::string_view id,
                    bool success, std::string_view value);

    // WebSocket instance (lazy initialized)
    AsyncWebSocket* ws_{nullptr};

    // One parked eval request, handed from the async_tcp task to the loop
    // task (which owns the JS engine). Waiting for the engine in async_tcp
    // would stall the web server (and its watchdog) for as long as a JS timer
    // or MQTT callback runs. A second request while one is parked is answered
    // with an error. `code` is the string the handler already built (moved,
    // not copied) and is freed after the run.
    struct PendingEval {
        uint32_t clientId{0};
        char id[40]{};
        size_t idLength{0};
        std::string code;
    };
    PendingEval pending_;
    // false: pending_ belongs to async_tcp; true: to the loop task.
    std::atomic<bool> pendingReady_{false};

    // Per-client receive buffers for reassembling a text message that arrives
    // split across multiple WS frames / TCP packets. Capped tight — this is a
    // memory-constrained device and the JS engine can only run small snippets;
    // larger scripts go via compile+upload, not the REPL.
    std::map<uint32_t, std::string> rxBuffers_;
    static constexpr size_t MAX_MESSAGE_BYTES = 1024;  // drop oversized messages

    // Disable copy and move
    WebSocketREPL(const WebSocketREPL&) = delete;
    WebSocketREPL& operator=(const WebSocketREPL&) = delete;
    WebSocketREPL(WebSocketREPL&&) = delete;
    WebSocketREPL& operator=(WebSocketREPL&&) = delete;
};

// Static callback for console output (called from mquickjs)
void websocketConsoleCallback(const char* buf, size_t len);

// Free function for main.cpp (avoids including ESPAsyncWebServer there)
void processWebSocketRepl();

} // namespace Scripting
