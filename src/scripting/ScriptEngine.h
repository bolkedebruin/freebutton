//
// ScriptEngine - JavaScript scripting engine for ESP32 using MQuickJS
// Phase 1: Basic engine with LED control
// Memory: 10KB heap (mquickjs minimum requirement)
//

#pragma once

#include <atomic>
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
// A script restart (ScriptEngine::restart()) holds it from the first step to
// the last: the other tasks see the old context or the new one, never the
// time between.
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

    // Loads and runs the boot script: "main.jsc", else "script.jsc", from the
    // js_user partition (checksum and relocation checked), then builds fb.
    // Without a loadable script the engine still gets fb, so the REPL works.
    // Needs a fresh context (init() or restart()): no fb yet. Takes the
    // engine lock. Loop task. Sets scriptStarts()/scriptError.
    void startUserScript();

    // Replaces the running script by the stored boot script without a
    // reboot. Under the engine lock, in this order:
    //   1. js_bindings_reset(): the firmware forgets the old script's timers,
    //      key handlers, MQTT subscriptions and sensor handlers;
    //   2. the old context is freed;
    //   3. the mapping and the flash pins of the old bytecode are released
    //      (BytecodeRegistry::unpinAll()), so its region can be reused;
    //   4. ConfigManager::restoreConfigState(): the device shows what
    //      config.json says;
    //   5. a new context on the SAME heap buffer (no allocation);
    //   6. startUserScript().
    // When the new bytecode does not load, the old script is gone all the
    // same: the device runs without a script and scriptError says why.
    // Loop task only, and never from script code (a binding that wants a
    // restart calls requestRestart()): the context it runs in would be freed
    // under it. False when the engine was never initialized.
    bool restart();

    // Asks the loop task for a restart. Any task (the bytecode upload runs
    // on async_tcp). Requests are not counted: several before the loop task
    // gets to it give one restart, and that one loads the newest stored
    // script. A request made while a restart runs gives one more restart.
    // False when the engine is not initialized (the request is kept).
    bool requestRestart();

    // Loop task, every pass: performs a requested restart. True when it did.
    bool processRestartRequest();

    // Number of times a user script was started since boot (1 after a normal
    // boot with a script, +1 per restart that got as far as running it).
    uint32_t scriptStarts() const noexcept { return scriptStarts_.load(std::memory_order_relaxed); }

    // Result of the last start: "" when the script ran to its end without an
    // exception; else the exception text (cut to fit) or one of "no script",
    // "other firmware build", "map failed", "checksum mismatch",
    // "relocation mismatch", "no engine". Copies at most `size` bytes, always
    // terminated. Any task, no lock: a copy taken during a restart can mix
    // the old and the new text.
    static constexpr size_t kScriptErrorSize = 64;
    void copyScriptError(char* out, size_t size) const;
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

    // JS heap: used = live data after a garbage collection, free = what is
    // left for new objects and the stack; size = the whole heap buffer.
    // Collects garbage, under the engine lock with a short wait; while the
    // engine is busy (a restart included) or not initialized it returns the
    // last measurement (0 before the first). Any task; for diagnostics
    // (sensors, /api/device).
    struct HeapUsage {
        size_t used{0};
        size_t free{0};
        size_t size{0};
    };
    HeapUsage heapUsage() const;

private:
    explicit ScriptEngine(size_t heapSize = DEFAULT_HEAP_SIZE);
    ~ScriptEngine();

    // Memory management
    uint8_t* heapBuffer_{nullptr};
    bool deviceApiReady_{false};
    size_t heapSize_{0};
    // Equal to heapBuffer_ while set (mquickjs puts the context at the start
    // of its heap), so a restart does not change its value.
    JSContext* ctx_{nullptr};

    // Last heapUsage() measurement. Written under the engine lock.
    mutable HeapUsage lastHeap_;

    // Restart request and script status (see the public accessors).
    std::atomic<bool> restartRequested_{false};
    std::atomic<uint32_t> scriptStarts_{0};
    char scriptError_[kScriptErrorSize] = "no script";

    // Execution state
    ScriptResult lastResult_;

    // New context on heapBuffer_, with everything a fresh context needs.
    bool createContext();
    // Finds, checks, loads and runs the boot script (engine lock held).
    void runBootScript();
    void setScriptError(const char* message);
    // GC and measurement into lastHeap_ (engine lock held).
    HeapUsage measureHeap() const;

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