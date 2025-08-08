//
// Created by Bolke de Bruin on 18/07/2024.
//

#ifndef JSONWRITER_H
#define JSONWRITER_H

#include <rapidjson/writer.h>
#include <string>
#include <stack>
#include <functional>
#include <type_traits>

#ifndef BUTTON_TEST
#include <WString.h>
#else
#include "mock_WString.h"
#endif

namespace json {

template<typename OutputStream>
class JsonWriter {
private:
    rapidjson::Writer<OutputStream>& writer;
    std::stack<bool> isInArray;
    int scopeDepth = 0;

public:
    class ScopedObject;
    class ScopedArray;

    explicit JsonWriter(rapidjson::Writer<OutputStream>& w) : writer(w) {}

    bool isInScope() const { return scopeDepth > 0; }

    template<typename T>
    void Add(const char* key, const T& value) {
        if (!isInArray.empty() && isInArray.top()) {
            addValue(value);
        } else {
            writer.Key(key);
            addValue(value);
        }
    }

    void StartObject(const char* key = nullptr) {
        if (key && (!isInArray.empty() && !isInArray.top())) writer.Key(key);
        writer.StartObject();
        isInArray.push(false);
        scopeDepth++;
    }

    void EndObject() {
        writer.EndObject();
        if (!isInArray.empty()) isInArray.pop();
        scopeDepth--;
    }

    void StartArray(const char* key = nullptr) {
        if (key && (!isInArray.empty() && !isInArray.top())) writer.Key(key);
        writer.StartArray();
        isInArray.push(true);
        scopeDepth++;
    }

    void EndArray() {
        writer.EndArray();
        if (!isInArray.empty()) isInArray.pop();
        scopeDepth--;
    }

    ScopedObject Object(const char* key = nullptr);
    ScopedArray Array(const char* key = nullptr);

private:
    void addValue(int value) { writer.Int(value); }
    void addValue(unsigned int value) { writer.Uint(value); }
    void addValue(int64_t value) { writer.Int64(value); }
    void addValue(uint64_t value) { writer.Uint64(value); }
    void addValue(float value) { writer.Double(value); }
    void addValue(long value) { writer.Int64(value); }
    void addValue(unsigned long value) { writer.Uint64(static_cast<uint64_t>(value)); }
    void addValue(double value) { writer.Double(value); }
    void addValue(bool value) { writer.Bool(value); }
    void addValue(const char* value) { writer.String(value); }
    void addValue(const String &value) { writer.String(value.c_str()); }
    void addValue(const std::string& value) { writer.String(value.c_str()); }
    void addValue(std::string_view value) { writer.String(value.data(), value.size()); }

public:
    class ScopedObject {
    private:
        JsonWriter& writer;
    public:
        ScopedObject(JsonWriter& w) : writer(w) {}
        ~ScopedObject() { writer.EndObject(); }

        template<typename T>
        void Add(const char* key, const T& value) { writer.Add(key, value); }

        ScopedObject Object(const char* key = nullptr) { return writer.Object(key); }
        ScopedArray Array(const char* key = nullptr);
    };

    class ScopedArray {
    private:
        JsonWriter& writer;
    public:
        ScopedArray(JsonWriter& w) : writer(w) {}
        ~ScopedArray() { writer.EndArray(); }

        template<typename T>
        void Add(const T& value) { writer.Add(nullptr, value); }

        ScopedObject Object();
        ScopedArray Array();
    };
};

// Implementations of Object and Array methods...
template<typename OutputStream>
typename JsonWriter<OutputStream>::ScopedObject JsonWriter<OutputStream>::Object(const char* key) {
    StartObject(key);
    return ScopedObject(*this);
}

template<typename OutputStream>
typename JsonWriter<OutputStream>::ScopedArray JsonWriter<OutputStream>::Array(const char* key) {
    StartArray(key);
    return ScopedArray(*this);
}

template<typename OutputStream>
typename JsonWriter<OutputStream>::ScopedArray JsonWriter<OutputStream>::ScopedObject::Array(const char* key) {
    return writer.Array(key);
}

template<typename OutputStream>
typename JsonWriter<OutputStream>::ScopedObject JsonWriter<OutputStream>::ScopedArray::Object() {
    return writer.Object();
}

template<typename OutputStream>
typename JsonWriter<OutputStream>::ScopedArray JsonWriter<OutputStream>::ScopedArray::Array() {
    return writer.Array();
}

};

#endif //JSONWRITER_H
