#ifndef BUSAWAREFILESYSTEM_H
#define BUSAWAREFILESYSTEM_H

#include <FS.h>
#include <LittleFS.h>
#include <memory>
#include "DisplayManager.h"

namespace FreeButton {

/**
 * @brief Bus-aware filesystem wrapper that integrates with LovyanGFX bus sharing
 *
 * This class wraps the standard FS interface and automatically manages SPI bus
 * arbitration when the display and filesystem share the same SPI bus. It uses
 * the same mechanism as LovyanGFX's drawJpgFile function.
 */
class BusAwareFile : public Stream {
public:
    BusAwareFile(fs::File file = fs::File(), LGFX_ESP32_SPI* display = nullptr);
    ~BusAwareFile();

    // Stream interface
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *buf, size_t size) override;
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    // File-specific methods
    size_t read(uint8_t *buf, size_t size);
    size_t readBytes(char *buffer, size_t length);
    bool seek(uint32_t pos, fs::SeekMode mode = fs::SeekSet);
    size_t position() const;
    size_t size() const;
    bool setBufferSize(size_t size);
    void close();
    operator bool() const;
    time_t getLastWrite();
    const char* path() const;
    const char* name() const;

    // Directory methods
    boolean isDirectory();
    boolean seekDir(long position);
    BusAwareFile openNextFile(const char* mode = FILE_READ);
    String getNextFileName();
    String getNextFileName(boolean* isDir);
    void rewindDirectory();

private:
    fs::File file_;
    LGFX_ESP32_SPI* display_;
    bool busShared_;

    void acquireBus() const;
    void releaseBus() const;
    bool needsBusManagement() const;

    template<typename Func>
    auto withBusManagement(Func&& func) -> decltype(func()) {
        if (needsBusManagement()) {
            releaseBus();
        }
        auto result = func();
        if (needsBusManagement()) {
            acquireBus();
        }
        return result;
    }
};

/**
 * @brief Bus-aware filesystem wrapper
 *
 * This class implements the FS interface and automatically manages SPI bus
 * arbitration when filesystem operations occur on a shared SPI bus.
 */
class BusAwareFileSystem {
public:
    explicit BusAwareFileSystem(LGFX_ESP32_SPI* display = nullptr);
    ~BusAwareFileSystem() = default;

    // Core file operations
    BusAwareFile open(const char* path, const char* mode = FILE_READ, const bool create = false);
    BusAwareFile open(const String& path, const char* mode = FILE_READ, const bool create = false);

    bool exists(const char* path);
    bool exists(const String& path);

    bool remove(const char* path);
    bool remove(const String& path);

    bool rename(const char* pathFrom, const char* pathTo);
    bool rename(const String& pathFrom, const String& pathTo);

    bool mkdir(const char* path);
    bool mkdir(const String& path);

    bool rmdir(const char* path);
    bool rmdir(const String& path);


    // File info
    size_t totalBytes();
    size_t usedBytes();
    size_t freeBytes();

    // Initialize filesystem if not already done
    bool begin(bool formatOnFail = false, const char* basePath = "/littlefs",
               uint8_t maxOpenFiles = 10, const char* partitionLabel = "spiffs");
    void end();

    // Get underlying LittleFS reference for compatibility
    fs::LittleFSFS& getLittleFS() { return LittleFS; }

private:
    LGFX_ESP32_SPI* display_;
    bool busShared_;

    void acquireBus();
    void releaseBus();
    bool needsBusManagement() const;

    template<typename Func>
    auto withBusManagement(Func&& func) -> decltype(func()) {
        if (needsBusManagement()) {
            releaseBus();
        }
        auto result = func();
        if (needsBusManagement()) {
            acquireBus();
        }
        return result;
    }
};

// Global functions
BusAwareFileSystem& getBusAwareFileSystem();
bool initializeBusAwareFileSystem(LGFX_ESP32_SPI* display);

} // namespace FreeButton

#endif // BUSAWAREFILESYSTEM_H