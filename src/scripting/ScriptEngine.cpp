//
// ScriptEngine - JavaScript scripting engine implementation
//

#include "ScriptEngine.h"
#include "WebSocketREPL.h"
#include <Arduino.h>
#include <cstring>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <freertos/semphr.h>

static const char* TAG = "ScriptEngine";

// Include mquickjs C headers
extern "C" {
    #include <mquickjs.h>
    #include <mquickjs_ext.h>

    // External declaration for the generated FreeButton stdlib
    // This is defined in lib/mquickjs/src/stdlib_export.c which includes the generated header
    extern const JSSTDLibraryDef js_stdlib;

    // Initialize the fb object tree (hierarchical device API, built entirely in C)
    void device_hw_init_fb(void* ctx);
}

namespace Scripting {

namespace {

// Created on first use (at run time, from setup()), never deleted.
SemaphoreHandle_t engineMutex() {
    static StaticSemaphore_t buffer;
    static SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutexStatic(&buffer);
    return handle;
}

} // namespace

EngineLock::EngineLock(const TickType_t wait)
    : owned_(xSemaphoreTakeRecursive(engineMutex(), wait) == pdTRUE) {
}

EngineLock::~EngineLock() {
    if (owned_) {
        xSemaphoreGiveRecursive(engineMutex());
    }
}

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

    // The fb object tree is built later, by initDeviceApi(): it creates RAM
    // atoms, which would stop the user bytecode from loading.

    ESP_LOGI(TAG, "JavaScript API initialized with %zu bytes heap", heapSize_);

    return true;
}

void ScriptEngine::initDeviceApi() {
    if (!ctx_ || deviceApiReady_) {
        return;
    }
    device_hw_init_fb(ctx_);
    deviceApiReady_ = true;
}

void ScriptEngine::shutdown() {
    if (ctx_) {
        JS_FreeContext(ctx_);
        ctx_ = nullptr;
        deviceApiReady_ = false;
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

    EngineLock lock;

    // Copy to std::string to ensure null-termination
    // mquickjs JS_Eval requires input[input_len] = '\0'
    const std::string scriptStr(script);

    // Evaluate the script with JS_EVAL_RETVAL to return expression values (needed for REPL)
    const JSValue result = JS_Eval(ctx_, scriptStr.c_str(), scriptStr.size(), filename, JS_EVAL_RETVAL);

    // Check for exception
    if (JS_IsException(result)) {
        lastResult_.success = false;
        lastResult_.output = "";

        const JSValue exception = JS_GetException(ctx_);
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

std::string ScriptEngine::jsValueToString(const JSValue value) const {
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

void ScriptEngine::printException() const {
    if (!ctx_) return;

    JSValue exception = JS_GetException(ctx_);
    std::string exceptionStr = jsValueToString(exception);
    ESP_LOGE(TAG, "Exception: %s", exceptionStr.c_str());
}

int64_t ScriptEngine::processTimers() const {
    if (!ctx_) {
        return -1;  // No timers when engine not initialized
    }
    EngineLock lock;
    return JS_ProcessTimers(ctx_);
}

} // namespace Scripting