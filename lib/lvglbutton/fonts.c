//
// Created by Bolke de Bruin on 24/06/2024.
//

#include "ui.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>
#include <misc/lv_utils.h>

#include "lvgl.h"

#if LV_USE_FONT_COMPRESSED
    #include "core/lv_global.h"
    #include "font/lv_font_fmt_txt_private.h"
#endif

LV_FONT_DECLARE(mnt_semibold_14);
LV_FONT_DECLARE(mnt_semibold_18);

LV_FONT_DECLARE(mnt_medium_14);
//LV_FONT_DECLARE(mnt_medium_24);
LV_FONT_DECLARE(mnt_medium_32);
LV_FONT_DECLARE(mnt_medium_48);
LV_FONT_DECLARE(mnt_medium_72);

#if LV_USE_FONT_COMPRESSED
#define font_rle LV_GLOBAL_DEFAULT()->font_fmt_rle
#endif /*LV_USE_FONT_COMPRESSED*/

streaming_font_t * font14_semibold;
streaming_font_t * font18_semibold;
streaming_font_t * font14;
streaming_font_t * font24;
streaming_font_t * font32;
streaming_font_t * font48;
streaming_font_t * font72;

lv_font_t *fnt_semibold_14;
lv_font_t *fnt_semibold_18;

lv_font_t *fnt_medium_14;
lv_font_t *fnt_medium_24;
lv_font_t *fnt_medium_32;
lv_font_t *fnt_medium_48;
lv_font_t *fnt_medium_72;

typedef struct {
    uint32_t gid_left;
    uint32_t gid_right;
} kern_pair_ref_t;

static const uint8_t opa4_table[16] = {0,  17, 34,  51,
                                       68, 85, 102, 119,
                                       136, 153, 170, 187,
                                       204, 221, 238, 255
};

#if LV_USE_FONT_COMPRESSED
static const uint8_t opa3_table[8] = {0, 36, 73, 109, 146, 182, 218, 255};
#endif

static const uint8_t opa2_table[4] = {0, 85, 170, 255};

// Thread-local storage for unicode letter (LVGL 9.3 compatibility)
// Fallback to static if thread-local is not supported
#ifdef _Thread_local
static _Thread_local uint32_t current_unicode_letter = 0;
#else
static uint32_t current_unicode_letter = 0;
#endif


static int32_t load_glyph_dsc(lv_fs_file_t * fp, lv_font_fmt_txt_dsc_t * font_dsc,
                              uint32_t start, uint32_t * glyph_offset, uint32_t loca_count, font_header_bin_t * header,
                              uint32_t glyph_length)
{
    lv_font_fmt_txt_glyph_dsc_t * glyph_dsc = (lv_font_fmt_txt_glyph_dsc_t *)
            lv_malloc(loca_count * sizeof(lv_font_fmt_txt_glyph_dsc_t));

    memset(glyph_dsc, 0, loca_count * sizeof(lv_font_fmt_txt_glyph_dsc_t));

    font_dsc->glyph_dsc = glyph_dsc;

    int cur_bmp_size = 0;

    for(unsigned int i = 0; i < loca_count; ++i) {
        lv_font_fmt_txt_glyph_dsc_t * gdsc = &glyph_dsc[i];

        lv_fs_res_t res = lv_fs_seek(fp, start + glyph_offset[i], LV_FS_SEEK_SET);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        bit_iterator_t bit_it = init_bit_iterator(fp);

        if(header->advance_width_bits == 0) {
            gdsc->adv_w = header->default_advance_width;
        }
        else {
            gdsc->adv_w = read_bits(&bit_it, header->advance_width_bits, &res);
            if(res != LV_FS_RES_OK) {
                return -1;
            }
        }

        if(header->advance_width_format == 0) {
            gdsc->adv_w *= 16;
        }

        gdsc->ofs_x = read_bits_signed(&bit_it, header->xy_bits, &res);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        gdsc->ofs_y = read_bits_signed(&bit_it, header->xy_bits, &res);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        gdsc->box_w = read_bits(&bit_it, header->wh_bits, &res);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        gdsc->box_h = read_bits(&bit_it, header->wh_bits, &res);
        if(res != LV_FS_RES_OK) {
            return -1;
        }

        int nbits = header->advance_width_bits + 2 * header->xy_bits + 2 * header->wh_bits;
        int next_offset = (i < loca_count - 1) ? glyph_offset[i + 1] : (uint32_t)glyph_length;
        int bmp_size = next_offset - glyph_offset[i] - nbits / 8;

        if(i == 0) {
            gdsc->adv_w = 0;
            gdsc->box_w = 0;
            gdsc->box_h = 0;
            gdsc->ofs_x = 0;
            gdsc->ofs_y = 0;
        }

        gdsc->bitmap_index = cur_bmp_size;
        if(gdsc->box_w * gdsc->box_h != 0) {
            cur_bmp_size += bmp_size;
        }
    }

    return cur_bmp_size;
}

const void * streaming_font_get_glyph_bitmap(lv_font_glyph_dsc_t * g_dsc, lv_draw_buf_t * draw_buf)
{
    const lv_font_t * font = g_dsc->resolved_font;
    uint8_t * bitmap_out = draw_buf->data;

    // In LVGL 9.3, we get the unicode_letter from thread-local storage
    // This is set by the get_glyph_dsc function
    uint32_t unicode_letter = current_unicode_letter;
    
    if(unicode_letter == '\t') unicode_letter = ' ';

    streaming_font_t * fd = (streaming_font_t*)font->dsc;
    uint32_t gid = get_glyph_dsc_id(fd->font_dsc, unicode_letter);
    if(!gid) return NULL;

    const lv_font_fmt_txt_glyph_dsc_t * gdsc = &fd->font_dsc->glyph_dsc[gid];

    int32_t gsize = (int32_t) gdsc->box_w * gdsc->box_h;
    if(gsize == 0) return NULL;

    uint8_t * bitmap_in;
    uint32_t bitmap_size;

    bitmap_size = load_bitmap(fd, gid, &bitmap_in);
    if (!bitmap_size) {
        // load_bitmap failed, bitmap_in is not valid - don't free it
        return NULL;
    }
    uint8_t * bitmap_in_ptr = bitmap_in;

    uint8_t * bitmap_out_tmp = bitmap_out;
    int32_t i = 0;
    int32_t x, y;
    uint32_t stride = lv_draw_buf_width_to_stride(gdsc->box_w, LV_COLOR_FORMAT_A8);

    if(fd->font_dsc->bitmap_format == LV_FONT_FMT_TXT_PLAIN) {
        if(fd->font_dsc->bpp == 1) {
            for(y = 0; y < gdsc->box_h; y ++) {
                for(x = 0; x < gdsc->box_w; x++, i++) {
                    i = i & 0x7;
                    if(i == 0) bitmap_out_tmp[x] = (*bitmap_in) & 0x80 ? 0xff : 0x00;
                    else if(i == 1) bitmap_out_tmp[x] = (*bitmap_in) & 0x40 ? 0xff : 0x00;
                    else if(i == 2) bitmap_out_tmp[x] = (*bitmap_in) & 0x20 ? 0xff : 0x00;
                    else if(i == 3) bitmap_out_tmp[x] = (*bitmap_in) & 0x10 ? 0xff : 0x00;
                    else if(i == 4) bitmap_out_tmp[x] = (*bitmap_in) & 0x08 ? 0xff : 0x00;
                    else if(i == 5) bitmap_out_tmp[x] = (*bitmap_in) & 0x04 ? 0xff : 0x00;
                    else if(i == 6) bitmap_out_tmp[x] = (*bitmap_in) & 0x02 ? 0xff : 0x00;
                    else if(i == 7) {
                        bitmap_out_tmp[x] = (*bitmap_in) & 0x01 ? 0xff : 0x00;
                        bitmap_in++;
                    }
                }
                bitmap_out_tmp += stride;
            }
        }
        else if(fd->font_dsc->bpp == 2) {
            for(y = 0; y < gdsc->box_h; y ++) {
                for(x = 0; x < gdsc->box_w; x++, i++) {
                    i = i & 0x3;
                    if(i == 0) bitmap_out_tmp[x] = opa2_table[(*bitmap_in) >> 6];
                    else if(i == 1) bitmap_out_tmp[x] = opa2_table[((*bitmap_in) >> 4) & 0x3];
                    else if(i == 2) bitmap_out_tmp[x] = opa2_table[((*bitmap_in) >> 2) & 0x3];
                    else if(i == 3) {
                        bitmap_out_tmp[x] = opa2_table[((*bitmap_in) >> 0) & 0x3];
                        bitmap_in++;
                    }
                }
                bitmap_out_tmp += stride;
            }
        }
        else if(fd->font_dsc->bpp == 4) {
            for(y = 0; y < gdsc->box_h; y ++) {
                for(x = 0; x < gdsc->box_w; x++, i++) {
                    i = i & 0x1;
                    if(i == 0) {
                        bitmap_out_tmp[x] = opa4_table[(*bitmap_in) >> 4];
                    }
                    else if(i == 1) {
                        bitmap_out_tmp[x] = opa4_table[(*bitmap_in) & 0xF];
                        bitmap_in++;
                    }
                }
                bitmap_out_tmp += stride;
            }
        }
        if (bitmap_size > 0) {
            lv_free(bitmap_in_ptr);
        }
        return draw_buf;
    }
        /*Handle compressed bitmap*/
    else {
#if LV_USE_FONT_COMPRESSED
        bool prefilter = fd->font_dsc->bitmap_format == LV_FONT_FMT_TXT_COMPRESSED;
        decompress(bitmap_in, bitmap_out, gdsc->box_w, gdsc->box_h,
                   (uint8_t)fd->font_dsc->bpp, prefilter);
        lv_free(bitmap_in_ptr);
        return draw_buf;
#else /*!LV_USE_FONT_COMPRESSED*/
        LV_LOG_WARN("Compressed fonts is used but LV_USE_FONT_COMPRESSED is not enabled in lv_conf.h");
        return NULL;
#endif
    }
    lv_free(bitmap_in_ptr);
    LV_LOG_ERROR("Should not reach this point");
    /*If not returned earlier then the letter is not found in this font*/
    return NULL;
}

uint32_t load_bitmap(const streaming_font_t * fd, const uint32_t gid, uint32_t ** bitmap_out)
{
    uint32_t start = fd->glyph_start;
    uint32_t glyph_offset = fd->glyph_offset[gid];

    // Calculate the stored bitmap size based on the offset information
    uint32_t next_offset = (gid < fd->glyph_count - 1) ? fd->glyph_offset[gid + 1] : fd->glyph_length;
    uint32_t bitmap_size = next_offset - glyph_offset - fd->nbits / 8;

    uint32_t start_pos = start + glyph_offset;

    lv_fs_res_t res = lv_fs_seek(&fd->file, start_pos, LV_FS_SEEK_SET);
    if (res != LV_FS_RES_OK) {
        return 0;  // Return 0 instead of -1 to indicate failure
    }

    bit_iterator_t bit_it = init_bit_iterator(&fd->file);

    // Read the preceding bits
    read_bits(&bit_it, fd->nbits, &res);
    if(res != LV_FS_RES_OK) {
        LV_LOG_WARN("Failed to read preceding glyph bits.");
        return 0;  // Return 0 instead of -1 to indicate failure
    }

    uint8_t  * bitmap = (uint8_t *)lv_malloc(bitmap_size);
    if (bitmap == NULL) {
        LV_LOG_WARN("Failed to allocate memory for bitmap");
        return 0;  // Return 0 instead of false to indicate failure
    }
    memset(bitmap, 0, bitmap_size);

    // Read the bitmap data
    uint32_t read_bytes;
    if (fd->nbits % 8 == 0) {
        /* fast path */
        res = lv_fs_read(&fd->file, bitmap, bitmap_size, &read_bytes);
        if (res != LV_FS_RES_OK || read_bytes != bitmap_size) {
            lv_free(bitmap);  // Free memory on error
            return 0;  // Return 0 instead of -1 to indicate failure
        }
    } else {
        uint8_t byte = 0;
        uint8_t bit = 0;
        for (int k=0; k < bitmap_size - 1; k++) {
            byte = read_bits(&bit_it, 8, &res);
            if (res != LV_FS_RES_OK) {
                lv_free(bitmap);  // Free memory on error
                return 0;  // Return 0 instead of -1 to indicate failure
            }
            bitmap[k] = byte;
        }
        bitmap[bitmap_size - 1] = read_bits(&bit_it, 8 - fd->nbits % 8, &res);
        if (res != LV_FS_RES_OK) {
            lv_free(bitmap);  // Free memory on error
            return 0;  // Return 0 instead of -1 to indicate failure
        }
        /*The last fragment should be on the MSB but read_bits() will place it to the LSB*/
        bitmap[bitmap_size - 1] = bitmap[bitmap_size - 1] << (fd->nbits % 8);

    }
    *bitmap_out = bitmap;
    return bitmap_size;
}

static uint32_t get_glyph_dsc_id(const lv_font_fmt_txt_dsc_t * fdsc, uint32_t letter)
{
    if(letter == '\0') return 0;

    uint16_t i;
    for(i = 0; i < fdsc->cmap_num; i++) {

        /*Relative code point*/
        uint32_t rcp = letter - fdsc->cmaps[i].range_start;
        if(rcp >= fdsc->cmaps[i].range_length) continue;
        uint32_t glyph_id = 0;
        if(fdsc->cmaps[i].type == LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY) {
            glyph_id = fdsc->cmaps[i].glyph_id_start + rcp;
        }
        else if(fdsc->cmaps[i].type == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL) {
            const uint8_t * gid_ofs_8 = fdsc->cmaps[i].glyph_id_ofs_list;
            glyph_id = fdsc->cmaps[i].glyph_id_start + gid_ofs_8[rcp];
        }
        else if(fdsc->cmaps[i].type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY) {
            uint16_t key = rcp;
            uint16_t * p = lv_utils_bsearch(&key, fdsc->cmaps[i].unicode_list, fdsc->cmaps[i].list_length,
                                             sizeof(fdsc->cmaps[i].unicode_list[0]), unicode_list_compare);

            if(p) {
                lv_uintptr_t ofs = p - fdsc->cmaps[i].unicode_list;
                glyph_id = fdsc->cmaps[i].glyph_id_start + (uint32_t) ofs;
            }
        }
        else if(fdsc->cmaps[i].type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL) {
            uint16_t key = rcp;
            uint16_t * p = lv_utils_bsearch(&key, fdsc->cmaps[i].unicode_list, fdsc->cmaps[i].list_length,
                                             sizeof(fdsc->cmaps[i].unicode_list[0]), unicode_list_compare);

            if(p) {
                lv_uintptr_t ofs = p - fdsc->cmaps[i].unicode_list;
                const uint16_t * gid_ofs_16 = fdsc->cmaps[i].glyph_id_ofs_list;
                glyph_id = fdsc->cmaps[i].glyph_id_start + gid_ofs_16[ofs];
            }
        }

        return glyph_id;
    }

    return 0;

}

#ifdef BUTTONPLUS_ONDEMAND_KERN
static int8_t get_kern_value(streaming_font_t *fd, uint32_t gid_left, uint32_t gid_right)
{
    int8_t value = 0;

    if (fd->kern_format == 0) { // Sorted pairs
        // Binary search for the kerning pair
        uint32_t start = 0;
        uint32_t end = fd->kern_length / (2 * sizeof(uint32_t) + sizeof(int8_t));

        while (start < end) {
            uint32_t mid = (start + end) / 2;
            uint32_t offset = fd->kern_offset + 4 + mid * (2 * sizeof(uint32_t) + sizeof(int8_t));

            uint32_t left, right;
            lv_fs_seek(&fd->file, offset, LV_FS_SEEK_SET);
            lv_fs_read(&fd->file, &left, sizeof(uint32_t), NULL);
            lv_fs_read(&fd->file, &right, sizeof(uint32_t), NULL);

            if (left == gid_left && right == gid_right) {
                lv_fs_read(&fd->file, &value, sizeof(int8_t), NULL);
                break;
            } else if (left < gid_left || (left == gid_left && right < gid_right)) {
                start = mid + 1;
            } else {
                end = mid;
            }
        }
    }
    else if (fd->kern_format == 3) { // Class-based kerning
        // Read class mappings and kerning values
        uint16_t left_class_cnt, right_class_cnt;
        lv_fs_seek(&fd->file, fd->kern_offset + 4, LV_FS_SEEK_SET);
        lv_fs_read(&fd->file, &left_class_cnt, sizeof(uint16_t), NULL);
        lv_fs_read(&fd->file, &right_class_cnt, sizeof(uint16_t), NULL);

        uint8_t left_class = 0, right_class = 0;

        // Find left class
        for (uint16_t i = 0; i < left_class_cnt; i++) {
            uint32_t glyph_id;
            uint8_t class_id;
            lv_fs_read(&fd->file, &glyph_id, sizeof(uint32_t), NULL);
            lv_fs_read(&fd->file, &class_id, sizeof(uint8_t), NULL);
            if (glyph_id == gid_left) {
                left_class = class_id;
                break;
            }
        }

        // Find right class
        for (uint16_t i = 0; i < right_class_cnt; i++) {
            uint32_t glyph_id;
            uint8_t class_id;
            lv_fs_read(&fd->file, &glyph_id, sizeof(uint32_t), NULL);
            lv_fs_read(&fd->file, &class_id, sizeof(uint8_t), NULL);
            if (glyph_id == gid_right) {
                right_class = class_id;
                break;
            }
        }

        // Read kerning value
        if (left_class > 0 && right_class > 0) {
            uint32_t offset = fd->kern_offset + 4 + 2 * sizeof(uint16_t) +
                              (left_class_cnt + right_class_cnt) * (sizeof(uint32_t) + sizeof(uint8_t)) +
                              ((left_class - 1) * right_class_cnt + (right_class - 1)) * sizeof(int8_t);
            lv_fs_seek(&fd->file, offset, LV_FS_SEEK_SET);
            lv_fs_read(&fd->file, &value, sizeof(int8_t), NULL);
        }
    }

    return value;
}
#else
static int8_t get_kern_value(const lv_font_fmt_txt_dsc_t * fdsc, uint32_t gid_left, uint32_t gid_right)
{
    int8_t value = 0;

    if(fdsc->kern_classes == 0) {
        /*Kern pairs*/
        const lv_font_fmt_txt_kern_pair_t * kdsc = fdsc->kern_dsc;
        if(kdsc->glyph_ids_size == 0) {
            /*Use binary search to find the kern value.
             *The pairs are ordered left_id first, then right_id secondly.*/
            const uint16_t * g_ids = kdsc->glyph_ids;
            kern_pair_ref_t g_id_both = {gid_left, gid_right};
            uint16_t * kid_p = lv_utils_bsearch(&g_id_both, g_ids, kdsc->pair_cnt, 2, kern_pair_8_compare);

            /*If the `g_id_both` were found get its index from the pointer*/
            if(kid_p) {
                lv_uintptr_t ofs = kid_p - g_ids;
                value = kdsc->values[ofs];
            }
        }
        else if(kdsc->glyph_ids_size == 1) {
            /*Use binary search to find the kern value.
             *The pairs are ordered left_id first, then right_id secondly.*/
            const uint32_t * g_ids = kdsc->glyph_ids;
            kern_pair_ref_t g_id_both = {gid_left, gid_right};
            uint32_t * kid_p = lv_utils_bsearch(&g_id_both, g_ids, kdsc->pair_cnt, 4, kern_pair_16_compare);

            /*If the `g_id_both` were found get its index from the pointer*/
            if(kid_p) {
                lv_uintptr_t ofs = kid_p - g_ids;
                value = kdsc->values[ofs];
            }

        }
        else {
            /*Invalid value*/
        }
    }
    else {
        /*Kern classes*/
        const lv_font_fmt_txt_kern_classes_t * kdsc = fdsc->kern_dsc;
        uint8_t left_class = kdsc->left_class_mapping[gid_left];
        uint8_t right_class = kdsc->right_class_mapping[gid_right];

        /*If class = 0, kerning not exist for that glyph
         *else got the value form `class_pair_values` 2D array*/
        if(left_class > 0 && right_class > 0) {
            value = kdsc->class_pair_values[(left_class - 1) * kdsc->right_class_cnt + (right_class - 1)];
        }

    }
    return value;
}
#endif

bool streaming_font_get_glyph_dsc(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter,
                                  uint32_t unicode_letter_next)
{
    // Store unicode_letter in thread-local storage for LVGL 9.3 compatibility
    current_unicode_letter = unicode_letter;
    
    /*It fixes a strange compiler optimization issue: https://github.com/lvgl/lvgl/issues/4370*/
    bool is_tab = unicode_letter == '\t';
    if(is_tab) {
        unicode_letter = ' ';
    }
    streaming_font_t * fd = (streaming_font_t*)font->dsc;
    uint32_t gid = get_glyph_dsc_id(fd->font_dsc, unicode_letter);
    if(!gid) return false;

    int8_t kvalue = 0;
#ifdef BUTTONPLUS_ONDEMAND_KERN
    if(unicode_letter_next) {
        uint32_t gid_next = get_glyph_dsc_id(fd->font_dsc, unicode_letter_next);
        if(gid_next) {
            kvalue = get_kern_value((streaming_font_t*)font->dsc, gid, gid_next);
        }
    }
#else
    if(fd->font_dsc->kern_dsc) {
        uint32_t gid_next = get_glyph_dsc_id(fd->font_dsc, unicode_letter_next);
        if(gid_next) {
            kvalue = get_kern_value(fd->font_dsc, gid, gid_next);
        }
    }
#endif
    /*Put together a glyph dsc*/
    const lv_font_fmt_txt_glyph_dsc_t * gdsc = &fd->font_dsc->glyph_dsc[gid];

    int32_t kv = ((int32_t)((int32_t)kvalue * fd->font_dsc->kern_scale) >> 4);

    uint32_t adv_w = gdsc->adv_w;
    if(is_tab) adv_w *= 2;

    adv_w += kv;
    adv_w  = (adv_w + (1 << 3)) >> 4;

    dsc_out->adv_w = adv_w;
    dsc_out->box_h = gdsc->box_h;
    dsc_out->box_w = gdsc->box_w;
    dsc_out->ofs_x = gdsc->ofs_x;
    dsc_out->ofs_y = gdsc->ofs_y;
    dsc_out->format = (uint8_t)fd->font_dsc->bpp;
    dsc_out->is_placeholder = false;

    if(is_tab) dsc_out->box_w = dsc_out->box_w * 2;

    return true;
}

streaming_font_t* streaming_font_init(const char* font_file, uint32_t cache_size)
{
    lv_mem_monitor_t mon;

    lv_mem_monitor(&mon);
    LV_LOG_INFO("streaming_font_init start memory: total: %d used: %d free: %d\n", (int)mon.total_size, (int)mon.total_size - mon.free_size, (int)mon.free_size);

    streaming_font_t* fd = (streaming_font_t*)lv_malloc(sizeof(streaming_font_t));
    if (fd == NULL) return NULL;

    lv_font_fmt_txt_dsc_t * font_dsc = (lv_font_fmt_txt_dsc_t *)
            lv_malloc(sizeof(lv_font_fmt_txt_dsc_t));

    memset(font_dsc, 0, sizeof(lv_font_fmt_txt_dsc_t));

    fd->font_dsc = font_dsc;

    /* Open the file */
    lv_fs_res_t res = lv_fs_open(&fd->file, font_file, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        lv_free(fd);
        return NULL;
    }

    fd->font.get_glyph_dsc = streaming_font_get_glyph_dsc;
    fd->font.get_glyph_bitmap = streaming_font_get_glyph_bitmap;
    fd->font.dsc = fd;

    /*header*/
    int32_t header_length = read_label(&fd->file, 0, "head");
    if(header_length < 0) {
        return false;
    }

    /* Read font header */
    font_header_bin_t font_header;
    lv_fs_read(&fd->file, &font_header, sizeof(font_header_bin_t), NULL);

    // precalculate nbits
    fd->nbits = font_header.advance_width_bits + 2 * font_header.xy_bits + 2 * font_header.wh_bits;

    // Set the font properties based on the font header
    fd->font.base_line = -font_header.descent;
    fd->font.line_height = font_header.ascent - font_header.descent;
    fd->font.subpx = font_header.subpixels_mode;
    fd->font.underline_position = (int8_t)font_header.underline_position;
    fd->font.underline_thickness = (int8_t)font_header.underline_thickness;

    fd->font_dsc->bpp = font_header.bits_per_pixel;
    fd->font_dsc->kern_scale = font_header.kerning_scale;
    fd->font_dsc->bitmap_format = font_header.compression_id;

    /*cmaps*/
    uint32_t cmaps_start = header_length;
    int32_t cmaps_length = load_cmaps(&fd->file, fd->font_dsc, cmaps_start);
    if(cmaps_length < 0) {
        return NULL;
    }
    fd->cmaps_start = cmaps_start;

    /*loca*/
    uint32_t loca_start = cmaps_start + cmaps_length;
    int32_t loca_length = read_label(&fd->file, loca_start, "loca");
    if(loca_length < 0) {
        return NULL;
    }

    uint32_t loca_count;
    if(lv_fs_read(&fd->file, &loca_count, sizeof(uint32_t), NULL) != LV_FS_RES_OK) {
        return NULL;
    }
    fd->glyph_count = loca_count;

    bool failed = false;
    uint32_t * glyph_offset = lv_malloc(sizeof(uint32_t) * (loca_count + 1));

    if(font_header.index_to_loc_format == 0) {
        for(unsigned int i = 0; i < loca_count; ++i) {
            uint16_t offset;
            if(lv_fs_read(&fd->file, &offset, sizeof(uint16_t), NULL) != LV_FS_RES_OK) {
                failed = true;
                break;
            }
            glyph_offset[i] = offset;
        }
    }
    else if(font_header.index_to_loc_format == 1) {
        if(lv_fs_read(&fd->file, glyph_offset, loca_count * sizeof(uint32_t), NULL) != LV_FS_RES_OK) {
            failed = true;
        }
    }
    else {
        LV_LOG_WARN("Unknown index_to_loc_format: %d.", font_header.index_to_loc_format);
        failed = true;
    }

    if(failed) {
        lv_free(glyph_offset);
        return false;
    }

    fd->glyph_offset = glyph_offset;
    fd->glyph_start = loca_start + loca_length;

    /*kerning*/
    if(font_header.tables_count < 4) {
        font_dsc->kern_dsc = NULL;
        font_dsc->kern_classes = 0;
        font_dsc->kern_scale = 0;
        return fd;
    }

    int32_t glyph_length = read_label(&fd->file, (int)fd->glyph_start, "glyf");
    if(glyph_length < 0) {
        return NULL;
    }

    load_glyph_dsc(&fd->file, font_dsc, fd->glyph_start, glyph_offset, loca_count, &font_header, glyph_length);

    uint32_t kern_start = fd->glyph_start + glyph_length;
#ifndef BUTTONPLUS_ONDEMAND_KERN
    int32_t kern_length = load_kern(&fd->file, font_dsc, font_header.glyph_id_format, kern_start);
#else
    int32_t kern_length = read_label(&fd->file, kern_start, "kern");

    uint8_t kern_format_type;
    int32_t padding;
    if(lv_fs_read(&fd->file, &kern_format_type, sizeof(uint8_t), NULL) != LV_FS_RES_OK ||
       lv_fs_read(&fd->file, &padding, 3 * sizeof(uint8_t), NULL) != LV_FS_RES_OK) {
        return NULL;
    }

    fd->kern_offset = kern_start;
    fd->kern_length = kern_length;
    fd->kern_format = kern_format_type;
#endif
    if (kern_length < 0) {
        return NULL;
    }

    lv_mem_monitor(&mon);
    LV_LOG_INFO("streaming_font_init done memory: total: %d used: %d free: %d\n", (int)mon.total_size, (int)mon.total_size - mon.free_size, (int)mon.free_size);

    return fd;
}

void destroy_streaming_font(streaming_font_t * fd) {
    if (fd != NULL) {
        lv_free(fd->font_dsc->cmaps);
        lv_free(fd->font_dsc->glyph_dsc);
        lv_free(fd->font_dsc);
        lv_free(fd->glyph_offset);
        lv_fs_close(&fd->file);
        lv_free(fd);
    }
}

void destroy_fonts() {
    destroy_streaming_font(font14_semibold);
    destroy_streaming_font(font18_semibold);

    destroy_streaming_font(font14);
    destroy_streaming_font(font24);
    destroy_streaming_font(font32);
    destroy_streaming_font(font48);
    destroy_streaming_font(font72);

    // set to build in fonts
    fnt_semibold_14 = &mnt_semibold_14;
    fnt_semibold_18 = &mnt_semibold_18;

    fnt_medium_14 = &mnt_medium_14;
    //fnt_medium_24 = &mnt_medium_24;
    fnt_medium_32 = &mnt_medium_32;
    fnt_medium_48 = &mnt_medium_48;
    fnt_medium_72 = &mnt_medium_72;
}

void init_fonts() {
    // Register the fonts
    font14_semibold = streaming_font_init("S:/fonts/mnt_semibold_14.bin", 0);
    if (font14_semibold == NULL) {
        LV_LOG_WARN("Failed to create font. Loading built-in");
        fnt_semibold_14 = &mnt_semibold_14;
    } else {
        fnt_semibold_14 = &font14_semibold->font;
    }

    font18_semibold = streaming_font_init("S:/fonts/mnt_semibold_18.bin", 0);
    if (font18_semibold == NULL) {
        LV_LOG_WARN("Failed to create font. Loading built-in");
        fnt_semibold_18 = &mnt_semibold_18;
    } else {
        fnt_semibold_18 = &font18_semibold->font;
    }

    font24 = streaming_font_init("S:/fonts/mnt_medium_24.bin", 0);
    if (font24 == NULL) {
        LV_LOG_WARN("Failed to create font. Loading built-in");
        //fnt_medium_24 = &mnt_medium_24;
    } else {
        fnt_medium_24 = &font24->font;
    }

    font32 = streaming_font_init("S:/fonts/mnt_medium_32.bin", 0);
    if (font32 == NULL) {
        LV_LOG_WARN("Failed to create font. Loading built-in");
        fnt_medium_32 = &mnt_medium_32;
    } else {
        fnt_medium_32 = &font32->font;
    }

    font14 = streaming_font_init("S:/fonts/mnt_medium_14.bin", 0);
    if (font14 == NULL) {
        LV_LOG_WARN("Failed to create font. Loading built-in");
        fnt_medium_14 = &mnt_medium_14;
    } else {
        fnt_medium_14 = &font14->font;
    }

    font48 = streaming_font_init("S:/fonts/mnt_medium_48.bin", 0);
    if (font48 == NULL) {
        LV_LOG_WARN("Failed to create font. Loading built-in");
        fnt_medium_48 = &mnt_medium_48;
    } else {
        fnt_medium_48 = &font48->font;
    }


    font72 = streaming_font_init("S:/fonts/mnt_medium_72.bin", 0);
    if (font72 == NULL) {
        LV_LOG_WARN("Failed to create font. Loading built-in");
        fnt_medium_72 = &mnt_medium_72;
    } else {
        fnt_medium_72 = &font72->font;
    }
}


/*********************************************
 *   STATIC FUNCTIONS FROM lv_binfont_loader.c
 *********************************************/

static bit_iterator_t init_bit_iterator(lv_fs_file_t * fp)
{
    bit_iterator_t it;
    it.fp = fp;
    it.bit_pos = -1;
    it.byte_value = 0;
    return it;
}

static unsigned int read_bits(bit_iterator_t * it, int n_bits, lv_fs_res_t * res)
{
    unsigned int value = 0;
    while(n_bits--) {
        it->byte_value = it->byte_value << 1;
        it->bit_pos--;

        if(it->bit_pos < 0) {
            it->bit_pos = 7;
            *res = lv_fs_read(it->fp, &(it->byte_value), 1, NULL);
            if(*res != LV_FS_RES_OK) {
                return 0;
            }
        }
        int8_t bit = (it->byte_value & 0x80) ? 1 : 0;

        value |= (bit << n_bits);
    }
    *res = LV_FS_RES_OK;
    return value;
}

static int read_bits_signed(bit_iterator_t * it, int n_bits, lv_fs_res_t * res)
{
    unsigned int value = read_bits(it, n_bits, res);
    if(value & (1 << (n_bits - 1))) {
        value |= ~0u << n_bits;
    }
    return value;
}

static int read_label(lv_fs_file_t * fp, int start, const char * label)
{
    lv_fs_seek(fp, start, LV_FS_SEEK_SET);

    uint32_t length;
    char buf[4];

    if(lv_fs_read(fp, &length, 4, NULL) != LV_FS_RES_OK
       || lv_fs_read(fp, buf, 4, NULL) != LV_FS_RES_OK
       || memcmp(label, buf, 4) != 0) {
        LV_LOG_WARN("Error reading '%s' label.", label);
        return -1;
    }

    return length;
}

static bool load_cmaps_tables(lv_fs_file_t * fp, lv_font_fmt_txt_dsc_t * font_dsc,
                              uint32_t cmaps_start, cmap_table_bin_t * cmap_table)
{
    if(lv_fs_read(fp, cmap_table, font_dsc->cmap_num * sizeof(cmap_table_bin_t), NULL) != LV_FS_RES_OK) {
        return false;
    }

    for(unsigned int i = 0; i < font_dsc->cmap_num; ++i) {
        lv_fs_res_t res = lv_fs_seek(fp, cmaps_start + cmap_table[i].data_offset, LV_FS_SEEK_SET);
        if(res != LV_FS_RES_OK) {
            return false;
        }

        lv_font_fmt_txt_cmap_t * cmap = (lv_font_fmt_txt_cmap_t *) & (font_dsc->cmaps[i]);

        cmap->range_start = cmap_table[i].range_start;
        cmap->range_length = cmap_table[i].range_length;
        cmap->glyph_id_start = cmap_table[i].glyph_id_start;
        cmap->type = cmap_table[i].format_type;

        switch(cmap_table[i].format_type) {
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL: {
                uint8_t ids_size = (uint8_t)(sizeof(uint8_t) * cmap_table[i].data_entries_count);
                uint8_t * glyph_id_ofs_list = lv_malloc(ids_size);

                cmap->glyph_id_ofs_list = glyph_id_ofs_list;

                if(lv_fs_read(fp, glyph_id_ofs_list, ids_size, NULL) != LV_FS_RES_OK) {
                    return false;
                }

                cmap->list_length = cmap->range_length;
                break;
            }
            case LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY:
                break;
            case LV_FONT_FMT_TXT_CMAP_SPARSE_FULL:
            case LV_FONT_FMT_TXT_CMAP_SPARSE_TINY: {
                uint32_t list_size = sizeof(uint16_t) * cmap_table[i].data_entries_count;
                uint16_t * unicode_list = (uint16_t *)lv_malloc(list_size);

                cmap->unicode_list = unicode_list;
                cmap->list_length = cmap_table[i].data_entries_count;

                if(lv_fs_read(fp, unicode_list, list_size, NULL) != LV_FS_RES_OK) {
                    return false;
                }

                if(cmap_table[i].format_type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL) {
                    uint16_t * buf = lv_malloc(sizeof(uint16_t) * cmap->list_length);

                    cmap->glyph_id_ofs_list = buf;

                    if(lv_fs_read(fp, buf, sizeof(uint16_t) * cmap->list_length, NULL) != LV_FS_RES_OK) {
                        return false;
                    }
                }
                break;
            }
            default:
                LV_LOG_WARN("Unknown cmaps format type %d.", cmap_table[i].format_type);
                return false;
        }
    }
    return true;
}

static int32_t load_cmaps(lv_fs_file_t * fp, lv_font_fmt_txt_dsc_t * font_dsc, uint32_t cmaps_start)
{
    int32_t cmaps_length = read_label(fp, cmaps_start, "cmap");
    if(cmaps_length < 0) {
        return -1;
    }

    uint32_t cmaps_subtables_count;
    if(lv_fs_read(fp, &cmaps_subtables_count, sizeof(uint32_t), NULL) != LV_FS_RES_OK) {
        return -1;
    }

    lv_font_fmt_txt_cmap_t * cmaps =
            lv_malloc(cmaps_subtables_count * sizeof(lv_font_fmt_txt_cmap_t));

    memset(cmaps, 0, cmaps_subtables_count * sizeof(lv_font_fmt_txt_cmap_t));

    font_dsc->cmaps = cmaps;
    font_dsc->cmap_num = cmaps_subtables_count;

    cmap_table_bin_t * cmaps_tables = lv_malloc(sizeof(cmap_table_bin_t) * font_dsc->cmap_num);

    bool success = load_cmaps_tables(fp, font_dsc, cmaps_start, cmaps_tables);

    lv_free(cmaps_tables);

    return success ? cmaps_length : -1;
}

/*********************************************
 *   STATIC FUNCTIONS FROM lv_binfont_loader.c
 *********************************************/

static int32_t kern_pair_8_compare(const void * ref, const void * element)
{
    const kern_pair_ref_t * ref8_p = ref;
    const uint8_t * element8_p = element;

    /*If the MSB is different it will matter. If not return the diff. of the LSB*/
    if(ref8_p->gid_left != element8_p[0]) return (int32_t) ref8_p->gid_left - element8_p[0];
    else return (int32_t) ref8_p->gid_right - element8_p[1];

}

static int32_t kern_pair_16_compare(const void * ref, const void * element)
{
    const kern_pair_ref_t * ref16_p = ref;
    const uint16_t * element16_p = element;

    /*If the MSB is different it will matter. If not return the diff. of the LSB*/
    if(ref16_p->gid_left != element16_p[0]) return (int32_t) ref16_p->gid_left - element16_p[0];
    else return (int32_t) ref16_p->gid_right - element16_p[1];
}

#if LV_USE_FONT_COMPRESSED

/**
 * The compress a glyph's bitmap
 * @param in the compressed bitmap
 * @param out buffer to store the result
 * @param px_num number of pixels in the glyph (width * height)
 * @param bpp bit per pixel (bpp = 3 will be converted to bpp = 4)
 * @param prefilter true: the lines are XORed
 */
static void  decompress(const uint8_t * in, uint8_t * out, int32_t w, int32_t h, uint8_t bpp, bool prefilter)
{
    const lv_opa_t * opa_table;
    switch(bpp) {
        case 2:
            opa_table = opa2_table;
            break;
        case 3:
            opa_table = opa3_table;
            break;
        case 4:
            opa_table = opa4_table;
            break;
        default:
            LV_LOG_WARN("%d bpp is not handled", bpp);
            return;
    }

    rle_init(in, bpp);

    uint8_t * line_buf1 = lv_malloc(w);

    uint8_t * line_buf2 = NULL;

    if(prefilter) {
        line_buf2 = lv_malloc(w);
    }

    decompress_line(line_buf1, w);

    int32_t y;
    int32_t x;
    uint32_t stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_A8);

    for(x = 0; x < w; x++) {
        out[x] = opa_table[line_buf1[x]];
    }
    out += stride;

    for(y = 1; y < h; y++) {
        if(prefilter) {
            decompress_line(line_buf2, w);

            for(x = 0; x < w; x++) {
                line_buf1[x] = line_buf2[x] ^ line_buf1[x];
                out[x] = opa_table[line_buf1[x]];
            }
        }
        else {
            decompress_line(line_buf1, w);

            for(x = 0; x < w; x++) {
                out[x] = opa_table[line_buf1[x]];
            }
        }
        out += stride;
    }

    lv_free(line_buf1);
    lv_free(line_buf2);
}

/**
 * Decompress one line. Store one pixel per byte
 * @param out output buffer
 * @param w width of the line in pixel count
 */
static inline void decompress_line(uint8_t * out, int32_t w)
{
    int32_t i;
    for(i = 0; i < w; i++) {
        out[i] = rle_next();
    }
}

/**
 * Read bits from an input buffer. The read can cross byte boundary.
 * @param in the input buffer to read from.
 * @param bit_pos index of the first bit to read.
 * @param len number of bits to read (must be <= 8).
 * @return the read bits
 */
static inline uint8_t get_bits(const uint8_t * in, uint32_t bit_pos, uint8_t len)
{
    uint8_t bit_mask;
    switch(len) {
        case 1:
            bit_mask = 0x1;
            break;
        case 2:
            bit_mask = 0x3;
            break;
        case 3:
            bit_mask = 0x7;
            break;
        case 4:
            bit_mask = 0xF;
            break;
        case 8:
            bit_mask = 0xFF;
            break;
        default:
            bit_mask = (uint16_t)((uint16_t) 1 << len) - 1;
    }

    uint32_t byte_pos = bit_pos >> 3;
    bit_pos = bit_pos & 0x7;

    if(bit_pos + len >= 8) {
        uint16_t in16 = (in[byte_pos] << 8) + in[byte_pos + 1];
        return (in16 >> (16 - bit_pos - len)) & bit_mask;
    }
    else {
        return (in[byte_pos] >> (8 - bit_pos - len)) & bit_mask;
    }
}

static inline void rle_init(const uint8_t * in,  uint8_t bpp)
{
    lv_font_fmt_rle_t * rle = &font_rle;
    rle->in = in;
    rle->bpp = bpp;
    rle->state = RLE_STATE_SINGLE;
    rle->rdp = 0;
    rle->prev_v = 0;
    rle->count = 0;
}

static inline uint8_t rle_next(void)
{
    uint8_t v = 0;
    uint8_t ret = 0;
    lv_font_fmt_rle_t * rle = &font_rle;

    if(rle->state == RLE_STATE_SINGLE) {
        ret = get_bits(rle->in, rle->rdp, rle->bpp);
        if(rle->rdp != 0 && rle->prev_v == ret) {
            rle->count = 0;
            rle->state = RLE_STATE_REPEATED;
        }

        rle->prev_v = ret;
        rle->rdp += rle->bpp;
    }
    else if(rle->state == RLE_STATE_REPEATED) {
        v = get_bits(rle->in, rle->rdp, 1);
        rle->count++;
        rle->rdp += 1;
        if(v == 1) {
            ret = rle->prev_v;
            if(rle->count == 11) {
                rle->count = get_bits(rle->in, rle->rdp, 6);
                rle->rdp += 6;
                if(rle->count != 0) {
                    rle->state = RLE_STATE_COUNTER;
                }
                else {
                    ret = get_bits(rle->in, rle->rdp, rle->bpp);
                    rle->prev_v = ret;
                    rle->rdp += rle->bpp;
                    rle->state = RLE_STATE_SINGLE;
                }
            }
        }
        else {
            ret = get_bits(rle->in, rle->rdp, rle->bpp);
            rle->prev_v = ret;
            rle->rdp += rle->bpp;
            rle->state = RLE_STATE_SINGLE;
        }

    }
    else if(rle->state == RLE_STATE_COUNTER) {
        ret = rle->prev_v;
        rle->count--;
        if(rle->count == 0) {
            ret = get_bits(rle->in, rle->rdp, rle->bpp);
            rle->prev_v = ret;
            rle->rdp += rle->bpp;
            rle->state = RLE_STATE_SINGLE;
        }
    }

    return ret;
}
#endif /*LV_USE_FONT_COMPRESSED*/

/** Code Comparator.
 *
 *  Compares the value of both input arguments.
 *
 *  @param[in]  pRef        Pointer to the reference.
 *  @param[in]  pElement    Pointer to the element to compare.
 *
 *  @return Result of comparison.
 *  @retval < 0   Reference is less than element.
 *  @retval = 0   Reference is equal to element.
 *  @retval > 0   Reference is greater than element.
 *
 */
static int32_t unicode_list_compare(const void * ref, const void * element)
{
    return ((int32_t)(*(uint16_t *)ref)) - ((int32_t)(*(uint16_t *)element));
}

