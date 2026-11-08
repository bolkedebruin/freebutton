//
// ScriptEngine - JavaScript scripting engine for ESP32 using MQuickJS
// Phase 1: Basic engine with LED control
// Memory: 10KB heap (mquickjs minimum requirement)
//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <functional>

#include <freertos/FreeRTOS.h>

// Include mquickjs C headers
extern "C" {
    #include <mquickjs.h>
}

// Forward declarations
class ButtonManager;

namespace Scripting {

// The JS engine lock. mquickjs is single-threaded: its context, its stack and
// the binding tables (timers, button/sensor/MQTT callbacks) must be used by
// one task at a time. Every entry into script code holds this lock:
//   * loop task: the user script in setup(), JS timers, MQTT messages and
//     REPL requests (both handed over by their own tasks, see below), and
//     sensor onChange callbacks (sampleSensors()).
//   * button task: button onClick/onLongPress/onRelease, which must return
//     synchronously (a handler returning false cancels the press).
// Tasks that must NOT take it:
//   * MQTT tasks: they copy messages into a bounded ring and return
//     (mqtt_binding.cpp); waiting for script code would stop them draining
//     the socket.
//   * async_tcp: REPL requests are parked in a slot and run by the loop task
//     (WebSocketREPL::processPending()).
//   * LVGL task / lv_timer callbacks.
// Lock order: JS lock -> {LVGL lock, broker subscriptionsLock_, JS MQTT queue
// lock, ConfigManager display-items lock}. None of those is ever held while
// taking the JS lock. Recursive, statically allocated.
class EngineLock {
public:
    explicit EngineLock(TickType_t wait = portMAX_DELAY);
    ~EngineLock();
    // False when the wait timed out: do not enter the engine.
    [[nodiscard]] bool owned() const noexcept { return owned_; }

    EngineLock(const EngineLock&) = delete;
    EngineLock& operator=(const EngineLock&) = delete;

private:
    bool owned_{false};
};

// Result of script execution
struct ScriptResult {
    bool success{false};
    std::string output;
    std::string error;
};

// Main scripting engine interface
class ScriptEngine {
public:
    static constexpr size_t DEFAULT_HEAP_SIZE = 10 * 1024;  // 10KB minimum for mquickjs

    // Singleton access
    static ScriptEngine& getInstance(size_t heapSize = DEFAULT_HEAP_SIZE);

    // Lifecycle
    bool init();
    // Builds the fb object tree. Its property names become RAM atoms, and
    // JS_LoadBytecode() refuses to load while any RAM atom exists ("no atom
    // must be defined in RAM"). So: init(), load the user bytecode, then
    // initDeviceApi(), then run the script. Idempotent.
    void initDeviceApi();
    void shutdown();
    bool isInitialized() const noexcept { return ctx_ != nullptr; }

    // Script execution (takes the engine lock)
    ScriptResult execute(std::string_view script, const char* filename = "<eval>");

    // REPL support
    bool evalLine(std::string_view line);  // Evaluate single line (non-blocking)

    // Get last result as string
    std::string getLastOutput() const { return lastResult_.output; }
    std::string getLastError() const { return lastResult_.error; }

    // Get the JSContext for advanced operations (like timer processing)
    JSContext* getContext() const { return ctx_; }

    // Process JavaScript timers - call this regularly from main loop (takes the
    // engine lock). Returns milliseconds until next timer, or -1 if no timers pending
    int64_t processTimers() const;

private:
    explicit ScriptEngine(size_t heapSize = DEFAULT_HEAP_SIZE);
    ~ScriptEngine();

    // Memory management
    uint8_t* heapBuffer_{nullptr};
    bool deviceApiReady_{false};
    size_t heapSize_{0};
    JSContext* ctx_{nullptr};

    // Execution state
    ScriptResult lastResult_;

    // Helper functions
    static void logCallback(void* opaque, const void* buf, size_t buf_len);
    std::string jsValueToString(JSValue value) const;
    void printException() const;

    // Disable copy and move
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;
    ScriptEngine(ScriptEngine&&) = delete;
    ScriptEngine& operator=(ScriptEngine&&) = delete;
};

} // namespace Scripting