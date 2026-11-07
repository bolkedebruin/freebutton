//
// WindowOutputStream - a RapidJSON-style output stream (Put/Flush) that keeps
// only the bytes in [skip, skip + capacity) of everything written to it, and
// counts the total.
//
// Lets a push-style writer (ConfigManager::write) feed a pull-style chunked
// HTTP response without buffering the whole document: run the writer once
// with capacity 0 to get the length, then once per chunk with the chunk's
// offset and buffer. Peak RAM is the chunk buffer the web server already owns.
//
// Pure (no Arduino / ESP headers), unit-tested in test/test_window_stream.
//

#ifndef FREEBUTTON_WINDOWOUTPUTSTREAM_H
#define FREEBUTTON_WINDOWOUTPUTSTREAM_H

#include <cstddef>
#include <cstdint>

class WindowOutputStream {
public:
    typedef char Ch;

    WindowOutputStream(uint8_t* buffer, const size_t skip, const size_t capacity)
        : buffer_(buffer), skip_(skip), capacity_(buffer ? capacity : 0) {}

    // Counting only.
    WindowOutputStream() : WindowOutputStream(nullptr, 0, 0) {}

    void Put(const Ch c) {
        if (position_ >= skip_ && position_ - skip_ < capacity_) {
            buffer_[position_ - skip_] = static_cast<uint8_t>(c);
        }
        ++position_;
    }

    void Flush() {}

    // Total bytes written to the stream (inside and outside the window).
    size_t total() const { return position_; }

    // Bytes that landed in the buffer.
    size_t copied() const {
        if (position_ <= skip_) return 0;
        const size_t after = position_ - skip_;
        return after < capacity_ ? after : capacity_;
    }

private:
    uint8_t* buffer_;
    size_t skip_;
    size_t capacity_;
    size_t position_ = 0;
};

#endif // FREEBUTTON_WINDOWOUTPUTSTREAM_H
