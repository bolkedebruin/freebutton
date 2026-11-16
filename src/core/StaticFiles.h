//
// The web IDE's files: LittleFS /www, served at "/". Replaces the server
// library's serveStatic(), which reads through a stdio file (a 4 KB buffer
// per open file, opened while the request is still being matched) and its
// own 2.9 KB send buffer; this handler sends with stream::beginFile().
//
// As before: "/" is index.html; "<file>.gz" is sent for "<file>" with
// Content-Encoding: gzip; an ETag answers If-None-Match with 304; files with
// a content hash in their name and the IDE's web fonts may be kept by the
// browser (StaticCache.h), everything else is checked on every load.
//
#ifndef FREEBUTTON_STATICFILES_H
#define FREEBUTTON_STATICFILES_H

class AsyncWebServer;

namespace freebutton {

// Register last: it answers every GET whose file exists below /www.
void registerStaticFiles(AsyncWebServer& server);

}  // namespace freebutton

#endif  // FREEBUTTON_STATICFILES_H
