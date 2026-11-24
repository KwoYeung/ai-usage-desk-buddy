/*******************************************************************************
 * Size: 20 px
 * Bpp: 4
 * Opts: --no-compress --no-prefilter --no-kerning --bpp 4 --size 20 --font managed_components/lvgl__lvgl/scripts/built_in_font/SourceHanSansSC-Normal.otf --symbols ¥$ --format lvgl --lv-include lvgl.h --lv-font-name buddy_font_currency_20 -o main/buddy_font_currency_20.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef BUDDY_FONT_CURRENCY_20
#define BUDDY_FONT_CURRENCY_20 1
#endif

#if BUDDY_FONT_CURRENCY_20

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0024 "$" */
    0x0, 0x0, 0xf3, 0x0, 0x0, 0x0, 0xf, 0x30,
    0x0, 0x0, 0x3a, 0xfb, 0x60, 0x0, 0x5f, 0xd9,
    0xbf, 0xc1, 0xe, 0xc0, 0x0, 0x28, 0x2, 0xf6,
    0x0, 0x0, 0x0, 0x1f, 0x80, 0x0, 0x0, 0x0,
    0xcf, 0x40, 0x0, 0x0, 0x1, 0xcf, 0xb2, 0x0,
    0x0, 0x0, 0x6f, 0xf7, 0x0, 0x0, 0x0, 0x1a,
    0xfb, 0x0, 0x0, 0x0, 0x8, 0xf5, 0x0, 0x0,
    0x0, 0x1f, 0x80, 0x0, 0x0, 0x0, 0xf8, 0x5a,
    0x10, 0x0, 0x6f, 0x44, 0xef, 0xb8, 0xbf, 0xa0,
    0x1, 0x6a, 0xfb, 0x60, 0x0, 0x0, 0xf, 0x30,
    0x0, 0x0, 0x0, 0xf3, 0x0, 0x0,

    /* U+00A5 "¥" */
    0x3f, 0x70, 0x0, 0x0, 0x7f, 0x30, 0xce, 0x0,
    0x0, 0xd, 0xb0, 0x4, 0xf5, 0x0, 0x4, 0xf4,
    0x0, 0xd, 0xb0, 0x0, 0xbc, 0x0, 0x0, 0x6f,
    0x20, 0x2f, 0x50, 0x0, 0x0, 0xe8, 0x8, 0xd0,
    0x0, 0x0, 0x7, 0xe0, 0xe6, 0x0, 0x0, 0x0,
    0xf, 0xbe, 0x0, 0x0, 0xe, 0xff, 0xff, 0xff,
    0xfe, 0x0, 0x0, 0x4, 0xf4, 0x0, 0x0, 0xe,
    0xff, 0xff, 0xff, 0xfe, 0x0, 0x0, 0x4, 0xf4,
    0x0, 0x0, 0x0, 0x0, 0x4f, 0x40, 0x0, 0x0,
    0x0, 0x4, 0xf4, 0x0, 0x0, 0x0, 0x0, 0x4f,
    0x40, 0x0, 0x0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 176, .box_w = 9, .box_h = 19, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 86, .adv_w = 176, .box_w = 11, .box_h = 15, .ofs_x = 0, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x81
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 36, .range_length = 130, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 2, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
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
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t buddy_font_currency_20 = {
#else
lv_font_t buddy_font_currency_20 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 19,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if BUDDY_FONT_CURRENCY_20*/

