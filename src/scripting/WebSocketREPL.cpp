//
// WebSocketREPL - WebSocket-based JavaScript REPL implementation
//

#include "WebSocketREPL.h"
#include "ScriptEngine.h"
#include "../json/JsonEscape.h"
#include <esp_log.h>
#include <algorithm>
#include <cstring>
#include <string>

static const char* TAG = "WebSocketREPL";

// Unescape a JSON string (handles \", \\, \n, \r, \t)
static std::string unescapeJson(std::string_view input) {
    std::string result;
    result.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 1 < input.size()) {
            char next = input[i + 1];
            switch (next) {
                case '"':  result += '"';  ++i; break;
                case '\\': result += '\\'; ++i; break;
                case 'n':  result += '\n'; ++i; break;
                case 'r':  result += '\r'; ++i; break;
                case 't':  result += '\t'; ++i; break;
                case '/':  result += '/';  ++i; break;
                default:   result += input[i]; break;
            }
        } else {
            result += input[i];
        }
    }
    return result;
}

// JSON string escaping is shared with the /api/device serializer via
// FreeButton::escapeJsonString (src/json/JsonEscape.h) — it also emits \u00XX
// for control characters, which arbitrary REPL output / console.log text can
// contain, so responses are always valid JSON.
using FreeButton::escapeJsonString;

namespace Scripting {

WebSocketREPL& WebSocketREPL::getInstance() {
    static WebSocketREPL instance;
    return instance;
}

void WebSocketREPL::init(AsyncWebServer& server) {
    if (ws_) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    // Create WebSocket endpoint at /ws/repl
    ws_ = new AsyncWebSocket("/ws/repl");

    // Set event handler
    ws_->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        onEvent(server, client, type, arg, data, len);
    });

    server.addHandler(ws_);
    ESP_LOGI(TAG, "WebSocket REPL initialized at /ws/repl");
}

void WebSocketREPL::onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                             AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            ESP_LOGI(TAG, "Client #%u connected from %s",
                     client->id(), client->remoteIP().toString().c_str());
            // Send connected message
            client->text(R"({"type":"connected"})");
            break;

        case WS_EVT_DISCONNECT:
            ESP_LOGI(TAG, "Client #%u disconnected", client->id());
            rxBuffers_.erase(client->id());  // drop any partial message
            break;

        case WS_EVT_DATA: {
            auto* info = static_cast<AwsFrameInfo*>(arg);

            // Only handle text messages (eval/ping are text JSON).
            if (info->message_opcode != WS_TEXT) {
                break;
            }

            // Fast path: a whole message delivered in a single unfragmented frame.
            if (info->final && info->index == 0 && info->len == len) {
                handleMessage(client, std::string_view(reinterpret_cast<char*>(data), len));
                break;
            }

            // Slow path: reassemble a message split across multiple frames and/or
            // TCP packets. Chunks arrive in order, so we just append them.
            std::string& buf = rxBuffers_[client->id()];
            if (buf.size() + len > MAX_MESSAGE_BYTES) {
                ESP_LOGW(TAG, "Client #%u message exceeds %u bytes, dropping",
                         client->id(), (unsigned)MAX_MESSAGE_BYTES);
                rxBuffers_.erase(client->id());
                break;
            }
            buf.append(reinterpret_cast<char*>(data), len);

            // Message is complete when this is the final frame and we have all of
            // its bytes (index+len == this frame's total length).
            if (info->final && (info->index + len) == info->len) {
                handleMessage(client, std::string_view(buf));
                rxBuffers_.erase(client->id());
            }
            break;
        }

        case WS_EVT_PONG:
            ESP_LOGD(TAG, "Pong from client #%u", client->id());
            break;

        case WS_EVT_ERROR:
            ESP_LOGE(TAG, "Error from client #%u", client->id());
            break;
    }
}

void WebSocketREPL::handleMessage(AsyncWebSocketClient* client, std::string_view data) {
    // Simple JSON parsing without ArduinoJson to minimize memory usage
    // Expected format: {"type":"eval","id":"xxx","code":"..."}
    //              or: {"type":"ping"}

    // Find type field
    auto typePos = data.find("\"type\"");
    if (typePos == std::string_view::npos) {
        return;
    }

    // Check for ping
    if (data.find("\"ping\"") != std::string_view::npos) {
        client->text(R"({"type":"pong"})");
        return;
    }

    // Check for eval
    if (data.find("\"eval\"") == std::string_view::npos) {
        return;
    }

    // Extract id field
    std::string_view id;
    auto idPos = data.find("\"id\"");
    if (idPos != std::string_view::npos) {
        auto colonPos = data.find(':', idPos);
        auto quoteStart = data.find('"', colonPos);
        auto quoteEnd = data.find('"', quoteStart + 1);
        if (quoteStart != std::string_view::npos && quoteEnd != std::string_view::npos) {
            id = data.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
    }

    // Extract code field
    auto codePos = data.find("\"code\"");
    if (codePos == std::string_view::npos) {
        sendResult(client, client->id(), id, false, "Missing 'code' field");
        return;
    }

    auto colonPos = data.find(':', codePos);
    auto quoteStart = data.find('"', colonPos);
    if (quoteStart == std::string_view::npos) {
        sendResult(client, client->id(), id, false, "Invalid 'code' format");
        return;
    }

    // Find end of code string (handle escaped quotes)
    size_t quoteEnd = quoteStart + 1;
    while (quoteEnd < data.size()) {
        if (data[quoteEnd] == '"' && data[quoteEnd - 1] != '\\') {
            break;
        }
        quoteEnd++;
    }

    if (quoteEnd >= data.size()) {
        sendResult(client, client->id(), id, false, "Unterminated 'code' string");
        return;
    }

    std::string_view codeEscaped = data.substr(quoteStart + 1, quoteEnd - quoteStart - 1);

    // Unescape JSON string (handles \" -> ", \n -> newline, etc.)
    std::string code = unescapeJson(codeEscaped);

    ESP_LOGD(TAG, "Executing: %s", code.c_str());

    // Memory diagnostics for debugging crashes
    ESP_LOGD(TAG, "Free heap: %lu, min free: %lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size());

    if (!ScriptEngine::getInstance().isInitialized()) {
        sendResult(client, client->id(), id, false, "Script engine not initialized");
        return;
    }

    // Park the request for the loop task, which owns the JS engine. This
    // handler runs in async_tcp and must not run script code.
    if (pendingReady_.load(std::memory_order_acquire)) {
        sendResult(client, client->id(), id, false, "REPL busy: previous eval still pending");
        return;
    }
    pending_.clientId = client->id();
    pending_.idLength = std::min(id.size(), sizeof(pending_.id));
    std::memcpy(pending_.id, id.data(), pending_.idLength);
    pending_.code = std::move(code);
    pendingReady_.store(true, std::memory_order_release);
}

void WebSocketREPL::processPending() {
    if (!pendingReady_.load(std::memory_order_acquire)) {
        return;
    }
    // execute() takes the engine lock.
    const auto result = ScriptEngine::getInstance().execute(pending_.code, "<repl>");
    // Free the slot BEFORE answering: a client may send its next eval as soon
    // as it has the result, and must not get "REPL busy" for it.
    char id[sizeof(pending_.id)];
    const size_t idLength = pending_.idLength;
    const uint32_t clientId = pending_.clientId;
    std::memcpy(id, pending_.id, idLength);
    std::string().swap(pending_.code);  // free the code now, not at the next eval
    pendingReady_.store(false, std::memory_order_release);
    sendResult(nullptr, clientId, std::string_view(id, idLength), result.success,
               result.success ? result.output : result.error);
}

void WebSocketREPL::sendResult(AsyncWebSocketClient* client, const uint32_t clientId, std::string_view id,
                                bool success, std::string_view value) {
    // Escape value for JSON and truncate if needed
    std::string escapedValue = escapeJsonString(value);
    if (escapedValue.size() > 150) {
        escapedValue = escapedValue.substr(0, 150) + "...";
    }

    // Build JSON response
    // Use different field names for success vs error
    char buffer[512];
    int len;

    if (success) {
        len = snprintf(buffer, sizeof(buffer),
            R"({"type":"result","id":"%.*s","success":true,"value":"%s"})",
            static_cast<int>(id.size()), id.data(),
            escapedValue.c_str());
    } else {
        len = snprintf(buffer, sizeof(buffer),
            R"({"type":"result","id":"%.*s","success":false,"error":"%s"})",
            static_cast<int>(id.size()), id.data(),
            escapedValue.c_str());
    }

    if (len > 0 && len < static_cast<int>(sizeof(buffer))) {
        if (client) {
            client->text(buffer, len);
        } else if (ws_) {
            // From loop(), after the code ran. Its console output may have
            // filled the client's short queue: a result must not be dropped
            // with it (the IDE waits for it), so give the queue up to a
            // second to drain.
            for (int i = 0; i < 100 && ws_->hasClient(clientId) && !ws_->availableForWrite(clientId); ++i) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            ws_->text(clientId, buffer, len);  // no-op if the client has gone
        }
    }
}

void WebSocketREPL::broadcastConsole(std::string_view message) {
    if (!ws_ || ws_->count() == 0) {
        return;  // No clients connected
    }

    // A client's queue is short (WS_MAX_QUEUED_MESSAGES, platformio.ini): a
    // burst of lines must not take the heap. Lines that do not fit are
    // counted, and the count is sent when there is room again.
    if (!ws_->availableForWriteAll()) {
        ++consoleDropped_;
        return;
    }
    if (consoleDropped_) {
        char note[112];
        const int n = snprintf(note, sizeof(note), R"({"type":"console","message":"... %u lines dropped: the console could not keep up"})",
                               static_cast<unsigned>(consoleDropped_));
        consoleDropped_ = 0;
        if (n > 0 && n < static_cast<int>(sizeof(note))) ws_->textAll(note, n);
    }

    // Escape message for JSON and truncate if needed
    std::string escapedMsg = escapeJsonString(message);
    if (escapedMsg.size() > 200) {
        escapedMsg = escapedMsg.substr(0, 200) + "...";
    }

    // Build JSON message
    char buffer[300];
    int len = snprintf(buffer, sizeof(buffer),
        R"({"type":"console","message":"%s"})",
        escapedMsg.c_str());

    if (len > 0 && len < static_cast<int>(sizeof(buffer))) {
        ws_->textAll(buffer, len);
    }
}

bool WebSocketREPL::hasClients() const noexcept {
    return ws_ && ws_->count() > 0;
}

size_t WebSocketREPL::clientCount() const noexcept {
    return ws_ ? ws_->count() : 0;
}

void WebSocketREPL::cleanupClients() {
    if (ws_) {
        ws_->cleanupClients();
    }
}

// Free function for easy access from main loop without include issues
void cleanupWebSocketClients() {
    WebSocketREPL::getInstance().cleanupClients();
}

void processWebSocketRepl() {
    WebSocketREPL::getInstance().processPending();
}

// Static callback for console output - bridges mquickjs to WebSocket
void websocketConsoleCallback(const char* buf, size_t len) {
    WebSocketREPL::getInstance().broadcastConsole(std::string_view(buf, len));
}

} // namespace Scripting
