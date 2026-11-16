//
// Downloads: the body of a GET goes from its source to the TCP stack without
// a buffer of ours in between, and only a few downloads send at a time.
//
//   - A media file (fonts, images) is mapped flash: the TCP stack copies it
//     from the mapping into the segment it sends. (By reference, without
//     that copy, gains nothing on this platform: StreamLogic.h.)
//   - A LittleFS file is read through its file descriptor (no stdio buffer)
//     in pieces of one segment into a scratch block that lives for the call
//     only; the stack keeps its own copy of unacknowledged bytes, as it must
//     for data that is not mapped.
//   - Downloads beyond the allowed number wait with their file not yet open.
//
// The rules are in StreamLogic.h; docs/DOWNLOADS.md has the measurements.
// Everything here runs on the async_tcp task.
//
#ifndef FREEBUTTON_STREAMRESPONSE_H
#define FREEBUTTON_STREAMRESPONSE_H

#include <ESPAsyncWebServer.h>

#include <string>

#include "media/media_store.h"

namespace stream {

// Response (200, Content-Length, Content-Type; add further headers to it)
// for a LittleFS file. path is a full VFS path; size is the file's size now.
// The file is opened when the download gets its turn; if it has another size
// by then the connection is closed. nullptr when out of memory.
AsyncWebServerResponse* beginFile(const char* path, size_t size, const char* contentType);

// The same for a file of the media store, as media_stat() described it. If
// the file was replaced before the download's turn the connection is closed.
AsyncWebServerResponse* beginMedia(media_bucket_t bucket, const char* key, const media_stat_t& st,
                                   const char* contentType);

// A body that is already in memory and outlives the response (flash). It
// needs no buffer and no file, so it does not wait for a turn.
AsyncWebServerResponse* beginConst(const char* data, size_t size, const char* contentType);

// The same for a text the response takes over (moved in, not copied).
AsyncWebServerResponse* beginText(std::string&& text, const char* contentType);

// A body written piece by piece: fill() puts up to max bytes of the body
// from offset index into buffer and returns how many. It takes its turn like
// a file download; its scratch block (one segment) lives for the call only.
using Fill = size_t (*)(void* context, uint8_t* buffer, size_t max, size_t index);

// size bytes exactly (Content-Length); fill must deliver them all.
AsyncWebServerResponse* beginGenerated(size_t size, const char* contentType, Fill fill, void* context);

// Length not known (chunked): fill returns 0 when the body is complete, and
// is then not called again. Bytes it returned are never asked for twice.
AsyncWebServerResponse* beginChunked(const char* contentType, Fill fill, void* context);

// Downloads that are sending or waiting.
uint8_t active();

}  // namespace stream

#endif  // FREEBUTTON_STREAMRESPONSE_H
