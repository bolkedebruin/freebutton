//
// Bytecode Registry - Implementation
// Manages user-uploaded JavaScript bytecode metadata
//

#include "BytecodeRegistry.h"
#include <ArduinoJson.h>
#include <esp_log.h>
#include <esp_crc.h>
#include "drivers/BusAwareFileSystem.h"

using namespace FreeButton;

static auto TAG = "BytecodeRegistry";

BytecodeRegistry::BytecodeRegistry()
    : user_partition_(nullptr)
    , next_offset_(0)
    , virtual_base_addr_(0)
    , mmap_handle_(0) {
    initPartition();
    initVirtualMapping();
}

BytecodeRegistry::~BytecodeRegistry() = default;

bool BytecodeRegistry::initPartition() {
    user_partition_ = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_ANY,
        "js_user"
    );

    if (!user_partition_) {
        ESP_LOGE(TAG, "js_user partition not found");
        return false;
    }

    ESP_LOGI(TAG, "Found js_user partition at 0x%lx, size %lu bytes",
             user_partition_->address, user_partition_->size);

    return true;
}

bool BytecodeRegistry::load() {
    entries_.clear();
    next_offset_ = 0;

    if (!loadFromFile(METADATA_PATH)) {
        ESP_LOGW(TAG, "No metadata file found or failed to load, starting fresh");
        return true;  // Not an error, just empty
    }

    // Calculate next offset from entries
    for (const auto& entry : entries_) {
        if (entry.active) {
            uint32_t entry_end = entry.offset + entry.size;
            if (entry_end > next_offset_) {
                next_offset_ = entry_end;
            }
        }
    }

    // Align to 4KB boundary (flash erase sector size)
    next_offset_ = (next_offset_ + 0xFFF) & ~0xFFF;

    ESP_LOGI(TAG, "Loaded %zu entries, next offset: 0x%lx",
             entries_.size(), next_offset_);

    return true;
}

bool BytecodeRegistry::save() {
    return saveToFile(METADATA_PATH);
}

bool BytecodeRegistry::add(const char* name, uint32_t offset, uint32_t size,
                           uint32_t checksum, uint32_t flash_addr) {
    if (!name || strlen(name) >= 32) {
        ESP_LOGE(TAG, "Invalid name");
        return false;
    }

    // Check if name already exists
    for (auto& entry : entries_) {
        if (strcmp(entry.name, name) == 0) {
            // Update existing entry
            entry.offset = offset;
            entry.size = size;
            entry.checksum = checksum;
            entry.flash_addr = flash_addr;
            entry.active = true;
            entry.version = 1;
            ESP_LOGI(TAG, "Updated entry '%s'", name);
            return true;
        }
    }

    // Add new entry
    BytecodeEntry entry;
    strncpy(entry.name, name, sizeof(entry.name) - 1);
    entry.name[sizeof(entry.name) - 1] = '\0';
    entry.offset = offset;
    entry.size = size;
    entry.checksum = checksum;
    entry.flash_addr = flash_addr;
    entry.active = true;
    entry.version = 1;

    entries_.push_back(entry);

    ESP_LOGI(TAG, "Added entry '%s' at offset 0x%lx, size %lu bytes",
             name, offset, size);

    return true;
}

const BytecodeEntry* BytecodeRegistry::find(const char* name) const {
    if (!name) return nullptr;

    for (const auto& entry : entries_) {
        if (entry.active && strcmp(entry.name, name) == 0) {
            return &entry;
        }
    }

    return nullptr;
}

std::vector<BytecodeEntry> BytecodeRegistry::list() const {
    std::vector<BytecodeEntry> active_entries;
    for (const auto& entry : entries_) {
        if (entry.active) {
            active_entries.push_back(entry);
        }
    }
    return active_entries;
}

bool BytecodeRegistry::remove(const char* name) {
    if (!name) return false;

    for (auto& entry : entries_) {
        if (strcmp(entry.name, name) == 0) {
            entry.active = false;
            ESP_LOGI(TAG, "Marked entry '%s' as inactive", name);
            return true;
        }
    }

    return false;
}

uint32_t BytecodeRegistry::getNextOffset() const {
    return next_offset_;
}

size_t BytecodeRegistry::getFreeSpace() const {
    if (!user_partition_) return 0;

    if (next_offset_ >= user_partition_->size) {
        return 0;
    }

    return user_partition_->size - next_offset_;
}

uint32_t BytecodeRegistry::getPartitionAddress() const {
    if (!user_partition_) return 0;
    return user_partition_->address;
}

bool BytecodeRegistry::loadFromFile(const char* path) {
    auto& fs = getBusAwareFileSystem();

    if (!fs.exists(path)) {
        return false;
    }

    auto file = fs.open(path, FILE_READ);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open metadata file for reading");
        return false;
    }

    // Read file content
    String content = file.readString();
    file.close();

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);

    if (error) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", error.c_str());
        return false;
    }

    // Load entries
    auto array = doc["entries"].as<JsonArray>();
    for (JsonObject obj : array) {
        BytecodeEntry entry;

        const char* name = obj["name"];
        if (!name) continue;

        strncpy(entry.name, name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';

        entry.offset = obj["offset"].as<uint32_t>();
        entry.size = obj["size"].as<uint32_t>();
        entry.checksum = obj["checksum"].as<uint32_t>();
        entry.flash_addr = obj["flash_addr"].as<uint32_t>();
        entry.version = obj["version"].as<uint32_t>();
        entry.active = obj["active"].as<bool>();

        entries_.push_back(entry);
    }

    ESP_LOGI(TAG, "Loaded %zu entries from %s", entries_.size(), path);
    return true;
}

bool BytecodeRegistry::saveToFile(const char* path) {
    auto& fs = getBusAwareFileSystem();

    auto file = fs.open(path, FILE_WRITE);
    if (!file) {
        ESP_LOGE(TAG, "Failed to open metadata file for writing");
        return false;
    }

    // Create JSON document
    JsonDocument doc;
    auto array = doc["entries"].to<JsonArray>();

    for (const auto& entry : entries_) {
        auto obj = array.add<JsonObject>();
        obj["name"] = entry.name;
        obj["offset"] = entry.offset;
        obj["size"] = entry.size;
        obj["checksum"] = entry.checksum;
        obj["flash_addr"] = entry.flash_addr;
        obj["version"] = entry.version;
        obj["active"] = entry.active;
    }

    // Serialize to file
    size_t written = serializeJson(doc, file);
    file.close();

    if (written == 0) {
        ESP_LOGE(TAG, "Failed to write JSON to file");
        return false;
    }

    ESP_LOGI(TAG, "Saved %zu entries to %s (%zu bytes)",
             entries_.size(), path, written);

    return true;
}

bool BytecodeRegistry::initVirtualMapping() {
    if (!user_partition_) {
        ESP_LOGE(TAG, "Cannot init virtual mapping: partition not initialized");
        return false;
    }

    // Map entire partition (256KB) permanently
    const void* mapped_ptr = nullptr;
    esp_err_t err = esp_partition_mmap(
        user_partition_,
        0,                      // offset 0
        MAX_PARTITION_SIZE,     // 256KB
        ESP_PARTITION_MMAP_DATA,
        &mapped_ptr,
        &mmap_handle_
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mmap partition: 0x%x", err);
        return false;
    }

    virtual_base_addr_ = reinterpret_cast<uint32_t>(mapped_ptr);

    ESP_LOGI(TAG, "Partition permanently mapped to virtual address 0x%lx", virtual_base_addr_);

    return true;
}

uint32_t BytecodeRegistry::getVirtualBaseAddress() const {
    return virtual_base_addr_;
}

void BytecodeRegistry::setVirtualBaseAddress(uint32_t addr) {
    virtual_base_addr_ = addr;
    ESP_LOGI(TAG, "Virtual base address set to 0x%lx", addr);
}

// C interface for mquickjs binding
extern "C" {

static BytecodeRegistry* g_registry = nullptr;

bool bytecode_registry_find(const char* name, BytecodeEntry* entry) {
    if (!g_registry) {
        g_registry = new BytecodeRegistry();
        g_registry->load();
    }

    const BytecodeEntry* found = g_registry->find(name);
    if (found && entry) {
        *entry = *found;
        return true;
    }

    return false;
}

} // extern "C"
