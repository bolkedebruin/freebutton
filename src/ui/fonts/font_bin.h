//
// Streaming reader for lv_font_conv "--format bin" fonts.
//
// Pure C, no LVGL / ESP-IDF / Arduino headers: all file access goes through
// the read callback in font_bin_io_t, so the format logic runs (and is unit
// tested) on the host. fonts.c wraps it into an lv_font_t.
//
// Only the cmap subtable headers live in RAM. Everything else (sparse cmap
// lists, loca, glyph records, kerning) is read from the file on demand.
//
// Format reference: lv_font_conv lib/font/table_*.js and compress.js.
//
#ifndef FONT_BIN_H
#define FONT_BIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* File access and memory for the cmap index. read_at reads exactly `len`
 * bytes at absolute offset `pos` and returns false on any short read. */
typedef struct {
    bool (*read_at)(void *ctx, uint32_t pos, void *dst, uint32_t len);
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    void *ctx;
} font_bin_io_t;

/* cmap subtable types, numbered as in the file */
enum {
    FONT_BIN_CMAP_FORMAT0_FULL = 0, /* uint8 glyph id delta per code point */
    FONT_BIN_CMAP_SPARSE_FULL = 1,  /* uint16 code deltas + uint16 glyph id deltas */
    FONT_BIN_CMAP_FORMAT0_TINY = 2, /* contiguous, no data */
    FONT_BIN_CMAP_SPARSE_TINY = 3,  /* uint16 code deltas, glyph ids contiguous */
};

typedef struct {
    uint32_t range_start;
    uint32_t data_pos; /* absolute file offset of the subtable data */
    uint16_t range_length;
    uint16_t glyph_id_start;
    uint16_t entries;
    uint8_t type;
} font_bin_cmap_t;

typedef struct {
    font_bin_io_t io;
    font_bin_cmap_t *cmaps; /* io.alloc'ed, the only per-font heap use */
    uint16_t cmap_count;
    uint16_t glyph_count; /* loca entries, including the reserved id 0 */

    uint32_t loca_pos; /* first loca entry */
    uint32_t glyf_pos; /* glyf table start; loca offsets are relative to it */
    uint32_t glyf_len;
    uint32_t kern_pos; /* kern sub-table data (after its header), 0 = none */
    uint32_t kern_pair_count;
    uint16_t kern_map_len;

    /* metrics, as stored in the head table */
    uint16_t font_size;
    uint16_t ascent;
    int16_t descent;
    uint16_t default_adv;
    uint16_t kern_scale; /* FP12.4 */
    uint16_t underline_thickness;

    uint8_t loca_32;     /* loca entries are uint32 (else uint16) */
    uint8_t gid_16;      /* kerning glyph ids are uint16 (else uint8) */
    uint8_t adv_fp;      /* advance widths are FP12.4 (else whole pixels) */
    uint8_t bpp;
    uint8_t xy_bits;
    uint8_t wh_bits;
    uint8_t adv_bits;
    uint8_t compression; /* 0 raw, 1 RLE + line XOR prefilter, 2 RLE only */
    uint8_t subpx;
    uint8_t kern_format; /* 0 sorted pairs, 3 class matrix */
    uint8_t kern_left_classes;
    uint8_t kern_right_classes;
} font_bin_t;

/* One glyph's metrics plus where its record sits in the file. */
typedef struct {
    uint32_t pos;   /* absolute offset of the glyph record */
    uint16_t len;   /* record length in bytes (header bits + bitmap) */
    uint16_t adv_w; /* advance width in 1/16 px */
    uint8_t box_w;
    uint8_t box_h;
    int8_t ofs_x;
    int8_t ofs_y;
} font_bin_glyph_t;

/* Parse head/cmap/loca/glyf/kern headers. On failure nothing stays allocated. */
bool font_bin_open(font_bin_t *f, const font_bin_io_t *io);
void font_bin_close(font_bin_t *f);

/* Glyph id for a code point, 0 if the font does not have it. */
uint16_t font_bin_glyph_id(const font_bin_t *f, uint32_t codepoint);

/* Read a glyph's metrics. False for id 0, out-of-range ids and read errors. */
bool font_bin_glyph(const font_bin_t *f, uint16_t gid, font_bin_glyph_t *out);

/* Raw FP4.4 kerning value for a glyph pair (0 = none). */
int8_t font_bin_kern_raw(const font_bin_t *f, uint16_t gid_left, uint16_t gid_right);

/* Kerning for a pair in 1/16 px, scaled like LVGL's fmt_txt fonts do. */
int32_t font_bin_kern(const font_bin_t *f, uint16_t gid_left, uint16_t gid_right);

/* Decode a glyph bitmap into `out` (box_h rows of `stride` bytes, one byte
 * per pixel). a8 = false keeps the raw bpp values (0 .. 2^bpp-1); a8 = true
 * scales them to 0..255 like LVGL. Streams the record through a small stack
 * buffer: no heap. */
bool font_bin_decode(const font_bin_t *f, const font_bin_glyph_t *g,
                     uint8_t *out, uint32_t stride, bool a8);

/* Tiny move-to-front cache of code point -> glyph lookups. Text layout asks
 * for the same few letters over and over, each miss costs several reads. */
#define FONT_BIN_CACHE_SIZE 8

typedef struct {
    uint32_t codepoint;
    uint16_t gid; /* 0 = cached miss */
    /* Kerning classes of the glyph (class matrix kerning only, 0 = none):
     * LVGL asks for the same pairs over and over, so a pair then costs one
     * read of the matrix value instead of three. */
    uint8_t kern_left;
    uint8_t kern_right;
    font_bin_glyph_t glyph;
} font_bin_cache_entry_t;

typedef struct {
    font_bin_cache_entry_t e[FONT_BIN_CACHE_SIZE];
    uint8_t used;
} font_bin_cache_t;

/* Code point -> glyph through the cache. False if the font lacks it. */
bool font_bin_lookup(const font_bin_t *f, font_bin_cache_t *c, uint32_t codepoint,
                     uint16_t *gid, font_bin_glyph_t *glyph);

/* As font_bin_lookup, but hands back the whole entry (with the kerning
 * classes for font_bin_kern_entries). */
bool font_bin_lookup_entry(const font_bin_t *f, font_bin_cache_t *c, uint32_t codepoint,
                           font_bin_cache_entry_t *out);

/* font_bin_kern for two looked-up glyphs, using their cached classes. */
int32_t font_bin_kern_entries(const font_bin_t *f, const font_bin_cache_entry_t *left,
                              const font_bin_cache_entry_t *right);

/* Glyph by id, using the cache when it holds it (the bitmap path). */
bool font_bin_glyph_cached(const font_bin_t *f, const font_bin_cache_t *c,
                           uint16_t gid, font_bin_glyph_t *glyph);

#ifdef __cplusplus
}
#endif

#endif
