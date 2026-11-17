#include "StaticFiles.h"

#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

#include "StreamLogic.h"
#include "StreamResponse.h"

namespace {

// What canHandle() found, kept for handleRequest() in the request's
// _tempObject (freed by the request).
struct Hit {
    uint32_t size;
    uint32_t etag;
    bool gzip;
    char path[stream::kStaticPathMax + 1];  // full VFS path
};

bool regularFile(const char* path, struct stat& st) {
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// CRC-32 of the uncompressed content: bytes 8..5 from the end of a gzip file.
bool gzipCrc(const char* path, uint32_t size, uint32_t& crc) {
    if (size < 18) return false;
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    uint8_t b[4];
    const bool good = lseek(fd, static_cast<off_t>(size) - 8, SEEK_SET) >= 0 && read(fd, b, 4) == 4;
    close(fd);
    crc = static_cast<uint32_t>(b[0]) | static_cast<uint32_t>(b[1]) << 8 | static_cast<uint32_t>(b[2]) << 16 |
          static_cast<uint32_t>(b[3]) << 24;
    return good;
}

bool find(const String& url, Hit& hit) {
    const char* mount = LittleFS.mountpoint();
    if (!mount) return false;  // not mounted
    const size_t mountLen = strlen(mount);
    char rel[stream::kStaticPathMax + 1];
    if (!stream::staticPath(url.c_str(), url.length(), rel, sizeof rel)) return false;
    const size_t relLen = strlen(rel);
    if (mountLen + relLen + 3 > sizeof(hit.path) - 1) return false;
    memcpy(hit.path, mount, mountLen);
    memcpy(hit.path + mountLen, rel, relLen + 1);

    struct stat st {};
    if (regularFile(hit.path, st)) {
        hit.gzip = false;
        hit.size = static_cast<uint32_t>(st.st_size);
        hit.etag = st.st_mtime > 0 ? static_cast<uint32_t>(st.st_mtime) : hit.size;
        return true;
    }
    memcpy(hit.path + mountLen + relLen, ".gz", 4);
    if (regularFile(hit.path, st)) {
        hit.gzip = true;
        hit.size = static_cast<uint32_t>(st.st_size);
        return gzipCrc(hit.path, hit.size, hit.etag);
    }
    return false;
}

class StaticFiles final : public AsyncWebHandler {
public:
    bool canHandle(AsyncWebServerRequest* request) const override {
        if (!request->isHTTP() || request->method() != HTTP_GET) return false;
        auto* hit = static_cast<Hit*>(malloc(sizeof(Hit)));
        if (!hit) return false;
        if (!find(request->url(), *hit)) {
            free(hit);
            return false;
        }
        request->_tempObject = hit;
        return true;
    }

    void handleRequest(AsyncWebServerRequest* request) override {
        auto* hit = static_cast<Hit*>(request->_tempObject);
        if (!hit) {
            request->send(404);
            return;
        }
        char etag[11];
        stream::formatStaticETag(hit->etag, etag);
        const String& url = request->url();
        const char* cache = stream::cacheControlFor(url.c_str(), url.length());

        AsyncWebServerResponse* response;
        const AsyncWebHeader* inm = request->getHeader("If-None-Match");
        if (inm && stream::etagMatches(inm->value().c_str(), etag)) {
            response = request->beginResponse(304);
        } else if (hit->size == 0) {
            response = request->beginResponse(200, stream::staticContentType(hit->path, strlen(hit->path)), "");
        } else {
            response = stream::beginFile(hit->path, hit->size,
                                         stream::staticContentType(hit->path, strlen(hit->path)));
            if (response && hit->gzip) response->addHeader("Content-Encoding", "gzip");
        }
        // The path is copied into the response: the request need not keep it.
        free(request->_tempObject);
        request->_tempObject = nullptr;
        if (!response) {
            request->send(503);
            return;
        }
        response->addHeader("ETag", etag);
        response->addHeader("Cache-Control", cache);
        request->send(response);
    }
};

}  // namespace

namespace freebutton {

void registerStaticFiles(AsyncWebServer& server) {
    server.addHandler(new StaticFiles());
}

}  // namespace freebutton
