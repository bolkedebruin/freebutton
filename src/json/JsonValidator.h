//
// JsonValidator - streaming JSON syntax check with the RapidJSON SAX Reader
// and a no-op handler: no DOM, and bounded memory whatever the input.
//
//  * kParseInsituFlag over a stream whose Put() discards: RapidJSON decodes
//    strings "in place" instead of onto its heap stack, so a long string
//    costs nothing (a non-insitu parse grows the stack to the longest string
//    and RapidJSON does not check the realloc result).
//  * kParseIterativeFlag: no recursion per nesting level (the async_tcp task
//    has an 8 KB stack); nesting is capped at kMaxDepth by the handler, which
//    also bounds the reader's own stack (a few bytes per level).
//  * kParseNumbersAsStringsFlag: numbers are only syntax-checked, never
//    converted (keeps RapidJSON's strtod tables and code out of this path).
//
// Pure header (no Arduino / ESP), unit-tested natively in test/test_s3_logic.
//

#ifndef FREEBUTTON_JSONVALIDATOR_H
#define FREEBUTTON_JSONVALIDATOR_H

#include <cstddef>

#include "rapidjson/reader.h"

namespace json {

// Adds the in-situ "write" side to a read-only stream (Peek/Take/Tell) and
// throws the written bytes away.
template <class Base>
class DiscardingInsituStream {
public:
    typedef typename Base::Ch Ch;

    explicit DiscardingInsituStream(Base& base) : base_(base) {}

    Ch Peek() const { return base_.Peek(); }
    Ch Take() { return base_.Take(); }
    size_t Tell() const { return base_.Tell(); }

    Ch* PutBegin() {
        written_ = 0;
        return &dummy_;
    }
    void Put(Ch) { ++written_; }
    void Flush() {}
    size_t PutEnd(Ch*) { return written_; }

private:
    Base& base_;
    size_t written_ = 0;
    Ch dummy_ = 0;
};

class DepthLimitedHandler : public rapidjson::BaseReaderHandler<rapidjson::UTF8<>, DepthLimitedHandler> {
public:
    static constexpr unsigned kMaxDepth = 64;

    bool StartObject() { return ++depth_ <= kMaxDepth; }
    bool EndObject(rapidjson::SizeType) { --depth_; return true; }
    bool StartArray() { return ++depth_ <= kMaxDepth; }
    bool EndArray(rapidjson::SizeType) { --depth_; return true; }

private:
    unsigned depth_ = 0;
};

// Returns true when the whole stream is exactly one valid JSON value
// (surrounding whitespace allowed). On failure *errorOffset is the byte
// offset RapidJSON stopped at.
template <class InputStream>
bool validateJson(InputStream& input, size_t* errorOffset) {
    DiscardingInsituStream<InputStream> is(input);
    rapidjson::Reader reader(nullptr, 64);
    DepthLimitedHandler handler;
    const rapidjson::ParseResult result =
        reader.Parse<rapidjson::kParseInsituFlag | rapidjson::kParseIterativeFlag |
                     rapidjson::kParseNumbersAsStringsFlag>(is, handler);
    if (result.IsError()) {
        if (errorOffset) *errorOffset = result.Offset();
        return false;
    }
    return true;
}

}  // namespace json

#endif  // FREEBUTTON_JSONVALIDATOR_H
