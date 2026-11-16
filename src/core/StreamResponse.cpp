#include "StreamResponse.h"

#include <Arduino.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <esp_heap_caps.h>
#include <cstdlib>
#include <cstring>
#include <new>

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

class StreamResponse final : public AsyncWebServerResponse {
public:
    // name: the file's path, or the media key. Copied.
    StreamResponse(const char* name, size_t size, const char* contentType) {
        _code = 200;
        _contentLength = size;
        _sendContentLength = true;
        _chunked = false;
        _contentType = contentType;
        _name = strdup(name);
    }

    void setMedia(media_bucket_t bucket, const uint8_t md5[16]) {
        _media = true;
        _bucket = bucket;
        memcpy(_md5, md5, sizeof _md5);
    }

    bool ok() const { return _name != nullptr; }

    ~StreamResponse() override {
        leaveQueue();
        closeSource();
        releaseSlot();
        free(_name);
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
        if (AsyncClient* c = request->client()) _fullRoom = c->space();
        if (_fullRoom == 0) _fullRoom = CONFIG_LWIP_TCP_SND_BUF_DEFAULT;
        if (s_waitHead || !stream::mayStart(s_active, freeHeap())) {
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

    bool takeSlot() {
        if (_slot) return true;
        if (_queued && s_waitHead != this) return false;  // first in, first out
        if (s_active >= stream::kMaxStreams) dropStalled();
        if (!stream::mayStart(s_active, freeHeap())) return false;
        leaveQueue();
        for (StreamResponse*& h : s_holders) {
            if (!h) {
                h = this;
                break;
            }
        }
        _slot = true;
        _progressAt = millis();
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

    // Slot holders that went silent (StreamLogic.h, kStallMs) lose their
    // slot. Their connection is closed when it next shows life.
    static void dropStalled() {
        const uint32_t now = millis();
        for (StreamResponse* h : s_holders) {
            if (h && stream::stalled(now, h->_progressAt)) {
                ++s_stalled;
                h->_state = RESPONSE_FAILED;
                h->closeSource();
                h->releaseSlot(false);
            }
        }
    }

    // --- source -----------------------------------------------------------
    bool openSource() {
        if (_media) {
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

    // --- sending ----------------------------------------------------------
    size_t pump() {
        if (_state == RESPONSE_FAILED || _state == RESPONSE_END || !_request) return 0;
        AsyncClient* c = _request->client();
        if (!c) return 0;
        if (!_slot) {
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
            if (_media) {
                wrote += sendMapped(c);
            } else if (!sendFile(c, wrote)) {
                _writtenLength += wrote;
                fail();
                return wrote;
            }
            _sentLength += wrote - before;
            if (_sentLength >= _contentLength) {
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
            ++s_served;
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
        const char* at = reinterpret_cast<const char*>(_file.data) + _sentLength;
        return c->add(at, n);  // copied by the TCP stack, straight from the mapping
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
    int _fd = -1;
    media_file_t _file{};
    uint8_t _md5[16] = {};
    media_bucket_t _bucket{};
    bool _media = false;
    bool _slot = false;
    bool _queued = false;
    bool _closed = false;
};

StreamResponse* make(const char* name, size_t size, const char* contentType) {
    auto* r = new (std::nothrow) StreamResponse(name, size, contentType);
    if (r && !r->ok()) {
        delete r;
        r = nullptr;
    }
    return r;
}

}  // namespace

namespace stream {

AsyncWebServerResponse* beginFile(const char* path, size_t size, const char* contentType) {
    return make(path, size, contentType);
}

AsyncWebServerResponse* beginMedia(media_bucket_t bucket, const char* key, const media_stat_t& st,
                                   const char* contentType) {
    StreamResponse* r = make(key, st.size, contentType);
    if (r) r->setMedia(bucket, st.md5);
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
