// AirESP32ace
// Created by VM, August 2026

/*******************************************************************************
 * Size: 34 px
 * Bpp: 4
 * Opts: --size 34 --bpp 4 --format lvgl --font nasalization-rg.ttf --symbols T+- --no-kerning --lv-include lvgl.h --lv-font-name nasalization_prefix_34 -o firmware/airspace-instrument/main/nasalization_prefix_34.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef NASALIZATION_PREFIX_34
#define NASALIZATION_PREFIX_34 1
#endif

#if NASALIZATION_PREFIX_34

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+002B "+" */
    0x0, 0xfc, 0x6a, 0x80, 0x1f, 0xfc, 0x4f, 0xa8,
    0x0, 0xff, 0xff, 0x80, 0x7f, 0xf8, 0xd1, 0xdf,
    0xe1, 0x2, 0x77, 0xf9, 0x7a, 0x23, 0xe2, 0x1,
    0x88, 0xfb, 0xc0, 0x3f, 0xf8, 0xf5, 0xff, 0xf1,
    0x81, 0xff, 0xfd, 0x40, 0x1f, 0xff, 0xf0, 0xf,
    0xff, 0x70,

    /* U+002D "-" */
    0x7, 0x7f, 0xfe, 0x0, 0x2, 0x23, 0xff, 0x80,
    0x40, 0x1f, 0xfd, 0x0,

    /* U+0054 "T" */
    0xf, 0xff, 0xfe, 0x2b, 0x0, 0x7f, 0xfb, 0xbf,
    0xfd, 0x40, 0x12, 0x7f, 0xf9, 0x80, 0x3f, 0xff,
    0xe0, 0x1f, 0xff, 0xf0, 0xf, 0xff, 0xf8, 0x7,
    0xff, 0xfc, 0x3, 0xff, 0xbc
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 337, .box_w = 19, .box_h = 19, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 42, .adv_w = 226, .box_w = 14, .box_h = 4, .ofs_x = 0, .ofs_y = 9},
    {.bitmap_index = 54, .adv_w = 311, .box_w = 19, .box_h = 23, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x2, 0x29
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 43, .range_length = 42, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 3, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 1,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t nasalization_prefix_34 = {
#else
lv_font_t nasalization_prefix_34 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 23,          /*The maximum line height required by the font*/
    .base_line = 0,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if NASALIZATION_PREFIX_34*/
