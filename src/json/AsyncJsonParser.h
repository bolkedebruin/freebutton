#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "FilteredReaderHandler.h"
#include <functional>
#include <memory>

namespace json {

/**
 * @brief Asynchronous JSON parser that processes JSON data in chunks
 * 
 * This parser is designed to handle large JSON documents that arrive in chunks,
 * particularly useful for web server request bodies. It uses RapidJSON for parsing
 * and runs in a separate FreeRTOS task to avoid blocking the main thread.
 * 
 * Features:
 * - Asynchronous processing using FreeRTOS tasks
 * - Streaming parser that handles partial JSON data
 * - Thread-safe implementation with semaphores
 * - Integration with FilteredReaderHandler for selective parsing
 * - Memory-efficient design for embedded systems
 */
class AsyncJsonParser {
public:
    /**
     * @brief Constructor
     * 
     * @param handler The FilteredReaderHandler that will process the parsed JSON
     */
    explicit AsyncJsonParser(FilteredReaderHandler* handler);
    
    /**
     * @brief Destructor
     * 
     * Cleans up resources and ensures the parsing task is properly terminated
     */
    ~AsyncJsonParser();
    
    /**
     * @brief Parse a chunk of JSON data
     * 
     * This method can be called multiple times with consecutive chunks of JSON data.
     * The parser will process each chunk asynchronously and signal completion.
     * 
     * @param buffer Pointer to the JSON data chunk
     * @param length Length of the data chunk in bytes
     */
    void ParsePart(const char* buffer, size_t length);
    
    /**
     * @brief Check if parsing is completed
     * 
     * @return true if parsing is completed, false otherwise
     */
    bool IsCompleted() const;
    
    /**
     * @brief Check if parsing has been started
     * 
     * @return true if parsing has been started, false otherwise
     */
    bool IsStarted() const;
    
    /**
     * @brief Get the current parsing offset
     * 
     * @return The number of characters processed so far
     */
    size_t GetOffset() const;
    
private:
    /**
     * @brief Custom string stream for asynchronous JSON parsing
     * 
     * This stream implementation works with RapidJSON's reader to provide
     * asynchronous access to JSON data that arrives in chunks.
     */
    struct AsyncStringStream {
        typedef char Ch;
        
        explicit AsyncStringStream(AsyncJsonParser& parser);
        
        // RapidJSON stream interface
        char Peek() const;
        char Take();
        size_t Tell() const;
        char* PutBegin();
        void Put(char c);
        void Flush();
        size_t PutEnd(char* begin);
        bool Empty() const;
        
    private:
        AsyncJsonParser& parser_;
        const char* src_;
        const char* end_;
        size_t count_;
        
        friend class AsyncJsonParser;
    };
    
    /**
     * @brief Static task function for FreeRTOS
     * 
     * @param pvParameters Pointer to the AsyncJsonParser instance
     */
    static void parseTask(void* pvParameters);
    
    /**
     * @brief Main parsing method that runs in the FreeRTOS task
     */
    void Parse();
    
    /**
     * @brief Initialize semaphores and task
     * 
     * @return true if initialization succeeded, false otherwise
     */
    bool Initialize();
    
    /**
     * @brief Cleanup semaphores and task
     */
    void Cleanup();
    
    // Member variables
    FilteredReaderHandler* streamingParser_;
    AsyncStringStream stream_;
    TaskHandle_t parseTaskHandle_;
    SemaphoreHandle_t mutex_;
    SemaphoreHandle_t notEmpty_;
    SemaphoreHandle_t finish_;
    bool completed_;
    bool started_;
    bool initialized_;
    
    // Constants
    static constexpr size_t PARSE_TASK_STACK_SIZE = 8096;
    static constexpr UBaseType_t PARSE_TASK_PRIORITY = tskIDLE_PRIORITY + 2;
    static constexpr const char* PARSE_TASK_NAME = "AsyncJsonParser";
};

/**
 * @brief Factory function to create an AsyncJsonParser with a FilteredReaderHandler
 * 
 * @param callback The callback function to handle parsed JSON data
 * @return Unique pointer to the created AsyncJsonParser
 */
std::unique_ptr<AsyncJsonParser> CreateAsyncJsonParser(
    std::function<void(const char* data, size_t length)> callback);

}; // namespace json