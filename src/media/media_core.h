// Media store core: the platform-independent part of the store (directory
// log, allocation, reference counting). The flash comes in through
// media_flash_t, so the same code runs on the device (media_esp.c), in the
// native tests (RAM flash) and in the host tool tools/media/mkmedia
// (file-backed flash). On-flash format: docs/MEDIA-STORE.md "On-flash format".
#ifndef MEDIA_CORE_H
#define MEDIA_CORE_H

#include "media_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEDIA_PAGE_SIZE 0x10000u  /* MMU page, allocation unit of large files */
#define MEDIA_SECTOR_SIZE 0x1000u /* erase unit, allocation unit of small files */
#define MEDIA_SMALL_MAX (32u * 1024u) /* files up to this size share pages */
#define MEDIA_MAX_FILE_PAGES 64u
#define MEDIA_FILE_MAX (MEDIA_MAX_FILE_PAGES * MEDIA_PAGE_SIZE) /* 4 MB */
#define MEDIA_DIR_PAGES 2u        /* pages 0 and 1: the two directory halves */
#define MEDIA_MAX_PAGES 256u      /* page numbers are one byte */
#define MEDIA_MAX_OPEN 32         /* distinct open files (same key = one entry): fonts + image rasters */
#define MEDIA_MAX_WRITERS 2       /* uploads in progress */
#define MEDIA_MAX_ENTRIES 1000u   /* live entries (the directory bytes limit usually hits first) */

/* The partition as the core sees it. Offsets are relative to the start of
 * the partition. Every function returns 0 on success. */
typedef struct media_flash {
    void *ctx;
    uint32_t size; /* bytes, a multiple of MEDIA_PAGE_SIZE */
    int (*read)(void *ctx, uint32_t off, void *dst, uint32_t len);
    /* NOR programming: only bytes that are erased (0xFF) are written. */
    int (*write)(void *ctx, uint32_t off, const void *src, uint32_t len);
    /* off and len are multiples of MEDIA_SECTOR_SIZE. */
    int (*erase)(void *ctx, uint32_t off, uint32_t len);
    /* Map `n` pages (partition page numbers, in file order) to one
     * contiguous read-only range. */
    int (*map)(void *ctx, const uint8_t *pages, uint32_t n, const void **out, void **handle);
    void (*unmap)(void *ctx, void *handle);
    /* Optional (NULL = single-threaded). Not recursive. */
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
} media_flash_t;

/* Mount a store on `flash` (kept by pointer until media_unmount). Formats a
 * blank or unreadable store. Does not map anything. */
media_err_t media_mount(const media_flash_t *flash);

/* Forget the RAM state and unmap what is still open, without touching the
 * flash: a reboot for the tests and the end of a host tool run. */
void media_unmount(void);

#ifdef __cplusplus
}
#endif

#endif
