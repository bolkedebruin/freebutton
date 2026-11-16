// Emulator shim: Arduino's String, as far as src/json/JsonWriter.h names it.
#pragma once
#include <string>
class String {
public:
    String() = default;
    String(const char* text) : text_(text ? text : "") {}
    const char* c_str() const { return text_.c_str(); }
    size_t length() const { return text_.size(); }

private:
    std::string text_;
};
