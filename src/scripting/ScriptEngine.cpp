//
// ScriptEngine - JavaScript scripting engine implementation
//

#include "ScriptEngine.h"
#include "WebSocketREPL.h"
#include "BytecodeRegistry.h"
#include "components/ConfigManager.h"
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

    #include "file_hardware_mmap.h"
    #include "js/js_reset.h"
    #include <esp_crc.h>
}

// The device configuration (main.cpp). restart() puts back what the old
// script changed.
extern freebutton::ConfigManager configManager;

namespace Scripting {

namespace {

// Created on first use (at run time, from setup()), never deleted.
SemaphoreHandle_t engineMutex() {
    static StaticSemaphore_t buffer;
    static SemaphoreHandle_t handle = xSemaphoreCreateRecursiveMutexStatic(&buffer);
    return handle;
}

// Mapping of the boot script that the current context executes from flash.
// Engine lock held for every access. Unmapped when that context is freed.
MappedFile* bootScriptMap = nullptr;

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

    if (!createContext()) {
        delete[] heapBuffer_;
        heapBuffer_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "JavaScript API initialized with %zu bytes heap", heapSize_);

    return true;
}

// Everything a fresh context needs. Used by init() and by restart(): the
// context lives at the start of heapBuffer_, so all of its state (log
// function, atoms, globals, GC ref list) starts empty again.
bool ScriptEngine::createContext() {
    deviceApiReady_ = false;

    // Create JSContext with our custom stdlib (includes standard library + LED bindings)
    ctx_ = JS_NewContext(heapBuffer_, heapSize_, &js_stdlib);
    if (!ctx_) {
        ESP_LOGE(TAG, "Failed to create JSContext");
        return false;
    }

    // Set log callback for internal mquickjs logging
    JS_SetLogFunc(ctx_, logCallback);

    // Set console callback for WebSocket REPL
    // This enables print()/console.log() to be sent to WebSocket clients
    JS_SetConsoleWriteFunc(websocketConsoleCallback);

    // The fb object tree is built later, by initDeviceApi(): it creates RAM
    // atoms, which would stop the user bytecode from loading.
    return true;
}

void ScriptEngine::initDeviceApi() {
    if (!ctx_ || deviceApiReady_) {
        return;
    }
    device_hw_init_fb(ctx_);
    deviceApiReady_ = true;
}

void ScriptEngine::setScriptError(const char* message) {
    // The last byte only ever gets a 0: a reader on another task always
    // finds a terminator.
    strlcpy(scriptError_, message ? message : "", sizeof(scriptError_));
}

void ScriptEngine::copyScriptError(char* out, const size_t size) const {
    if (!out || size == 0) return;
    strlcpy(out, scriptError_, size);
}

void ScriptEngine::startUserScript() {
    if (!ctx_) {
        setScriptError("no engine");
        return;
    }
    // The button task may call into the handlers this script registers
    // while it is still running; the engine lock keeps them out.
    const EngineLock lock;
    if (deviceApiReady_) {
        // fb exists: its RAM atoms stop JS_LoadBytecode(). Only restart()
        // gives a context that can load again.
        ESP_LOGE(TAG, "startUserScript: context already in use, not loading");
        return;
    }
    runBootScript();
    // No (loadable) user script: the REPL and later scripts still need fb.
    initDeviceApi();
}

// Must load bytecode BEFORE executing any JavaScript to avoid RAM atom conflicts.
void ScriptEngine::runBootScript() {
    // Find bytecode in registry: the boot script, else the legacy name
    // (BytecodeRegistry::bootScriptName). A found entry is pinned.
    BytecodeEntry entry;
    const char* bootName = BytecodeRegistry::bootScriptName(
        [&entry](const char* name) { return bytecode_registry_find(name, &entry); });
    if (bootName == nullptr) {
        // Stored for another firmware build (the registry logged it), or nothing stored.
        bool stale = false;
        {
            BytecodeRegistry::Lock registryLock;
            auto& registry = BytecodeRegistry::instance();
            stale = BytecodeRegistry::bootScriptName([&registry](const char* name) {
                        return registry.find(name) != nullptr;
                    }) != nullptr;
        }
        if (stale) {
            setScriptError("other firmware build");
        } else {
            ESP_LOGI(TAG, "No user script found");
            ESP_LOGI(TAG, "Upload %s via web interface to run custom code", kBootScriptName);
            setScriptError("no script");
        }
        return;
    }
    ESP_LOGI(TAG, "Found user script '%s' in bytecode registry", entry.name);

    // Memory map from flash: the image, which lies behind the entry's header.
    // The partition is mapped once by the registry; this gives the same
    // address and one small bookkeeping block.
    MappedFile* mapped = file_hw_mmap("js_user", entry.imageOffset(), entry.size);
    if (!mapped) {
        ESP_LOGE(TAG, "Failed to memory map user script");
        setScriptError("map failed");
        return;
    }

    // Verify checksum
    const uint32_t checksum = esp_crc32_le(0, mapped->data, mapped->size);
    if (checksum != entry.checksum) {
        ESP_LOGE(TAG, "Bytecode checksum mismatch");
        file_hw_munmap(mapped);
        setScriptError("checksum mismatch");
        return;
    }

    // Verify bytecode relocation before loading
    const auto* hdr = reinterpret_cast<const JSBytecodeHeader*>(mapped->data);
    const uintptr_t expected_base = reinterpret_cast<uintptr_t>(hdr + 1);
    ESP_LOGI(TAG, "Bytecode header: base_addr=0x%lx, expected=0x%lx",
             (unsigned long)hdr->base_addr, (unsigned long)expected_base);
    if (hdr->base_addr != expected_base) {
        ESP_LOGE(TAG, "Bytecode relocation mismatch! Need to re-upload.");
        file_hw_munmap(mapped);
        setScriptError("relocation mismatch");
        return;
    }

    // Load bytecode directly from flash (zero-copy, already relocated)
    const JSValue userFunc = JS_LoadBytecode(ctx_, mapped->data);
    if (JS_IsException(userFunc)) {
        ESP_LOGE(TAG, "Failed to load bytecode");
        const JSValue exception = JS_GetException(ctx_);
        JSCStringBuf buf;
        const char* err = JS_ToCString(ctx_, exception, &buf);
        ESP_LOGE(TAG, "Error: %s", err ? err : "?");
        setScriptError(err && *err ? err : "load failed");
        file_hw_munmap(mapped);
        return;
    }
    ESP_LOGI(TAG, "User bytecode loaded successfully");
    // The context executes it in place: mapped until the context is freed.
    bootScriptMap = mapped;

    // fb only now: its RAM atoms would have blocked the load.
    initDeviceApi();
    scriptStarts_.fetch_add(1, std::memory_order_relaxed);
    const JSValue result = JS_Run(ctx_, userFunc);

    if (JS_IsException(result)) {
        ESP_LOGE(TAG, "User script execution failed");
        const JSValue exception = JS_GetException(ctx_);
        JSCStringBuf buf;
        const char* err = JS_ToCString(ctx_, exception, &buf);
        if (err) {
            ESP_LOGE(TAG, "Error: %s", err);
        }
        // Copied before the next engine call: an allocation may move the string.
        setScriptError(err && *err ? err : "exception");

        // Also try to get the stack trace
        const JSValue stack = JS_GetPropertyStr(ctx_, exception, "stack");
        if (!JS_IsUndefined(stack)) {
            JSCStringBuf stack_buf;
            const char* stack_str = JS_ToCString(ctx_, stack, &stack_buf);
            if (stack_str) {
                ESP_LOGE(TAG, "Stack: %s", stack_str);
            }
        }
        return;
    }

    ESP_LOGI(TAG, "User script executed successfully");
    setScriptError("");

    // Print the result
    JSCStringBuf buf;
    const char* result_str = JS_ToCString(ctx_, result, &buf);
    if (result_str) {
        ESP_LOGI(TAG, "Result: %s", result_str);
    }
}

bool ScriptEngine::requestRestart() {
    restartRequested_.store(true, std::memory_order_release);
    return ctx_ != nullptr;
}

bool ScriptEngine::processRestartRequest() {
    // Cleared BEFORE the restart: a request that arrives while it runs stays
    // set and gives another restart on the next pass. Only the loop task
    // gets here, so two restarts never run at once.
    if (!restartRequested_.exchange(false, std::memory_order_acq_rel)) {
        return false;
    }
    return restart();
}

bool ScriptEngine::restart() {
    if (!ctx_ || !heapBuffer_) {
        ESP_LOGE(TAG, "Restart: engine not initialized");
        return false;
    }

    // Held to the end. The button task waits for it (up to 1 s, then the
    // press takes its default action), heapUsage() on other tasks returns
    // its last measurement. REPL lines, MQTT messages and timers are run by
    // this task, so none of them runs in between: a REPL line that arrives
    // now is parked and evaluated in the new context.
    const EngineLock lock;
    const uint32_t startMs = millis();

    // Measured (with a GC) before the reset: after it the old context must
    // not collect garbage any more (js_reset.h).
    const HeapUsage before = measureHeap();

    js_bindings_reset(ctx_);
    JS_FreeContext(ctx_);

    // Nothing executes the old bytecode now: its mapping and its flash
    // regions are free. An upload in progress keeps its own reservation.
    if (bootScriptMap) {
        file_hw_munmap(bootScriptMap);
        bootScriptMap = nullptr;
    }
    BytecodeRegistry::instance().unpinAll();

    configManager.restoreConfigState();

    // Same buffer: the general heap is too fragmented to give 16 KB again.
    if (!createContext()) {
        setScriptError("no engine");
        return false;
    }
    startUserScript();

    const HeapUsage after = measureHeap();
    ESP_LOGI(TAG, "Script restarted in %lu ms (start %lu, error \"%s\"): JS heap free %zu -> %zu of %zu bytes",
             static_cast<unsigned long>(millis() - startMs),
             static_cast<unsigned long>(scriptStarts()), scriptError_,
             before.free, after.free, after.size);
    return true;
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

ScriptEngine::HeapUsage ScriptEngine::measureHeap() const {
    JS_GC(ctx_);
    size_t used = 0;
    size_t free = 0;
    JS_GetMemoryUsage(ctx_, &used, &free);
    lastHeap_.used = used;
    lastHeap_.free = free;
    lastHeap_.size = heapSize_;
    return lastHeap_;
}

ScriptEngine::HeapUsage ScriptEngine::heapUsage() const {
    // lastHeap_: the last measurement, for callers while the engine is busy.
    // Written only under the engine lock; the fields are word-sized (a torn
    // read between them only mixes two measurements).
    if (ctx_ != nullptr) {
        const EngineLock lock(pdMS_TO_TICKS(20));
        if (lock.owned()) {
            return measureHeap();
        }
    }
    return lastHeap_;
}

int64_t ScriptEngine::processTimers() const {
    if (!ctx_) {
        return -1;  // No timers when engine not initialized
    }
    EngineLock lock;
    return JS_ProcessTimers(ctx_);
}

} // namespace Scripting