// Emulator shim: src/mqtt/Broker.h has a toJson template that is never
// instantiated here.
#pragma once
namespace json {
template <typename OutputStream>
class JsonWriter {
public:
    class ScopedObject {
    public:
        template <typename... A> void Add(A&&...) {}
    };
};
} // namespace json
