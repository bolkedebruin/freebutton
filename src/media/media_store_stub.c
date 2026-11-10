// Placeholder so the firmware links while the store is built: every call
// reports MEDIA_ERR_UNAVAILABLE (fonts then fall back to the built-in font).
// Replaced by the real implementation; delete this file then.
#include "media_store.h"

#include <string.h>

media_err_t media_init(void) { return MEDIA_ERR_UNAVAILABLE; }

media_err_t media_open(media_bucket_t bucket, const char *key, media_file_t *out) {
    (void)bucket;
    (void)key;
    if (out) memset(out, 0, sizeof *out);
    return MEDIA_ERR_UNAVAILABLE;
}

void media_close(media_file_t *file) {
    if (file) memset(file, 0, sizeof *file);
}

media_err_t media_put_begin(media_bucket_t bucket, const char *key, uint32_t size, uint32_t mtime,
                            media_writer_t **out) {
    (void)bucket;
    (void)key;
    (void)size;
    (void)mtime;
    if (out) *out = NULL;
    return MEDIA_ERR_UNAVAILABLE;
}

media_err_t media_put_write(media_writer_t *w, const void *data, uint32_t len) {
    (void)w;
    (void)data;
    (void)len;
    return MEDIA_ERR_UNAVAILABLE;
}

media_err_t media_put_commit(media_writer_t *w, const uint8_t md5[16]) {
    (void)w;
    (void)md5;
    return MEDIA_ERR_UNAVAILABLE;
}

void media_put_abort(media_writer_t *w) { (void)w; }

media_err_t media_delete(media_bucket_t bucket, const char *key) {
    (void)bucket;
    (void)key;
    return MEDIA_ERR_UNAVAILABLE;
}

media_err_t media_stat(media_bucket_t bucket, const char *key, media_stat_t *out) {
    (void)bucket;
    (void)key;
    (void)out;
    return MEDIA_ERR_UNAVAILABLE;
}

media_err_t media_next(media_bucket_t bucket, const char *after, media_stat_t *out) {
    (void)bucket;
    (void)after;
    (void)out;
    return MEDIA_ERR_UNAVAILABLE;
}

void media_usage(media_usage_t *out) {
    if (out) memset(out, 0, sizeof *out);
}

bool media_maintain(void) { return false; }
