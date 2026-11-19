// Emulator shim: a file of the device's filesystem, read from the host
// (config.json). Read only.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <cstring>
#include <memory>
#include <string>
#include "Arduino.h"

class File {
public:
    File() = default;
    explicit File(std::shared_ptr<const std::string> data) : data_(std::move(data)) {}
    explicit operator bool() const { return data_ != nullptr; }
    int available() const { return data_ ? static_cast<int>(data_->size() - pos_) : 0; }
    int peek() const { return available() > 0 ? static_cast<unsigned char>((*data_)[pos_]) : -1; }
    int read() { return available() > 0 ? static_cast<unsigned char>((*data_)[pos_++]) : -1; }
    size_t read(uint8_t* buf, const size_t size) {
        const size_t n = static_cast<size_t>(available()) < size ? static_cast<size_t>(available()) : size;
        if (n) std::memcpy(buf, data_->data() + pos_, n);
        pos_ += n;
        return n;
    }
    size_t position() const { return pos_; }
    size_t size() const { return data_ ? data_->size() : 0; }
    void close() { data_.reset(); }

private:
    std::shared_ptr<const std::string> data_;
    size_t pos_ = 0;
};
