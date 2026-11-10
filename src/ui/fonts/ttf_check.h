// Sanity check of a TrueType file before stb_truetype parses it.
//
// stb_truetype trusts every offset in the file. The fonts come from the
// user's S3 bucket, so a truncated or wrong upload would make it read past
// the mapped file and crash the device at every boot. This check walks the
// structures stb_truetype reads (table directory, head, hhea, maxp, hmtx,
// loca, the cmap subtable it picks, GPOS header) and rejects anything that
// points outside the file. It does not decode glyph outlines.
//
// Plain C, no LVGL: also built by the native tests.
#ifndef TTF_CHECK_H
#define TTF_CHECK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TTF_OK = 0,
    TTF_ERR_SHORT,     /* smaller than a table directory */
    TTF_ERR_MAGIC,     /* not a TrueType (glyf) font: CFF, collection, other */
    TTF_ERR_TABLES,    /* table directory points outside the file */
    TTF_ERR_MISSING,   /* a table stb_truetype needs is missing */
    TTF_ERR_HEAD,      /* head/hhea/maxp/hmtx inconsistent */
    TTF_ERR_LOCA,      /* loca too short or a glyph outside glyf */
    TTF_ERR_CMAP,      /* no usable Unicode cmap subtable, or it is truncated */
    TTF_ERR_GPOS,      /* GPOS lookup list points outside the file */
} ttf_check_t;

ttf_check_t ttf_check(const uint8_t *data, size_t size);

const char *ttf_check_str(ttf_check_t r);

#ifdef __cplusplus
}
#endif

#endif
