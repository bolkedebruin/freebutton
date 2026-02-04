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

// Include mquickjs C headers
extern "C" {
    #include <mquickjs.h>
}

// Forward declarations
class ButtonManager;

namespace Scripting {

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
    void shutdown();
    bool isInitialized() const noexcept { return ctx_ != nullptr; }

    // Script execution
    ScriptResult execute(std::string_view script, const char* filename = "<eval>");

    // REPL support
    bool evalLine(std::string_view line);  // Evaluate single line (non-blocking)

    // Get last result as string
    std::string getLastOutput() const { return lastResult_.output; }
    std::string getLastError() const { return lastResult_.error; }

    // Get the JSContext for advanced operations (like timer processing)
    JSContext* getContext() const { return ctx_; }

    // Process JavaScript timers - call this regularly from main loop
    // Returns milliseconds until next timer, or -1 if no timers pending
    int64_t processTimers() const;

private:
    explicit ScriptEngine(size_t heapSize = DEFAULT_HEAP_SIZE);
    ~ScriptEngine();

    // Memory management
    uint8_t* heapBuffer_{nullptr};
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