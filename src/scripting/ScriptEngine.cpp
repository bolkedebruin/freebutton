//
// ScriptEngine - JavaScript scripting engine implementation
//

#include "ScriptEngine.h"
#include "WebSocketREPL.h"
#include <Arduino.h>
#include <cstring>
#include <esp_log.h>
#include <esp_rom_sys.h>

static const char* TAG = "ScriptEngine";

// Include mquickjs C headers
extern "C" {
    #include <mquickjs.h>
    #include <mquickjs_ext.h>

    // External declaration for the generated FreeButton stdlib
    // This is defined in lib/mquickjs/src/stdlib_export.c which includes the generated header
    extern const JSSTDLibraryDef js_stdlib;
}

namespace Scripting {

// Singleton implementation
ScriptEngine& ScriptEngine::getInstance(size_t heapSize) {
    static ScriptEngine instance(heapSize);
    return instance;
}

ScriptEngine::ScriptEngine(size_t heapSize)
    : heapSize_(heapSize) {
}

ScriptEngine::~ScriptEngine() {
    shutdown();
}

bool ScriptEngine::init() {
    if (ctx_) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    // Allocate heap buffer
    heapBuffer_ = new (std::nothrow) uint8_t[heapSize_];
    if (!heapBuffer_) {
        ESP_LOGE(TAG, "Failed to allocate %zu bytes for heap", heapSize_);
        return false;
    }

    // Create JSContext with our custom stdlib (includes standard library + LED bindings)
    ctx_ = JS_NewContext(heapBuffer_, heapSize_, &js_stdlib);
    if (!ctx_) {
        ESP_LOGE(TAG, "Failed to create JSContext");
        delete[] heapBuffer_;
        heapBuffer_ = nullptr;
        return false;
    }

    // Set log callback for internal mquickjs logging
    JS_SetLogFunc(ctx_, logCallback);

    // Set console callback for WebSocket REPL
    // This enables print()/console.log() to be sent to WebSocket clients
    JS_SetConsoleWriteFunc(websocketConsoleCallback);

    ESP_LOGI(TAG, "Initialized with %zu bytes heap", heapSize_);
    ESP_LOGI(TAG, "JavaScript API available:");
    ESP_LOGI(TAG, "  Standard library:");
    ESP_LOGI(TAG, "    - console.log(...)");
    ESP_LOGI(TAG, "    - print(...)");
    ESP_LOGI(TAG, "    - Math, String, Array, Object, etc.");
    ESP_LOGI(TAG, "  FreeButton LED control:");
    ESP_LOGI(TAG, "    - led.count()");
    ESP_LOGI(TAG, "    - led.on(position)");
    ESP_LOGI(TAG, "    - led.off(position)");
    ESP_LOGI(TAG, "    - led.setColor(position, r, g, b)");
    ESP_LOGI(TAG, "  FreeButton Button control:");
    ESP_LOGI(TAG, "    - button.count()");
    ESP_LOGI(TAG, "    - button.setLabel(position, text)");
    ESP_LOGI(TAG, "    - button.setTopLabel(position, text)");
    ESP_LOGI(TAG, "    - button.onClick(position, callback)");
    ESP_LOGI(TAG, "    - button.onLongPress(position, callback)");
    ESP_LOGI(TAG, "    - button.onRelease(position, callback)");
    ESP_LOGI(TAG, "  FreeButton Sensor monitoring:");
    ESP_LOGI(TAG, "    - sensor.count()");
    ESP_LOGI(TAG, "    - sensor.getValue(sensorId)");
    ESP_LOGI(TAG, "    - sensor.getType(sensorId)");
    ESP_LOGI(TAG, "    - sensor.getInfo(sensorId)");
    ESP_LOGI(TAG, "    - sensor.getAll()");
    ESP_LOGI(TAG, "    - sensor.onChange(sensorId, callback)");
    return true;
}

void ScriptEngine::shutdown() {
    if (ctx_) {
        JS_FreeContext(ctx_);
        ctx_ = nullptr;
    }
    if (heapBuffer_) {
        delete[] heapBuffer_;
        heapBuffer_ = nullptr;
    }
    ESP_LOGI(TAG, "Shutdown complete");
}

ScriptResult ScriptEngine::execute(std::string_view script, const char* filename) {
    if (!ctx_) {
        return {false, "", "Script engine not initialized"};
    }

    // Evaluate the script with JS_EVAL_RETVAL to return expression values (needed for REPL)
    JSValue result = JS_Eval(ctx_, script.data(), script.size(), filename, JS_EVAL_RETVAL);

    // Check for exception
    if (JS_IsException(result)) {
        lastResult_.success = false;
        lastResult_.output = "";

        JSValue exception = JS_GetException(ctx_);
        lastResult_.error = jsValueToString(exception);

        ESP_LOGE(TAG, "Exception: %s", lastResult_.error.c_str());
        return lastResult_;
    }

    // Convert result to string
    lastResult_.success = true;
    lastResult_.output = jsValueToString(result);
    lastResult_.error = "";

    return lastResult_;
}

ScriptResult ScriptEngine::executeFile(const char* filepath) {
    // TODO: Implement file loading from LittleFS
    return {false, "", "File loading not yet implemented"};
}

void ScriptEngine::runREPL() {
    if (!ctx_) {
        ESP_LOGE(TAG, "Cannot run REPL - engine not initialized");
        return;
    }

    ESP_LOGI(TAG, "Starting MicroQuickJS REPL");
    ESP_LOGI(TAG, "Enter JavaScript code (type 'exit' to quit)");
    ESP_LOGI(TAG, "Examples:");
    ESP_LOGI(TAG, "  console.log('Hello, JavaScript!')");
    ESP_LOGI(TAG, "  led.on(0)");
    ESP_LOGI(TAG, "  led.setColor(0, 255, 0, 0)");
    ESP_LOGI(TAG, "  button.setLabel(1, 'Hello')");
    ESP_LOGI(TAG, "  button.onClick(1, function() { console.log('Clicked!'); })");
    Serial.print("\nmqjs> ");

    String line;
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();

            // Echo character
            Serial.write(c);

            if (c == '\n' || c == '\r') {
                // Execute the line
                if (line.length() > 0) {
                    // Check for exit command
                    if (line.equals("exit") || line.equals("quit")) {
                        ESP_LOGI(TAG, "REPL exited");
                        return;
                    }

                    evalLine(line.c_str());
                    line = "";
                }
                Serial.print("mqjs> ");
            } else if (c == '\b' || c == 127) {  // Backspace
                if (line.length() > 0) {
                    line.remove(line.length() - 1);
                    // Echo backspace visually
                    Serial.print(" \b");
                }
            } else if (c >= 32 && c < 127) {  // Printable character
                line += c;
            }
        }

        yield();  // Let other tasks run
    }
}

bool ScriptEngine::evalLine(std::string_view line) {
    auto result = execute(line, "<repl>");

    if (result.success) {
        if (!result.output.empty() && result.output != "undefined") {
            ESP_LOGI("REPL", "%s", result.output.c_str());
        }
        return true;
    } else {
        ESP_LOGE("REPL", "Error: %s", result.error.c_str());
        return false;
    }
}

// Static callback for logging
void ScriptEngine::logCallback(void* opaque, const void* buf, size_t buf_len) {
    (void)opaque;
    // Use esp_rom_printf to output directly to UART without buffering
    esp_rom_printf("%.*s", (int)buf_len, (const char*)buf);
}

std::string ScriptEngine::jsValueToString(JSValue value) {
    if (!ctx_) return "";

    if (JS_IsString(ctx_, value)) {
        JSCStringBuf buf;
        size_t len;
        const char* str = JS_ToCStringLen(ctx_, &len, value, &buf);
        return std::string(str, len);
    }

    if (JS_IsBool(value)) {
        bool val = JS_VALUE_GET_SPECIAL_VALUE(value);
        return val ? "true" : "false";
    }

    if (JS_VALUE_GET_SPECIAL_TAG(value) == JS_TAG_INT) {
        return std::to_string(JS_VALUE_GET_INT(value));
    }

    // This ensures large numbers like Date.now() timestamps are handled correctly
    if (JS_IsNumber(ctx_, value)) {
        double num;
        if (JS_ToNumber(ctx_, &num, value) == 0) {
            // Check if it's a whole number (can display without decimals)
            if (num == static_cast<int64_t>(num) && num >= -9007199254740992.0 && num <= 9007199254740992.0) {
                return std::to_string(static_cast<int64_t>(num));
            }
            // Format as floating point
            char buf[32];
            snprintf(buf, sizeof(buf), "%.6g", num);
            return buf;
        }
    }

    if (JS_IsNull(value)) {
        return "null";
    }

    if (JS_IsUndefined(value)) {
        return "undefined";
    }

    // For error objects, try to get the message
    if (JS_IsError(ctx_, value)) {
        JSCStringBuf buf;
        size_t len;

        // Try to access .message property for errors
        JSValue message = JS_GetPropertyStr(ctx_, value, "message");
        if (JS_IsString(ctx_, message)) {
            const char* msg = JS_ToCStringLen(ctx_, &len, message, &buf);
            std::string result = "Error: ";
            result += std::string(msg, len);
            return result;
        }
    }

    // For other types, return generic representation
    return "[object]";
}

void ScriptEngine::printException() {
    if (!ctx_) return;

    JSValue exception = JS_GetException(ctx_);
    std::string exceptionStr = jsValueToString(exception);
    ESP_LOGE(TAG, "Exception: %s", exceptionStr.c_str());
}

int64_t ScriptEngine::processTimers() {
    if (!ctx_) {
        return -1;  // No timers when engine not initialized
    }
    return JS_ProcessTimers(ctx_);
}

} // namespace Scripting