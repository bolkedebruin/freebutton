//
// media_emu.c - the media store (src/media/media_store.h) over the host's
// files, in place of src/media/media_esp.c (flash).
//
// The firmware memory-maps a file of the store and reads it in place (the
// fonts: tiny_ttf rasterizes from the mapping). Here a file is copied once
// from the host into the wasm memory (fbh_media_size / fbh_media_read) and
// kept: every later media_open() of the key gives the same bytes and the same
// handle, as the store does for an unchanged file. fbe_media_changed() is an
// upload or a delete: the next open asks the host again, and a copy that is
// still open lives until its last close (the store's rule for a replaced
// file).
//
// Read-only: the firmware sources of the emulator do not write media.
//
#include <stdlib.h>
#include <string.h>

#include "emu.h"
#include "fbh.h"
#include "media/media_store.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t refs;
    uint8_t bucket;
    bool stale; /* replaced: freed at the last close */
    char key[MEDIA_KEY_MAX + 1];
} entry_t;

static entry_t *entries;
static size_t entry_count;
static size_t entry_cap;
static uint32_t bytes_held;

media_err_t media_init(void) { return MEDIA_OK; }

static entry_t *find(const media_bucket_t bucket, const char *key) {
    for (size_t i = 0; i < entry_count; i++) {
        entry_t *e = &entries[i];
        if (e->data != NULL && !e->stale && e->bucket == bucket && strcmp(e->key, key) == 0) return e;
    }
    return NULL;
}

static entry_t *free_slot(void) {
    for (size_t i = 0; i < entry_count; i++) {
        if (entries[i].data == NULL) return &entries[i];
    }
    if (entry_count == entry_cap) {
        const size_t cap = entry_cap ? entry_cap * 2 : 16;
        entry_t *grown = (entry_t *)realloc(entries, cap * sizeof(entry_t));
        if (grown == NULL) return NULL;
        entries = grown;
        entry_cap = cap;
    }
    entry_t *e = &entries[entry_count++];
    memset(e, 0, sizeof(*e));
    return e;
}

static void drop(entry_t *e) {
    bytes_held -= e->size;
    free(e->data);
    memset(e, 0, sizeof(*e));
}

media_err_t media_open(const media_bucket_t bucket, const char *key, media_file_t *out) {
    if (out == NULL) return MEDIA_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    const size_t len = key != NULL ? strlen(key) : 0;
    if (len == 0 || len > MEDIA_KEY_MAX || bucket >= MEDIA_BUCKET_COUNT) return MEDIA_ERR_INVALID;
    entry_t *e = find(bucket, key);
    if (e == NULL) {
        const int32_t size = fbh_media_size((int)bucket, key, len);
        if (size < 0) return MEDIA_ERR_NOT_FOUND;
        uint8_t *data = (uint8_t *)malloc(size > 0 ? (size_t)size : 1);
        if (data == NULL) return MEDIA_ERR_UNAVAILABLE;
        if (size > 0 && !fbh_media_read((int)bucket, key, len, data, (uint32_t)size)) {
            free(data);
            return MEDIA_ERR_NOT_FOUND;
        }
        e = free_slot();
        if (e == NULL) {
            free(data);
            return MEDIA_ERR_BUSY;
        }
        e->data = data;
        e->size = (uint32_t)size;
        e->bucket = (uint8_t)bucket;
        memcpy(e->key, key, len + 1);
        bytes_held += e->size;
    }
    e->refs++;
    out->data = e->data;
    out->size = e->size;
    out->handle = (uint32_t)(e - entries) + 1u;
    return MEDIA_OK;
}

void media_close(media_file_t *file) {
    if (file == NULL || file->handle == 0 || file->handle > entry_count) return;
    entry_t *e = &entries[file->handle - 1];
    file->handle = 0;
    if (e->data == NULL || e->refs == 0) return;
    if (--e->refs == 0 && e->stale) drop(e);
}

/* The key of the file that holds `data` (an image descriptor's pixels), NULL
 * when it is not media. */
const char *fbe_media_key_of(const uint8_t *data) {
    for (size_t i = 0; i < entry_count; i++) {
        const entry_t *e = &entries[i];
        if (e->data != NULL && data >= e->data && data < e->data + e->size) return e->key;
    }
    return NULL;
}

uint32_t fbe_media_bytes(void) { return bytes_held; }

/* The host's files of a bucket changed (-1: of every bucket). */
void fbe_media_forget(const int bucket) {
    for (size_t i = 0; i < entry_count; i++) {
        entry_t *e = &entries[i];
        if (e->data == NULL || (bucket >= 0 && e->bucket != bucket)) continue;
        if (e->refs == 0) drop(e);
        else e->stale = true;
    }
}

void fbe_media_forget_key(const int bucket, const char *key) {
    for (size_t i = 0; i < entry_count; i++) {
        entry_t *e = &entries[i];
        if (e->data == NULL || e->stale || e->bucket != bucket || strcmp(e->key, key) != 0) continue;
        if (e->refs == 0) drop(e);
        else e->stale = true;
    }
}
