#include "BusAwareFileSystem.h"
#include <esp_log.h>

namespace ButtonPlus {

static const char* TAG = "BusAwareFS";

// Global instance
static std::unique_ptr<BusAwareFileSystem> g_busAwareFS;

// =============================================================================
// BusAwareFile Implementation
// =============================================================================

BusAwareFile::BusAwareFile(fs::File file, LGFX_ESP32_SPI* display)
    : file_(file), display_(display), busShared_(false) {
    if (display_ && display_->panel()) {
        busShared_ = display_->panel()->config().bus_shared;
    }
}

BusAwareFile::~BusAwareFile() {
    close();
}

bool BusAwareFile::needsBusManagement() const {
    return display_ && busShared_ && file_;
}

void BusAwareFile::acquireBus() const {
    if (display_) {
        display_->beginTransaction();
    }
}

void BusAwareFile::releaseBus() const {
    if (display_) {
        display_->endTransaction();
    }
}

size_t BusAwareFile::write(const uint8_t data) {
    return withBusManagement([&]() {
        return file_.write(data);
    });
}

size_t BusAwareFile::write(const uint8_t *buf, const size_t size) {
    return withBusManagement([&]() {
        return file_.write(buf, size);
    });
}

int BusAwareFile::available() {
    return withBusManagement([&]() {
        return file_.available();
    });
}

int BusAwareFile::read() {
    return withBusManagement([&]() {
        return file_.read();
    });
}

int BusAwareFile::peek() {
    return withBusManagement([&]() {
        return file_.peek();
    });
}

void BusAwareFile::flush() {
    withBusManagement([&]() {
        file_.flush();
        return 0; // void function, return dummy value for template
    });
}

size_t BusAwareFile::read(uint8_t *buf, size_t size) {
    return withBusManagement([&]() {
        return file_.read(buf, size);
    });
}

size_t BusAwareFile::readBytes(char *buffer, size_t length) {
    return withBusManagement([&]() {
        return file_.readBytes(buffer, length);
    });
}

bool BusAwareFile::seek(uint32_t pos, fs::SeekMode mode) {
    return withBusManagement([&]() {
        return file_.seek(pos, mode);
    });
}

size_t BusAwareFile::position() const {
    return const_cast<BusAwareFile*>(this)->withBusManagement([&]() {
        return file_.position();
    });
}

size_t BusAwareFile::size() const {
    return const_cast<BusAwareFile*>(this)->withBusManagement([&]() {
        return file_.size();
    });
}

bool BusAwareFile::setBufferSize(size_t size) {
    return withBusManagement([&]() {
        return file_.setBufferSize(size);
    });
}

void BusAwareFile::close() {
    withBusManagement([&]() {
        file_.close();
        return 0; // void function, return dummy value for template
    });
}

BusAwareFile::operator bool() const {
    return const_cast<BusAwareFile*>(this)->withBusManagement([&]() {
        return static_cast<bool>(file_);
    });
}

time_t BusAwareFile::getLastWrite() {
    return withBusManagement([&]() {
        return file_.getLastWrite();
    });
}

const char* BusAwareFile::path() const {
    return file_.path();
}

const char* BusAwareFile::name() const {
    return file_.name();
}

boolean BusAwareFile::isDirectory() {
    return withBusManagement([&]() {
        return file_.isDirectory();
    });
}

boolean BusAwareFile::seekDir(long position) {
    return withBusManagement([&]() {
        return file_.seekDir(position);
    });
}

BusAwareFile BusAwareFile::openNextFile(const char* mode) {
    return withBusManagement([&]() {
        fs::File nextFile = file_.openNextFile(mode);
        return BusAwareFile(nextFile, display_);
    });
}

String BusAwareFile::getNextFileName() {
    return withBusManagement([&]() {
        return file_.getNextFileName();
    });
}

String BusAwareFile::getNextFileName(boolean* isDir) {
    return withBusManagement([&]() {
        return file_.getNextFileName(isDir);
    });
}

void BusAwareFile::rewindDirectory() {
    withBusManagement([&]() {
        file_.rewindDirectory();
        return 0; // void function, return dummy value for template
    });
}

// =============================================================================
// BusAwareFileSystem Implementation
// =============================================================================

BusAwareFileSystem::BusAwareFileSystem(LGFX_ESP32_SPI* display)
    : display_(display), busShared_(false) {
    if (display_ && display_->panel()) {
        busShared_ = display_->panel()->config().bus_shared;
    }
}

bool BusAwareFileSystem::needsBusManagement() const {
    return display_ && busShared_;
}

void BusAwareFileSystem::acquireBus() {
    if (display_) {
        display_->beginTransaction();
    }
}

void BusAwareFileSystem::releaseBus() {
    if (display_) {
        display_->endTransaction();
    }
}

BusAwareFile BusAwareFileSystem::open(const char* path, const char* mode, const bool create) {
    return withBusManagement([&]() {
        fs::File file = LittleFS.open(path, mode, create);
        return BusAwareFile(file, display_);
    });
}

BusAwareFile BusAwareFileSystem::open(const String& path, const char* mode, const bool create) {
    return open(path.c_str(), mode, create);
}

bool BusAwareFileSystem::exists(const char* path) {
    return withBusManagement([&]() {
        return LittleFS.exists(path);
    });
}

bool BusAwareFileSystem::exists(const String& path) {
    return exists(path.c_str());
}

bool BusAwareFileSystem::remove(const char* path) {
    return withBusManagement([&]() {
        return LittleFS.remove(path);
    });
}

bool BusAwareFileSystem::remove(const String& path) {
    return remove(path.c_str());
}

bool BusAwareFileSystem::rename(const char* pathFrom, const char* pathTo) {
    return withBusManagement([&]() {
        return LittleFS.rename(pathFrom, pathTo);
    });
}

bool BusAwareFileSystem::rename(const String& pathFrom, const String& pathTo) {
    return rename(pathFrom.c_str(), pathTo.c_str());
}

bool BusAwareFileSystem::mkdir(const char* path) {
    return withBusManagement([&]() {
        return LittleFS.mkdir(path);
    });
}

bool BusAwareFileSystem::mkdir(const String& path) {
    return mkdir(path.c_str());
}

bool BusAwareFileSystem::rmdir(const char* path) {
    return withBusManagement([&]() {
        return LittleFS.rmdir(path);
    });
}

bool BusAwareFileSystem::rmdir(const String& path) {
    return rmdir(path.c_str());
}


size_t BusAwareFileSystem::totalBytes() {
    return withBusManagement([&]() {
        return LittleFS.totalBytes();
    });
}

size_t BusAwareFileSystem::usedBytes() {
    return withBusManagement([&]() {
        return LittleFS.usedBytes();
    });
}

size_t BusAwareFileSystem::freeBytes() {
    return totalBytes() - usedBytes();
}

bool BusAwareFileSystem::begin(bool formatOnFail, const char* basePath,
                               uint8_t maxOpenFiles, const char* partitionLabel) {
    return withBusManagement([&]() {
        bool result = LittleFS.begin(formatOnFail, basePath, maxOpenFiles, partitionLabel);
        if (result) {
            ESP_LOGI(TAG, "LittleFS mounted successfully");
            ESP_LOGI(TAG, "Total: %zu bytes, Used: %zu bytes, Free: %zu bytes",
                     totalBytes(), usedBytes(), freeBytes());
        } else {
            ESP_LOGE(TAG, "Failed to mount LittleFS");
        }
        return result;
    });
}

void BusAwareFileSystem::end() {
    withBusManagement([&]() {
        LittleFS.end();
        ESP_LOGI(TAG, "LittleFS unmounted");
        return 0; // void function, return dummy value for template
    });
}

// =============================================================================
// Global functions
// =============================================================================

BusAwareFileSystem& getBusAwareFileSystem() {
    if (!g_busAwareFS) {
        // Create with null display - will be set later via initializeBusAwareFileSystem
        g_busAwareFS = std::make_unique<BusAwareFileSystem>(nullptr);
    }
    return *g_busAwareFS;
}

bool initializeBusAwareFileSystem(LGFX_ESP32_SPI* display) {
    g_busAwareFS = std::make_unique<BusAwareFileSystem>(display);

    // Initialize LittleFS if not already done
    bool result = g_busAwareFS->begin(true); // Format on fail = true

    if (result) {
        ESP_LOGI(TAG, "Bus-aware filesystem initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to initialize bus-aware filesystem");
    }

    return result;
}

} // namespace ButtonPlus