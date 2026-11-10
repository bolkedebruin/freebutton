// Media store: files in the raw `media` partition that the UI reads in place
// (memory-mapped). Design: docs/MEDIA-STORE.md.
//
// Thread safety: every function may be called from any task (S3 uploads run
// on async_tcp, fonts on the LVGL task); the store serialises internally.
// Pointers returned by media_open() stay valid until media_close().
#ifndef MEDIA_STORE_H
#define MEDIA_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_KEY_MAX 96 /* bytes, without the NUL */

/* Buckets. FONTS and IMAGES belong to the user; RENDER holds entries the
 * device derives (rendered SVG, decoded images) and may evict. */
typedef enum {
    MEDIA_BUCKET_FONTS = 0,
    MEDIA_BUCKET_IMAGES = 1,
    MEDIA_BUCKET_RENDER = 2,
    MEDIA_BUCKET_COUNT
} media_bucket_t;

typedef enum {
    MEDIA_OK = 0,
    MEDIA_ERR_UNAVAILABLE, /* no media partition, or not initialised */
    MEDIA_ERR_NOT_FOUND,
    MEDIA_ERR_NO_SPACE,    /* even after counting freeable space and evicting RENDER */
    MEDIA_ERR_INVALID,     /* bad key, size, bucket or state */
    MEDIA_ERR_BUSY,        /* too many open files or uploads in progress */
    MEDIA_ERR_SIZE,        /* more or fewer bytes written than reserved */
    MEDIA_ERR_IO,          /* flash read/write/erase/map failed */
} media_err_t;

/* A file mapped for reading. */
typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t handle; /* opaque; 0 = not open */
} media_file_t;

typedef struct {
    char key[MEDIA_KEY_MAX + 1];
    uint32_t size;
    uint32_t mtime;     /* seconds since the epoch, as given to media_put_begin */
    uint8_t md5[16];    /* as given to media_put_commit (S3 ETag) */
} media_stat_t;

typedef struct {
    uint32_t total;       /* data bytes (pages * 64 KB, directory excluded) */
    uint32_t used;        /* allocated to live entries (incl. page tails) */
    uint32_t reclaimable; /* dead but still open (freed on close) */
    uint32_t derived;     /* used by MEDIA_BUCKET_RENDER (evictable) */
    uint32_t free;        /* total - used - reclaimable */
} media_usage_t;

typedef struct media_writer media_writer_t;

/* Mount the partition, rebuild the free map from the directory. Formats the
 * store when it is blank or its directory is unreadable. Idempotent. */
media_err_t media_init(void);

/* Map a file and take a reference. */
media_err_t media_open(media_bucket_t bucket, const char *key, media_file_t *out);
/* Drop the reference; unmaps, and frees the pages of a deleted/replaced file
 * once nothing uses it. Safe on a zeroed media_file_t. */
void media_close(media_file_t *file);

/* Upload: reserve `size` bytes (fails before anything is written), stream
 * exactly `size` bytes, then commit (atomic: until then any previous version
 * stays the live one). Abort releases the reservation. */
media_err_t media_put_begin(media_bucket_t bucket, const char *key, uint32_t size, uint32_t mtime,
                            media_writer_t **out);
media_err_t media_put_write(media_writer_t *w, const void *data, uint32_t len);
media_err_t media_put_commit(media_writer_t *w, const uint8_t md5[16]);
void media_put_abort(media_writer_t *w);

media_err_t media_delete(media_bucket_t bucket, const char *key);
media_err_t media_stat(media_bucket_t bucket, const char *key, media_stat_t *out);

/* Iterate a bucket in byte-wise key order: the first key greater than
 * `after` (NULL or "" = from the start). MEDIA_ERR_NOT_FOUND at the end. */
media_err_t media_next(media_bucket_t bucket, const char *after, media_stat_t *out);

void media_usage(media_usage_t *out);

/* Background work (erase freed pages ahead of uploads, empty sparse shared
 * pages). Call when idle; does at most one erase per call. Returns true when
 * more work is pending. */
bool media_maintain(void);

#ifdef __cplusplus
}
#endif

#endif
