#include "StreamResponse.h"

#include <Arduino.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_heap_caps.h>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <utility>

#include "StreamLogic.h"
#include "StreamStats.h"

namespace {

class StreamResponse;

// Downloads that hold a slot, and those that wait for one (first in, first
// out). async_tcp task only.
uint8_t s_active = 0;
uint8_t s_waiting = 0;
StreamResponse* s_waitHead = nullptr;
StreamResponse* s_holders[stream::kMaxStreams] = {};
uint8_t s_waitingMax = 0;
uint32_t s_served = 0;
uint32_t s_stalled = 0;

uint32_t freeHeap() {
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

enum class Source : uint8_t {
    File,       // LittleFS, read through a file descriptor
    Media,      // media store, mapped flash
    Memory,     // a block that outlives the response, or the text it owns
    Generated,  // written piece by piece by a function
};

class StreamResponse final : public AsyncWebServerResponse {
public:
    // A full heap must give nullptr, not abort: the default nothrow new
    // calls the throwing one, which aborts without exceptions.
    static void* operator new(size_t n, const std::nothrow_t&) noexcept { return malloc(n); }
    static void operator delete(void* p, const std::nothrow_t&) noexcept { free(p); }
    static void operator delete(void* p) noexcept { free(p); }

    StreamResponse(Source source, size_t size, const char* contentType) : _source(source) {
        _code = 200;
        _contentLength = size;
        _sendContentLength = true;
        _chunked = false;
        _contentType = contentType;
    }

    // name: the file's path, or the media key. Copied.
    bool setName(const char* name) {
        _name = strdup(name);
        return _name != nullptr;
    }

    void setMedia(media_bucket_t bucket, const uint8_t md5[16]) {
        _bucket = bucket;
        memcpy(_md5, md5, sizeof _md5);
    }

    void setMemory(const char* data) { _mem = data; }

    void setText(std::string&& text) {
        _text = std::move(text);
        _mem = _text.data();
        _contentLength = _text.size();
    }

    // chunked: the length is not known; the writer ends the body with 0.
    void setWriter(stream::Fill fill, void* context, bool chunked) {
        _fill = fill;
        _context = context;
        if (chunked) {
            _chunked = true;
            _sendContentLength = false;
        }
    }

    ~StreamResponse() override {
        leaveQueue();
        closeSource();
        releaseSlot();
        free(_name);
        free(_pend);
    }

    bool _sourceValid() const override { return true; }

    void _respond(AsyncWebServerRequest* request) override {
        _request = request;
        addHeader("Connection", "close", false);
        _assembleHead(_head, request->version());
        _state = RESPONSE_HEADERS;
        // Neither header list is read again, and a download may wait a
        // while: a browser's request headers alone are about 1 KB.
        _headers.clear();
        _contentType = String();
        request->removeHeaders();
        _frames = _chunked && request->version() != 0;  // HTTP/1.0: the close ends the body
        if (AsyncClient* c = request->client()) _fullRoom = c->space();
        if (_fullRoom == 0) _fullRoom = CONFIG_LWIP_TCP_SND_BUF_DEFAULT;
        if (needsSlot() && (s_waitHead || !stream::mayStart(s_active, freeHeap()))) {
            joinQueue();
            return;
        }
        pump();
        closeIfFailed();
    }

    // len: bytes the client acknowledged; 0 when the TCP stack polls us.
    size_t _ack(AsyncWebServerRequest* request, size_t len, uint32_t) override {
        _request = request;
        _acked += len;
        if (len) _progressAt = millis();
        const size_t wrote = pump();
        closeIfFailed();
        // After an acknowledgement the server closes a finished response
        // itself; after a poll it does not.
        if (len == 0 && _state == RESPONSE_END) {
            if (AsyncClient* c = request->client()) c->close();
        }
        return wrote;
    }

    // The head of the queue gets its turn when a slot is released. Must not
    // close the connection: nothing keeps the request alive here.
    void turn() { pump(); }

private:
    // --- wait queue -------------------------------------------------------
    void joinQueue() {
        _queued = true;
        _next = nullptr;
        StreamResponse** p = &s_waitHead;
        while (*p) p = &(*p)->_next;
        *p = this;
        ++s_waiting;
        if (s_waiting > s_waitingMax) s_waitingMax = s_waiting;
    }

    void leaveQueue() {
        if (!_queued) return;
        for (StreamResponse** p = &s_waitHead; *p; p = &(*p)->_next) {
            if (*p == this) {
                *p = _next;
                break;
            }
        }
        _queued = false;
        _next = nullptr;
        --s_waiting;
    }

    // Memory needs neither a scratch block nor a file: it never waits.
    bool needsSlot() const { return _source != Source::Memory; }

    bool takeSlot() {
        if (_slot) return true;
        if (_queued && s_waitHead != this) return false;  // first in, first out
        if (!stream::mayStart(s_active, freeHeap())) {
            dropStalled();
            if (!stream::mayStart(s_active, freeHeap())) return false;
        }
        leaveQueue();
        for (StreamResponse*& h : s_holders) {
            if (!h) {
                h = this;
                break;
            }
        }
        _slot = true;
        _progressAt = millis();
        _paceAt = _progressAt;
        _paceAcked = _acked;
        ++s_active;
        return true;
    }

    // next: give the head of the queue its turn. Not while that head is the
    // caller (dropStalled).
    void releaseSlot(bool next = true) {
        if (!_slot) return;
        _slot = false;
        for (StreamResponse*& h : s_holders) {
            if (h == this) h = nullptr;
        }
        --s_active;
        if (next && s_waitHead) s_waitHead->turn();
    }

    // Slot holders that went silent or are too slow (StreamLogic.h) lose
    // their slot and their connection. Called by a download that cannot
    // start, so never by a holder itself.
    static void dropStalled() {
        const uint32_t now = millis();
        for (StreamResponse* h : s_holders) {
            if (!h) continue;
            const size_t paced = h->_acked - h->_paceAcked;
            if (stream::stalled(now, h->_progressAt) || stream::tooSlow(now, h->_paceAt, paced)) {
                ++s_stalled;
                h->_state = RESPONSE_FAILED;
                h->_closed = true;
                h->closeSource();
                h->releaseSlot(false);
                // Reset, not close: a close would keep the unsent bytes (and
                // their memory) for a client that does not read. This frees
                // the request and, with it, h.
                if (AsyncClient* c = h->_request ? h->_request->client() : nullptr) c->abort();
            } else if (stream::paceWindowOver(now, h->_paceAt)) {
                h->_paceAt = now;
                h->_paceAcked = h->_acked;
            }
        }
    }

    // --- source -----------------------------------------------------------
    bool openSource() {
        if (_source == Source::Generated) return true;
        if (_source == Source::Media) {
            media_stat_t st;
            if (media_stat(_bucket, _name, &st) != MEDIA_OK || st.size != _contentLength ||
                memcmp(st.md5, _md5, sizeof _md5) != 0) {
                return false;  // replaced or deleted while it waited
            }
            if (media_open(_bucket, _name, &_file) != MEDIA_OK) {
                memset(&_file, 0, sizeof _file);
                return false;
            }
            return _file.size == _contentLength;
        }
        _fd = open(_name, O_RDONLY);
        if (_fd < 0) return false;
        struct stat st {};
        return fstat(_fd, &st) == 0 && static_cast<size_t>(st.st_size) == _contentLength;
    }

    void closeSource() {
        if (_fd >= 0) {
            close(_fd);
            _fd = -1;
        }
        if (_file.handle) {
            media_close(&_file);
            memset(&_file, 0, sizeof _file);
        }
    }

    void fail() {
        _state = RESPONSE_FAILED;
        closeSource();
        releaseSlot();
    }

    void closeIfFailed() {
        if (_state != RESPONSE_FAILED || _closed) return;
        _closed = true;
        if (AsyncClient* c = _request ? _request->client() : nullptr) c->close();
    }

    bool allAcknowledged(AsyncClient* c) const {
        return _acked >= _writtenLength || c->space() >= _fullRoom;
    }

    bool contentDone() const {
        if (_source == Source::Generated) return _writerDone && !_pend;
        return _sentLength >= _contentLength;
    }

    // --- sending ----------------------------------------------------------
    size_t pump() {
        if (_state == RESPONSE_FAILED || _state == RESPONSE_END || !_request) return 0;
        AsyncClient* c = _request->client();
        if (!c) return 0;
        if (needsSlot() && !_slot) {
            if (!takeSlot()) return 0;
            if (!openSource()) {
                fail();
                return 0;
            }
        }

        size_t wrote = 0;
        if (_state == RESPONSE_HEADERS) {
            const size_t n = c->add(_head.c_str() + _headSent, _head.length() - _headSent);
            _headSent += n;
            wrote += n;
            if (_headSent < _head.length()) {
                _writtenLength += wrote;
                if (wrote) c->send();
                return wrote;
            }
            _head = String();
            _state = RESPONSE_CONTENT;
        }

        if (_state == RESPONSE_CONTENT) {
            const size_t before = wrote;
            bool good = true;
            if (_source == Source::Media || _source == Source::Memory) {
                wrote += sendMapped(c);
            } else if (_source == Source::Generated) {
                good = sendGenerated(c, wrote);
            } else {
                good = sendFile(c, wrote);
            }
            if (!good) {
                _writtenLength += wrote;
                fail();
                return wrote;
            }
            _sentLength += wrote - before;
            if (contentDone()) {
                _state = RESPONSE_WAIT_ACK;
                closeSource();  // nothing more to read: the TCP stack has its copy
            }
        }

        _writtenLength += wrote;
        if (wrote) {
            _progressAt = millis();
            c->send();
        }

        if (_state == RESPONSE_WAIT_ACK && allAcknowledged(c)) {
            _state = RESPONSE_END;
            if (needsSlot()) ++s_served;
            closeSource();
            releaseSlot();
        }
        return wrote;
    }

    size_t sendMapped(AsyncClient* c) {
        const size_t room = c->space();
        size_t n = _contentLength - _sentLength;
        if (n > room) n = room;
        if (n == 0) return 0;
        const char* base = _source == Source::Media ? reinterpret_cast<const char*>(_file.data) : _mem;
        return c->add(base + _sentLength, n);  // copied by the TCP stack, straight from the source
    }

    // False when the writer gave less than the announced length. wrote grows
    // by what the TCP stack took. The scratch block outlives the call only
    // when the stack took less than it said it would (_pend): the writer
    // cannot be asked for the same bytes again.
    bool sendGenerated(AsyncClient* c, size_t& wrote) {
        if (_pend) {
            const size_t added = c->add(_pend + _pendSent, _pendLength - _pendSent);
            _pendSent += added;
            wrote += added;
            if (_pendSent < _pendLength) return true;
            free(_pend);
            _pend = nullptr;
        }
        const size_t head = _frames ? stream::kChunkHead : 0;
        while (!_writerDone) {
            const size_t remaining = _chunked ? SIZE_MAX : _contentLength - _written;
            if (remaining == 0) {
                _writerDone = true;
                break;
            }
            const size_t n = stream::generatedPiece(c->space(), remaining, _frames);
            if (n == 0) break;
            char* block = static_cast<char*>(malloc(n + (_frames ? stream::kChunkFrame : 0)));
            if (!block) break;  // try again on the next poll
            const size_t got = _fill(_context, reinterpret_cast<uint8_t*>(block) + head, n, _written);
            _written += got;
            size_t length = got;
            if (got == 0) {
                _writerDone = true;
                if (!_chunked) {
                    free(block);
                    return false;
                }
                if (_frames) {
                    memcpy(block, "000\r\n\r\n", stream::kChunkFrame);
                    length = stream::kChunkFrame;
                }
            } else if (_frames) {
                stream::chunkHead(got, block);
                memcpy(block + head + got, "\r\n", stream::kChunkTail);
                length = got + stream::kChunkFrame;
            }
            const size_t added = length ? c->add(block, length) : 0;
            wrote += added;
            if (added < length) {
                _pend = block;
                _pendLength = length;
                _pendSent = added;
                break;
            }
            free(block);
        }
        return true;
    }

    // False on a read error. wrote grows by what the TCP stack took.
    bool sendFile(AsyncClient* c, size_t& wrote) {
        char* scratch = nullptr;
        bool good = true;
        size_t sent = _sentLength;
        while (sent < _contentLength) {
            const size_t n = stream::pieceSize(c->space(), _contentLength - sent);
            if (n == 0) break;
            if (!scratch) {
                scratch = static_cast<char*>(malloc(n));  // pieces only get smaller
                if (!scratch) break;                      // try again on the next poll
            }
            const ssize_t got = read(_fd, scratch, n);
            if (got <= 0) {
                good = false;
                break;
            }
            const size_t added = c->add(scratch, static_cast<size_t>(got));
            sent += added;
            wrote += added;
            if (added < static_cast<size_t>(got)) {
                lseek(_fd, static_cast<off_t>(added) - static_cast<off_t>(got), SEEK_CUR);
                break;
            }
        }
        free(scratch);
        return good;
    }

    AsyncWebServerRequest* _request = nullptr;
    StreamResponse* _next = nullptr;
    char* _name = nullptr;
    String _head;
    size_t _headSent = 0;
    size_t _acked = 0;
    size_t _fullRoom = 0;
    uint32_t _progressAt = 0;
    uint32_t _paceAt = 0;      // start of the pace window, and what was
    size_t _paceAcked = 0;     // acknowledged by then
    const char* _mem = nullptr;
    std::string _text;
    stream::Fill _fill = nullptr;
    void* _context = nullptr;
    size_t _written = 0;       // body bytes the writer produced
    char* _pend = nullptr;     // a piece the TCP stack took only part of
    size_t _pendLength = 0;
    size_t _pendSent = 0;
    int _fd = -1;
    media_file_t _file{};
    uint8_t _md5[16] = {};
    media_bucket_t _bucket{};
    Source _source;
    bool _frames = false;      // the body goes out as HTTP chunks
    bool _writerDone = false;
    bool _slot = false;
    bool _queued = false;
    bool _closed = false;
};

StreamResponse* make(Source source, size_t size, const char* contentType, const char* name = nullptr) {
    auto* r = new (std::nothrow) StreamResponse(source, size, contentType);
    if (r && name && !r->setName(name)) {
        delete r;
        r = nullptr;
    }
    return r;
}

}  // namespace

namespace stream {

AsyncWebServerResponse* beginFile(const char* path, size_t size, const char* contentType) {
    return make(Source::File, size, contentType, path);
}

AsyncWebServerResponse* beginMedia(media_bucket_t bucket, const char* key, const media_stat_t& st,
                                   const char* contentType) {
    StreamResponse* r = make(Source::Media, st.size, contentType, key);
    if (r) r->setMedia(bucket, st.md5);
    return r;
}

AsyncWebServerResponse* beginConst(const char* data, size_t size, const char* contentType) {
    StreamResponse* r = make(Source::Memory, size, contentType);
    if (r) r->setMemory(data);
    return r;
}

AsyncWebServerResponse* beginText(std::string&& text, const char* contentType) {
    StreamResponse* r = make(Source::Memory, 0, contentType);
    if (r) r->setText(std::move(text));
    return r;
}

AsyncWebServerResponse* beginGenerated(size_t size, const char* contentType, Fill fill, void* context) {
    StreamResponse* r = make(Source::Generated, size, contentType);
    if (r) r->setWriter(fill, context, false);
    return r;
}

AsyncWebServerResponse* beginChunked(const char* contentType, Fill fill, void* context) {
    StreamResponse* r = make(Source::Generated, 0, contentType);
    if (r) r->setWriter(fill, context, true);
    return r;
}

uint8_t active() {
    return s_active + s_waiting;
}

// Read from another task: single bytes and words, a torn view is harmless.
Stats stats() {
    Stats s;
    s.sending = s_active;
    s.waiting = s_waiting;
    s.waitingMax = s_waitingMax;
    s.served = s_served;
    s.stalled = s_stalled;
    return s;
}

}  // namespace stream
