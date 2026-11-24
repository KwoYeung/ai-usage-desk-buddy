#include "buddy_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "buddy_i4.h"
#include "buddy_usage.h"
#include "buddy_deepseek.h"
#include "buddy_font_zh.h"
#include "buddy_sprite.h"
#include "buddy_text_layout.h"
#include "lvgl.h"

#define UI_W 240
#define UI_H 320
#define COL_BG lv_color_hex(0x000000)
#define COL_INK lv_color_hex(0xFFFFFF)
#define COL_DIM lv_color_hex(0xADADAD)
#define COL_LINE lv_color_hex(0x2A373B)
#define COL_ORANGE lv_color_hex(0xE17B52)
#define COL_RED lv_color_hex(0xEF4B38)
#define COL_GREEN lv_color_hex(0x79DDAA)
#define COL_YELLOW lv_color_hex(0xEAC371)
#define COL_BLUE lv_color_hex(0x72A7D8)

static lv_obj_t *s_screen;
static lv_obj_t *s_canvas;
static LV_ATTRIBUTE_MEM_ALIGN uint8_t s_canvas_buffer[
    LV_DRAW_BUF_SIZE(UI_W, UI_H, LV_COLOR_FORMAT_I4)];
static buddy_ui_snapshot_t s_snapshot;
static bool s_have_snapshot;
static uint32_t s_tick;
static uint64_t s_elapsed_ms;
static int s_scroll;
static int s_clip_top;
static int s_clip_end = UI_H;
static buddy_i4_surface_t s_surface;

#define I4_PALETTE_BYTES (16U * sizeof(lv_color32_t))

#define COL_TITLE lv_color_hex(0x9FD5FF)
#define COL_TELEMETRY lv_color_hex(0x88A6A7)
#define COL_WEEK_TOKEN lv_color_hex(0x59C8FF)
#define COL_MONTH_TOKEN lv_color_hex(0xC79AFF)
#define COL_CARD lv_color_hex(0x10191C)
#define COL_BADGE lv_color_hex(0x14271F)

static const uint32_t s_palette_rgb[] = {0x000000, 0xFFFFFF, 0xADADAD, 0x2A373B,
    0xE17B52, 0xEF4B38, 0x79DDAA, 0xEAC371, 0x72A7D8, 0x9FD5FF,
    0x88A6A7, 0x59C8FF, 0xC79AFF, 0x10191C, 0x14271F, 0x151719};

static uint8_t color_index(lv_color_t color)
{
    uint32_t rgb = lv_color_to_int(color);
    uint32_t best_distance = UINT32_MAX;
    uint8_t best = 0;
    uint8_t i;
    for (i = 0; i < sizeof(s_palette_rgb) / sizeof(s_palette_rgb[0]); ++i) {
        int dr = (int)((rgb >> 16) & 0xffU) - (int)((s_palette_rgb[i] >> 16) & 0xffU);
        int dg = (int)((rgb >> 8) & 0xffU) - (int)((s_palette_rgb[i] >> 8) & 0xffU);
        int db = (int)(rgb & 0xffU) - (int)(s_palette_rgb[i] & 0xffU);
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

static void pixel(int x, int y, uint8_t index)
{
    if ((unsigned)x >= UI_W || y < s_clip_top || y >= s_clip_end) return;
    buddy_i4_set_pixel(s_canvas_buffer + I4_PALETTE_BYTES, UI_W,
                       (uint16_t)x, (uint16_t)y, index);
}

static size_t utf8_decode(const char *text, size_t remaining, uint32_t *codepoint)
{
    const uint8_t *s = (const uint8_t *)text;
    if (remaining == 0U) return 0U;
    if (s[0] < 0x80U) {
        *codepoint = s[0];
        return 1U;
    }
    if (remaining >= 2U && (s[0] & 0xe0U) == 0xc0U && (s[1] & 0xc0U) == 0x80U) {
        *codepoint = ((uint32_t)(s[0] & 0x1fU) << 6) | (uint32_t)(s[1] & 0x3fU);
        return 2U;
    }
    if (remaining >= 3U && (s[0] & 0xf0U) == 0xe0U &&
        (s[1] & 0xc0U) == 0x80U && (s[2] & 0xc0U) == 0x80U) {
        *codepoint = ((uint32_t)(s[0] & 0x0fU) << 12) |
                     ((uint32_t)(s[1] & 0x3fU) << 6) | (uint32_t)(s[2] & 0x3fU);
        return 3U;
    }
    *codepoint = '?';
    return 1U;
}

static void glyph(int x, int y, uint8_t index, const lv_font_t *font, uint32_t codepoint)
{
    lv_font_glyph_dsc_t dsc;
    const uint8_t *bitmap;
    unsigned row;
    unsigned col;
    if (!lv_font_get_glyph_dsc(font, &dsc, codepoint, 0) || dsc.box_w == 0 || dsc.box_h == 0) return;
    dsc.req_raw_bitmap = 1;
    bitmap = dsc.resolved_font->get_glyph_bitmap(&dsc, NULL);
    if (!bitmap || (dsc.format != LV_FONT_GLYPH_FORMAT_A1 &&
                    dsc.format != LV_FONT_GLYPH_FORMAT_A4)) return;
    y += font->line_height - font->base_line - dsc.box_h - dsc.ofs_y;
    x += dsc.ofs_x;
    for (row = 0; row < dsc.box_h; ++row) {
        for (col = 0; col < dsc.box_w; ++col) {
            uint32_t sample = row * dsc.box_w + col;
            bool visible = dsc.format == LV_FONT_GLYPH_FORMAT_A1
                               ? (bitmap[sample >> 3] & (0x80U >> (sample & 7U))) != 0
                               : (((sample & 1U) == 0U ? bitmap[sample >> 1] >> 4
                                                       : bitmap[sample >> 1] & 0x0fU) >= 4U);
            if (visible) pixel(x + col, y + row, index);
        }
    }
}

static void text_limited(lv_layer_t *layer, int x, int y, int width, lv_color_t color,
                         const char *value, bool large, lv_text_align_t align,
                         unsigned max_lines)
{
    const lv_font_t *font = large ? &buddy_font_zh_16 : &buddy_font_zh_14;
    int spacing = large ? 1 : 0;
    int line_step = font->line_height + (large ? 2 : 1);
    uint8_t index = color_index(color);
    const char *cursor = value;
    unsigned lines = 0;
    (void)layer;
    while (*cursor && y < UI_H && lines < max_lines) {
        const char *end = strchr(cursor, '\n');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        size_t fit = 0;
        size_t offset = 0;
        int measured = 0;
        while (offset < length) {
            lv_font_glyph_dsc_t dsc;
            uint32_t codepoint;
            size_t consumed = utf8_decode(cursor + offset, length - offset, &codepoint);
            int advance = lv_font_get_glyph_dsc(font, &dsc, codepoint, 0)
                              ? dsc.adv_w + spacing : 0;
            if (fit > 0U && measured + advance > width) break;
            measured += advance;
            offset += consumed;
            fit = offset;
        }
        if (measured > 0) measured -= spacing;
        int pen = x;
        if (align == LV_TEXT_ALIGN_CENTER) pen += (width - measured) / 2;
        else if (align == LV_TEXT_ALIGN_RIGHT) pen += width - measured;
        offset = 0;
        while (offset < fit) {
            lv_font_glyph_dsc_t dsc;
            uint32_t codepoint;
            size_t consumed = utf8_decode(cursor + offset, fit - offset, &codepoint);
            glyph(pen, y, index, font, codepoint);
            if (lv_font_get_glyph_dsc(font, &dsc, codepoint, 0)) pen += dsc.adv_w + spacing;
            offset += consumed;
        }
        y += line_step;
        lines++;
        if (fit < length) cursor += fit;
        else cursor = end ? end + 1 : cursor + length;
    }
}

static void text(lv_layer_t *layer, int x, int y, int width, lv_color_t color,
                 const char *value, bool large, lv_text_align_t align)
{
    text_limited(layer, x, y, width, color, value, large, align, UINT32_MAX);
}

static void wrapped_text(lv_layer_t *layer, int x, int y, int width, lv_color_t color,
                         const char *value, unsigned max_lines)
{
    text_limited(layer, x, y, width, color, value, false, LV_TEXT_ALIGN_LEFT, max_lines);
}

static void box(lv_layer_t *layer, int x, int y, int w, int h, lv_color_t fill,
                lv_color_t border, int border_width, int radius)
{
    uint8_t fill_index = color_index(fill);
    uint8_t border_index = color_index(border);
    int px;
    int py;
    (void)layer;
    (void)radius;
    int first = y < s_clip_top ? s_clip_top - y : 0;
    int end = y + h > s_clip_end ? s_clip_end - y : h;
    for (py = first; py < end; ++py) {
        for (px = 0; px < w; ++px) {
            bool edge = px < border_width || py < border_width ||
                        px >= w - border_width || py >= h - border_width;
            pixel(x + px, y + py, edge ? border_index : fill_index);
        }
    }
}

static void rule(lv_layer_t *layer, int x, int y, int w, lv_color_t color)
{
    box(layer, x, y, w, 1, color, color, 0, 0);
}

static uint8_t art_state(buddy_character_t state)
{
    switch (state) {
    case BUDDY_CHARACTER_SLEEP: return 0;
    case BUDDY_CHARACTER_BUSY: return 2;
    case BUDDY_CHARACTER_ATTENTION:
    case BUDDY_CHARACTER_PAIRING:
    case BUDDY_CHARACTER_CONFIRMATION: return 3;
    case BUDDY_CHARACTER_CELEBRATE: return 4;
    case BUDDY_CHARACTER_DIZZY: return 5;
    case BUDDY_CHARACTER_HEART: return 6;
    default: return 1;
    }
}

static void draw_buddy(lv_layer_t *layer, const buddy_ui_snapshot_t *s, bool peek)
{
    buddy_i4_clip_t clip = {.x = 0, .y = BUDDY_UI_STAGE_Y,
                            .w = UI_W, .h = BUDDY_UI_STAGE_H};
    buddy_sprite_bounds_t bounds;
    int x = 88;
    int y = peek ? 72 : 65;
    (void)layer;
    if (buddy_sprite_bounds(s->species, art_state(s->character), s_tick, &bounds)) {
        x = (UI_W - bounds.w) / 2 - bounds.x;
    }
    buddy_sprite_render(&s_surface, &clip, s->species, art_state(s->character),
                        s_tick, x, y);
}

static void draw_status_bar(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char left[32];
    char center[32];
    char battery[32];
    uint64_t age_ms = s_elapsed_ms >= s->time_received_ms ? s_elapsed_ms - s->time_received_ms : 0;
    time_t epoch = (time_t)(s->epoch_seconds + s->timezone_offset_seconds + age_ms / 1000U);
    struct tm tm_value;

    snprintf(left, sizeof(left), "%s", s->lan_connected ? "LAN" :
             (s->ble_connected ? "BLE" : "未连接"));
    if (s->epoch_seconds > 0 && gmtime_r(&epoch, &tm_value) != NULL) {
        snprintf(center, sizeof(center), "%02d:%02d", tm_value.tm_hour, tm_value.tm_min);
    } else {
        snprintf(center, sizeof(center), "%s", s->heartbeat_stale ? "休眠" : "在线");
    }
    if (s->battery_available) {
        snprintf(battery, sizeof(battery), "%u%%", (unsigned)s->battery_percent);
    } else {
        snprintf(battery, sizeof(battery), "--");
    }
    box(layer, 14, 6, 212, 21, COL_CARD, COL_CARD, 0, 0);
    text(layer, 21, 9, 72, COL_TELEMETRY, left, false, LV_TEXT_ALIGN_LEFT);
    text(layer, 88, 9, 64, COL_TELEMETRY, center, false, LV_TEXT_ALIGN_CENTER);
    text(layer, 147, 9, 72, COL_TELEMETRY, battery, false, LV_TEXT_ALIGN_RIGHT);
}

static void draw_companion(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char caption[176];
    text(layer, 10, 35, 220, COL_ORANGE, buddy_sprite_name(s->species), false,
         LV_TEXT_ALIGN_CENTER);
    draw_buddy(layer, s, false);
    rule(layer, 18, BUDDY_UI_INFO_Y, 204, COL_LINE);
    snprintf(caption, sizeof(caption), "%s", s->message[0] ? s->message :
             ((s->ble_connected || s->lan_connected) ? "助手已就绪" : "请启动电脑端桥接程序进行配对"));
    wrapped_text(layer, 18, 174, 204, s->heartbeat_stale ? COL_DIM : COL_INK,
                 caption, 8);
    text(layer, 8, 300, 224, COL_DIM, BUDDY_ACTION_HOME, false, LV_TEXT_ALIGN_CENTER);
}

static void usage_reset_text(char *destination, size_t size, uint64_t resets_at,
                             const buddy_ui_snapshot_t *s)
{
    uint64_t now = s->epoch_seconds > 0
                       ? (uint64_t)s->epoch_seconds +
                             (s_elapsed_ms >= s->time_received_ms
                                  ? (s_elapsed_ms - s->time_received_ms) / 1000U
                                  : 0U)
                       : 0U;
    uint64_t remaining = resets_at > now ? resets_at - now : 0U;

    if (resets_at == 0U || now == 0U) {
        snprintf(destination, size, "重置时间：--");
    } else if (remaining >= 86400U) {
        snprintf(destination, size, "距重置 %llu 天 %llu 小时",
                 (unsigned long long)(remaining / 86400U),
                 (unsigned long long)((remaining % 86400U) / 3600U));
    } else {
        snprintf(destination, size, "距重置 %llu 小时 %02llu 分",
                 (unsigned long long)(remaining / 3600U),
                 (unsigned long long)((remaining % 3600U) / 60U));
    }
}

static void format_tokens(char *out, size_t size, uint64_t tokens)
{
    if (tokens == UINT64_MAX) {
        snprintf(out, size, "--");
    } else if (tokens >= 999950000ULL) {
        if (tokens < 9995000000ULL) {
            uint64_t hundredths = (tokens + 5000000ULL) / 10000000ULL;
            snprintf(out, size, "%llu.%02lluB", (unsigned long long)(hundredths / 100),
                     (unsigned long long)(hundredths % 100));
        } else if (tokens < 99950000000ULL) {
            uint64_t tenths = (tokens + 50000000ULL) / 100000000ULL;
            snprintf(out, size, "%llu.%lluB", (unsigned long long)(tenths / 10),
                     (unsigned long long)(tenths % 10));
        } else {
            uint64_t billions = tokens / 1000000000ULL + (tokens % 1000000000ULL >= 500000000ULL);
            snprintf(out, size, "%lluB", (unsigned long long)billions);
        }
    } else if (tokens >= 1000000ULL) {
        uint64_t tenths = (tokens + 50000ULL) / 100000ULL;
        snprintf(out, size, "%llu.%lluM", (unsigned long long)(tenths / 10),
                 (unsigned long long)(tenths % 10));
    } else {
        snprintf(out, size, "%llu", (unsigned long long)tokens);
    }
}

static int ascii_width(const lv_font_t *font, const char *value)
{
    int width = 0;
    for (; *value; ++value) {
        lv_font_glyph_dsc_t dsc;
        if (lv_font_get_glyph_dsc(font, &dsc, (uint8_t)*value, 0)) width += dsc.adv_w;
    }
    return width;
}

static void display_number_font(int x, int y, int width, lv_color_t color,
                                const char *value, bool right, const lv_font_t *font)
{
    int measured = ascii_width(font, value);
    if (measured > width) {
        font = &lv_font_montserrat_20;
        measured = ascii_width(font, value);
    }
    if (measured > width) {
        font = &lv_font_montserrat_14;
        measured = ascii_width(font, value);
    }
    if (right) x += width - measured;
    uint8_t index = color_index(color);
    for (; *value; ++value) {
        lv_font_glyph_dsc_t dsc;
        glyph(x, y, index, font, (uint8_t)*value);
        if (lv_font_get_glyph_dsc(font, &dsc, (uint8_t)*value, 0)) x += dsc.adv_w;
    }
}

static void display_number(int x, int y, int width, lv_color_t color,
                           const char *value, bool right)
{
    display_number_font(x, y, width, color, value, right, &lv_font_montserrat_20);
}

static void draw_home_tokens(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    bool live = (s->ble_connected || s->lan_connected) && !s->heartbeat_stale;
    char weekly[24], monthly[24], activity[40];
    format_tokens(weekly, sizeof(weekly), live ? s->tokens : UINT64_MAX);
    format_tokens(monthly, sizeof(monthly), live ? s->tokens_today : UINT64_MAX);
    rule(layer, 14, 198, 212, COL_LINE);
    text(layer, 14, 205, 212, COL_TELEMETRY, "TOKEN 累计", false, LV_TEXT_ALIGN_LEFT);
    box(layer, 14, 225, 103, 58, COL_CARD, COL_CARD, 0, 0);
    box(layer, 123, 225, 103, 58, COL_CARD, COL_CARD, 0, 0);
    display_number(20, 231, 91, COL_WEEK_TOKEN, weekly, false);
    display_number(129, 231, 91, COL_MONTH_TOKEN, monthly, true);
    text(layer, 20, 261, 91, COL_DIM, "7 天周期", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 129, 261, 91, COL_DIM, "近 30 天", false, LV_TEXT_ALIGN_RIGHT);
    snprintf(activity, sizeof(activity), "%u", s->running);
    text(layer, 14, 296, 100, COL_TELEMETRY, "运行任务", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 126, 296, 100, COL_TITLE, activity, false, LV_TEXT_ALIGN_RIGHT);
}

static void draw_home(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char value[64];
    char reset[32];
    buddy_usage_window_t windows[2];
    size_t count = buddy_usage_windows(&s->codex_usage, windows);
    const char *status = "就绪";
    lv_color_t status_color = COL_GREEN;
    if (!(s->ble_connected || s->lan_connected) || s->heartbeat_stale) {
        status = "等待同步";
        status_color = COL_DIM;
    } else if (s->character == BUDDY_CHARACTER_CELEBRATE) {
        status = "任务完成";
        status_color = COL_TITLE;
    } else if (s->running > 0U) {
        status = "工作中";
        status_color = COL_GREEN;
    }
    const lv_font_t *title_font = &lv_font_montserrat_26;
    int title_x = 14;
    uint8_t title_color = color_index(COL_TITLE);
    for (const char *letter = "Codex"; *letter; ++letter) {
        lv_font_glyph_dsc_t dsc;
        if (!lv_font_get_glyph_dsc(title_font, &dsc, (uint8_t)*letter, 0)) continue;
        glyph(title_x, 32, title_color, title_font, (uint8_t)*letter);
        glyph(title_x + 1, 32, title_color, title_font, (uint8_t)*letter);
        title_x += dsc.adv_w + 1;
    }
    box(layer, 146, 39, 80, 22, COL_BADGE, COL_BADGE, 0, 0);
    text(layer, 146, 42, 80, status_color, status, false, LV_TEXT_ALIGN_CENTER);
    if (count == 0U) {
        wrapped_text(layer, 22, 113, 196, COL_INK,
                     "暂无用量数据。\n请保持桥接程序运行。", 5);
    }
    for (size_t index = 0; index < count; ++index) {
        const buddy_usage_window_t *window = &windows[index];
        int top = 76 + (int)index * 61;
        lv_color_t color = strcmp(window->label, "7 天") == 0 ? COL_YELLOW : COL_GREEN;
        snprintf(value, sizeof(value), "%s剩余", window->label);
        text(layer, 14, top + 3, 148, COL_INK, value, false, LV_TEXT_ALIGN_LEFT);
        snprintf(value, sizeof(value), "%u%%", window->remaining);
        display_number(154, top - 1, 72, color, value, true);
        box(layer, 14, top + 27, 212, 3, COL_LINE, COL_LINE, 0, 0);
        box(layer, 14, top + 27, (int)(212U * window->remaining / 100U), 3, color, color, 0, 0);
        usage_reset_text(reset, sizeof(reset), window->resets_at, s);
        text(layer, 14, top + 34, 212, COL_DIM, reset, false, LV_TEXT_ALIGN_LEFT);
    }
    draw_home_tokens(layer, s);
}

static void brand_title(lv_layer_t *layer, const char *value, int x, int y)
{
    const lv_font_t *font = &lv_font_montserrat_26;
    uint8_t color = color_index(COL_TITLE);

    (void)layer;
    for (const char *letter = value; *letter; ++letter) {
        lv_font_glyph_dsc_t dsc;
        if (!lv_font_get_glyph_dsc(font, &dsc, (uint8_t)*letter, 0)) continue;
        glyph(x, y, color, font, (uint8_t)*letter);
        glyph(x + 1, y, color, font, (uint8_t)*letter);
        x += dsc.adv_w + 1;
    }
}

static int font_baseline_top(const lv_font_t *font, int baseline)
{
    return baseline - (font->line_height - font->base_line);
}

static int glyph_advance(const lv_font_t *font, uint32_t codepoint)
{
    lv_font_glyph_dsc_t dsc;
    return lv_font_get_glyph_dsc(font, &dsc, codepoint, 0) ? dsc.adv_w : 0;
}

static void text_at_baseline(int x, int baseline, lv_color_t color,
                             const lv_font_t *font, const char *value)
{
    uint8_t index = color_index(color);
    int top = font_baseline_top(font, baseline);
    for (; *value; ++value) {
        lv_font_glyph_dsc_t dsc;
        glyph(x, top, index, font, (uint8_t)*value);
        if (lv_font_get_glyph_dsc(font, &dsc, (uint8_t)*value, 0)) x += dsc.adv_w;
    }
}

static void draw_balance_row(lv_layer_t *layer, const buddy_deepseek_t *deepseek)
{
    static const lv_font_t *const number_fonts[] = {
        &lv_font_montserrat_40,
        &lv_font_montserrat_32,
        &lv_font_montserrat_26,
        &lv_font_montserrat_20,
    };
    const lv_font_t *symbol_font = &buddy_font_currency_20;
    const lv_font_t *unit_font = &lv_font_montserrat_20;
    char amount[32];
    uint32_t symbol = strcmp(deepseek->currency, "USD") == 0 ? '$' : 0xA5;
    buddy_deepseek_amount(amount, sizeof(amount), deepseek->total_cents);

    int symbol_width = glyph_advance(symbol_font, symbol);
    int unit_width = ascii_width(unit_font, deepseek->currency);
    int number_left = 26 + symbol_width + 8;
    int unit_x = 214 - unit_width;
    int number_right = unit_x - 8;
    int number_width = 0;
    const lv_font_t *chosen = number_fonts[0];
    for (size_t i = 0; i < sizeof(number_fonts) / sizeof(number_fonts[0]); ++i) {
        number_width = ascii_width(number_fonts[i], amount);
        chosen = number_fonts[i];
        if (number_width <= number_right - number_left) break;
    }
    int number_x = number_left + (number_right - number_left - number_width) / 2;
    if (number_x < number_left) number_x = number_left;
    const int baseline = 148;
    glyph(26, font_baseline_top(symbol_font, baseline), color_index(COL_TELEMETRY),
          symbol_font, symbol);
    text_at_baseline(number_x, baseline, COL_TITLE, chosen, amount);
    text_at_baseline(unit_x, baseline, COL_TELEMETRY, unit_font, deepseek->currency);
    (void)layer;
}

static void deepseek_notice(lv_layer_t *layer, const char *title, const char *body)
{
    box(layer, 14, 76, 212, 112, COL_CARD, COL_CARD, 0, 0);
    text(layer, 14, 104, 212, COL_TITLE, title, true, LV_TEXT_ALIGN_CENTER);
    text(layer, 14, 134, 212, COL_DIM, body, false, LV_TEXT_ALIGN_CENTER);
}

static void draw_deepseek_tokens(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    bool available = s->deepseek.usage.present && s->deepseek.usage.available;
    char weekly[24], monthly[24];
    format_tokens(weekly, sizeof(weekly),
                  available ? s->deepseek.usage.tokens_7d : UINT64_MAX);
    format_tokens(monthly, sizeof(monthly),
                  available ? s->deepseek.usage.tokens_30d : UINT64_MAX);
    rule(layer, 14, 198, 212, COL_LINE);
    text(layer, 14, 205, 212, COL_TELEMETRY, "TOKEN 累计", false, LV_TEXT_ALIGN_LEFT);
    box(layer, 14, 225, 103, 58, COL_CARD, COL_CARD, 0, 0);
    box(layer, 123, 225, 103, 58, COL_CARD, COL_CARD, 0, 0);
    display_number(20, 231, 91, COL_WEEK_TOKEN, weekly, false);
    display_number(129, 231, 91, COL_MONTH_TOKEN, monthly, true);
    text(layer, 20, 261, 91, COL_DIM, "近 7 天", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 129, 261, 91, COL_DIM, "近 30 天", false, LV_TEXT_ALIGN_RIGHT);
}

static void draw_deepseek_daily(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    const buddy_deepseek_usage_t *usage = &s->deepseek.usage;
    bool daily = usage->present && usage->available && usage->daily_present;
    char today[24];
    char requests[24];
    format_tokens(today, sizeof(today), daily ? usage->tokens_today : UINT64_MAX);
    if (daily) {
        snprintf(requests, sizeof(requests), "%llu",
                 (unsigned long long)usage->requests_today);
    } else {
        snprintf(requests, sizeof(requests), "--");
    }
    text(layer, 14, 296, 36, COL_TELEMETRY, "今天", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 50, 296, 80, daily ? COL_INK : COL_DIM, today, false, LV_TEXT_ALIGN_LEFT);
    text(layer, 136, 296, 36, COL_TELEMETRY, "请求", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 172, 296, 54, daily ? COL_INK : COL_DIM, requests, false, LV_TEXT_ALIGN_RIGHT);
}

static void draw_deepseek(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    const buddy_deepseek_t *deepseek = &s->deepseek;
    const buddy_deepseek_usage_t *usage = &deepseek->usage;
    char age[40];
    bool live = (s->ble_connected || s->lan_connected) && !s->heartbeat_stale;
    bool balance_available = deepseek->configured && deepseek->available;
    bool usage_available = usage->present && usage->available;

    brand_title(layer, "DeepSeek", 14, 32);
    if (balance_available) {
        box(layer, 14, 76, 212, 112, COL_CARD, COL_CARD, 0, 0);
        text(layer, 26, 86, 188, COL_INK, "可用余额", false, LV_TEXT_ALIGN_LEFT);
        draw_balance_row(layer, deepseek);
    } else if (deepseek->configured) {
        deepseek_notice(layer, "等待同步", live ? "在电脑端检查余额设置" : "保持电脑端程序运行");
    } else {
        deepseek_notice(layer, "未配置 API Key", "请在电脑端设置");
    }

    if (s->dsh.present && s->dsh.available) {
        bool working = s->dsh.working;
        box(layer, 150, 80, 64, 20, COL_BADGE, COL_BADGE, 0, 0);
        text(layer, 150, 84, 64, COL_GREEN,
             working ? "工作中" : "就绪", false, LV_TEXT_ALIGN_CENTER);
    }
    uint64_t updated = usage_available ? usage->updated_at : deepseek->updated_at;
    buddy_deepseek_age(age, sizeof(age), updated, s->epoch_seconds,
                       s->time_received_ms, s_elapsed_ms);
    const char *status;
    lv_color_t status_color;
    if (!live) {
        status = "等待同步";
        status_color = COL_DIM;
    } else if (deepseek->stale) {
        status = "数据未更新";
        status_color = COL_ORANGE;
    } else if (usage_available && balance_available) {
        status = "已同步";
        status_color = COL_GREEN;
    } else if (usage_available || balance_available) {
        status = "部分同步";
        status_color = COL_ORANGE;
    } else {
        status = "等待同步";
        status_color = COL_DIM;
    }
    /* Meta row lives inside the balance card so the card is not left half empty. */
    rule(layer, 26, 156, 188, COL_LINE);
    text(layer, 26, 164, 118, COL_TELEMETRY, age, false, LV_TEXT_ALIGN_LEFT);
    text(layer, 144, 164, 70, status_color, status, false, LV_TEXT_ALIGN_RIGHT);

    draw_deepseek_tokens(layer, s);
    draw_deepseek_daily(layer, s);
}

static void draw_info(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    if (!s->lan_setup) {
        text(layer, 14, 38, 212, COL_TITLE, "帮助", true, LV_TEXT_ALIGN_LEFT);
        rule(layer, 14, 66, 212, COL_LINE);
        wrapped_text(layer, 14, 80, 212, COL_INK,
            "首页上键：切换页面\n首页确认键：打开菜单\n菜单上下键：选择\n确认键：进入或执行\n选择返回：回到上一级", 8);
        text(layer, 14, 224, 212, COL_TELEMETRY, "KwoYeung", false, LV_TEXT_ALIGN_LEFT);
        text(layer, 14, 296, 212, COL_DIM, "确认键返回", false, LV_TEXT_ALIGN_CENTER);
        return;
    }
    static const char *const titles[] = {"关于", "按键说明", "用量状态", "设备信息", "蓝牙", "致谢"};
    char body[512];
    char page[16];
    unsigned p = s->info_page < 6 ? s->info_page : 0;
    text(layer, 14, 38, 180, COL_ORANGE, p == 4 ? "无线网络" : titles[p], true, LV_TEXT_ALIGN_LEFT);
    snprintf(page, sizeof(page), "%u / 6", p + 1);
    text(layer, 174, 43, 52, COL_DIM, page, false, LV_TEXT_ALIGN_RIGHT);
    rule(layer, 14, 66, 212, COL_LINE);
    switch (p) {
    case 0: snprintf(body, sizeof(body), "你的桌面伙伴。\n\n显示用量余量和任务完成提醒。"); break;
    case 1: snprintf(body, sizeof(body), "上键：返回首页\n下键：翻页或拒绝\n确认键：允许或更改\n首页确认键：打开菜单"); break;
    case 2: {
        buddy_usage_window_t windows[2];
        size_t count = buddy_usage_windows(&s->codex_usage, windows);
        size_t used = (size_t)snprintf(body, sizeof(body),
            "任务：%u\n进行中：%u\n", s->total, s->running);
        if (count == 0U) {
            snprintf(body + used, sizeof(body) - used, "\n暂无用量数据。");
        }
        for (size_t i = 0; i < count && used < sizeof(body); ++i) {
            int written = snprintf(body + used, sizeof(body) - used,
                "\n%s 剩余：%u%%", windows[i].label, windows[i].remaining);
            if (written < 0) break;
            used += (size_t)written;
        }
        break;
    }
    case 3: snprintf(body, sizeof(body), "名称\n%s\n\n所有者\n%s\n\n屏幕：240 × 320", s->name[0] ? s->name : "Codex 助手", s->owner[0] ? s->owner : "-"); break;
    case 4: if (s->lan_setup) {
        snprintf(body, sizeof(body), "连接热点：\nPassport Setup\n\n无需密码\n\nhttp://192.168.4.1\n\n10 分钟后关闭\n确认键：退出配网");
        break;
    } else if (s->lan_mode) {
        snprintf(body, sizeof(body), "无线网络\n\nIP：%s\n端口：8765\n\n%s\n\n确认键：无线配网",
                 s->lan_ip[0] ? s->lan_ip : "连接中…",
                 s->lan_connected ? "已加密连接" : "等待桥接连接");
        break;
    }
    snprintf(body, sizeof(body), "当前：蓝牙\n\n确认键：无线配网\n\n手机连接设备热点\n设置 2.4 GHz 无线网络。\n\n也可通过 USB 配网。"); break;
    default: snprintf(body, sizeof(body), "AI 用量桌伴\n\nCodex 与 DSH 用量\nDeepSeek 余额\n\n自动换页与完成提醒"); break;
    }
    wrapped_text(layer, 16, 82 - s_scroll, 208, COL_INK, body, 18);
    text(layer, 8, 300, 224, COL_DIM, BUDDY_ACTION_INFO, false, LV_TEXT_ALIGN_CENTER);
}

static void draw_list(lv_layer_t *layer, const char *title, const char *const *items,
                      unsigned count, unsigned selected, const buddy_ui_snapshot_t *s)
{
    unsigned first = 0;
    unsigned i;
    text(layer, 14, 34, 212, COL_TITLE, title, true, LV_TEXT_ALIGN_LEFT);
    rule(layer, 14, 62, 212, COL_LINE);
    for (i = first; i < count; ++i) {
        int y = 76 + (int)(i - first) * 25;
        bool active = i == selected;
        char row[64];
        const char *suffix = "";
        char value[12];
        if (!s->reset_open && i == BUDDY_SETTINGS_BRIGHTNESS) { snprintf(value, sizeof(value), "%u/5", (unsigned)s->brightness_level + 1U); suffix = value; }
        else if (!s->reset_open && i == BUDDY_SETTINGS_VOLUME) {
            unsigned level = s->volume_level ? s->volume_level : BUDDY_DEFAULT_VOLUME_LEVEL;
            snprintf(value, sizeof(value), "%u/5", level);
            suffix = value;
        }
        else if (!s->reset_open && i == BUDDY_SETTINGS_TONE) {
            static const char *const tones[BUDDY_TONE_COUNT] = {"09", "010", "Charging"};
            suffix = tones[s->tone < BUDDY_TONE_COUNT ? s->tone : BUDDY_TONE_NOTIFICATION_09];
        }
        else if (!s->reset_open && i == BUDDY_SETTINGS_SOUND) suffix = s->sound_mode == BUDDY_SOUND_OFF ? "关闭" : (s->sound_mode == BUDDY_SOUND_ON ? "开启" : "自动");
        else if (!s->reset_open && i == BUDDY_SETTINGS_SLEEP) {
            static const char *const modes[BUDDY_SLEEP_COUNT] = {
                [BUDDY_SLEEP_1_MIN] = "1 分钟",
                [BUDDY_SLEEP_5_MIN] = "5 分钟",
                [BUDDY_SLEEP_10_MIN] = "10 分钟",
                [BUDDY_SLEEP_NEVER] = "永不",
                [BUDDY_SLEEP_10_SEC] = "10 秒",
                [BUDDY_SLEEP_30_SEC] = "30 秒",
            };
            suffix = modes[s->sleep_mode < BUDDY_SLEEP_COUNT ? s->sleep_mode : BUDDY_DEFAULT_SLEEP_MODE];
        }
        else if (!s->reset_open && i == BUDDY_SETTINGS_WIFI) suffix = s->lan_mode ? "开" : "关";
        else if (!s->reset_open && i == BUDDY_SETTINGS_BLE) suffix = s->ble_enabled ? "开" : "关";
        snprintf(row, sizeof(row), "%s", items[i]);
        if (active) box(layer, 12, y - 7, 216, 24, COL_TITLE, COL_TITLE, 0, 3);
        text(layer, 20, y, 142, active ? COL_BG : COL_INK, row, false, LV_TEXT_ALIGN_LEFT);
        text(layer, 147, y, 73, active ? COL_BG : COL_DIM, suffix, false, LV_TEXT_ALIGN_RIGHT);
    }
    text(layer, 8, 300, 224, COL_DIM, BUDDY_ACTION_SETTINGS, false, LV_TEXT_ALIGN_CENTER);
}

static void draw_settings(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    static const char *const settings[] = {"返回", "屏幕亮度", "自动睡眠", "提示音", "提示音量", "提示音色", "蓝牙", "Wi-Fi", "重置"};
    static const char *const reset[] = {"返回", "恢复出厂设置", "解除蓝牙配对"};
    _Static_assert(sizeof(settings) / sizeof(settings[0]) == BUDDY_SETTINGS_COUNT,
                   "Settings labels must match navigation");
    _Static_assert(sizeof(reset) / sizeof(reset[0]) == BUDDY_RESET_COUNT,
                   "Reset labels must match navigation");
    draw_list(layer, s->reset_open ? "重置" : "设置", s->reset_open ? reset : settings,
              s->reset_open ? BUDDY_RESET_COUNT : BUDDY_SETTINGS_COUNT,
              s->reset_open ? s->reset_selection : s->settings_selection, s);
}

static void panel(lv_layer_t *layer, int y, int h, lv_color_t accent, const char *title,
                  const char *body, const char *footer)
{
    box(layer, 10, y, 220, h, lv_color_hex(0x151719), accent, 2, 6);
    box(layer, 10, y, 220, 27, accent, accent, 0, 5);
    text(layer, 18, y + 8, 204, COL_BG, title, false, LV_TEXT_ALIGN_LEFT);
    wrapped_text(layer, 20, y + 42 - s_scroll, 200, COL_INK, body, 5);
    rule(layer, 20, y + h - 34, 200, COL_LINE);
    text(layer, 18, y + h - 23, 204, COL_DIM, footer, false, LV_TEXT_ALIGN_CENTER);
}

static void draw_overlay(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char body[448];
    int x;
    int y;
    if (s->lan_pair_id) {
        snprintf(body, sizeof(body), "Compare with PC:\n\n       %06lu\n\n%s",
                 (unsigned long)s->lan_pair_code,
                 s->lan_pair_approved ? "Confirmed here.\nConfirm on your PC." : "Only accept if both\ncodes match.");
        panel(layer, 55, 220, COL_BLUE, "LAN pairing", body,
              s->lan_pair_approved ? "Waiting for computer" : "OK: match   UP: cancel");
        return;
    }
    buddy_overlay_kind_t overlay = buddy_overlay_select(s->confirmation_pending,
                                                        s->passkey_visible,
                                                        s->prompt_id[0] != '\0',
                                                        s->menu_open);
    if (overlay != BUDDY_OVERLAY_NONE) {
        for (y = BUDDY_UI_STATUS_H; y < BUDDY_UI_ACTION_Y; ++y) {
            for (x = (y & 1); x < UI_W; x += 2) {
                uint8_t current = buddy_i4_get_pixel(s_surface.pixels, UI_W,
                                                     (uint16_t)x, (uint16_t)y);
                if (current != 0) buddy_i4_set_pixel(s_surface.pixels, UI_W,
                                                          (uint16_t)x, (uint16_t)y,
                                                          15);
            }
        }
    }
    if (overlay == BUDDY_OVERLAY_CONFIRMATION) {
        panel(layer, 62, 196, COL_RED, "确认操作",
              s->confirmation == BUDDY_CONFIRM_FACTORY_RESET ? "确定恢复出厂设置吗？\n\n全部设置和统计数据将被清除。" : "确定解除 Codex 桥接配对吗？\n\n已保存的蓝牙配对信息将被清除。",
              BUDDY_ACTION_CONFIRM);
    } else if (overlay == BUDDY_OVERLAY_PAIRING) {
        snprintf(body, sizeof(body), "请在电脑上输入此配对码\n\n       %06lu", (unsigned long)s->passkey);
        panel(layer, 66, 188, COL_BLUE, "蓝牙配对", body, "请保持此界面开启");
    } else if (overlay == BUDDY_OVERLAY_APPROVAL) {
        snprintf(body, sizeof(body), "%s\n\n%s", s->prompt_tool, s->prompt_hint);
        panel(layer, 154, 158, s->approval_locked ? COL_DIM : COL_RED, "助手请求授权", body,
              s->approval_locked ? (s->permission_delivery == BUDDY_PERMISSION_DELIVERY_FAILED ? "发送失败" : "正在发送……") : BUDDY_ACTION_APPROVAL);
    } else if (overlay == BUDDY_OVERLAY_MENU) {
        static const char *const menu[] = {"返回首页", "设置", "帮助", "关闭屏幕"};
        unsigned i;
        box(layer, 38, 48, 164, 74 + BUDDY_MENU_COUNT * 25, COL_CARD, COL_LINE, 1, 5);
        text(layer, 52, 61, 136, COL_TITLE, "菜单", true, LV_TEXT_ALIGN_CENTER);
        rule(layer, 52, 88, 136, COL_LINE);
        for (i = 0; i < BUDDY_MENU_COUNT; ++i) {
            int y = 103 + (int)i * 25;
            bool active = i == (unsigned)s->menu_selection;
            if (active) box(layer, 48, y - 7, 144, 21, COL_TITLE, COL_TITLE, 0, 2);
            text(layer, 56, y, 128, active ? COL_BG : COL_INK, menu[i], false, LV_TEXT_ALIGN_CENTER);
        }
    } else if (s->dsh_notice) {
        panel(layer, 194, 86, COL_TITLE, "DSH 任务完成",
              "助手已完成当前任务。", "请在电脑上查看结果");
    } else if (s->character == BUDDY_CHARACTER_CELEBRATE) {
        if (s->page != BUDDY_PAGE_HOME) {
            panel(layer, 194, 86, COL_TITLE, "任务已完成",
                  "助手已完成当前任务。", "请在电脑上查看结果");
        }
    }
}

static void draw_wifi(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    static const char *const items[BUDDY_WIFI_COUNT] = {"返回", "切换为 Wi-Fi", "重新配网"};
    char address[48];
    text(layer, 14, 38, 212, COL_TITLE, "Wi-Fi", true, LV_TEXT_ALIGN_LEFT);
    snprintf(address, sizeof(address), "IP: %s", s->lan_mode && s->lan_ip[0] ? s->lan_ip : "--");
    text(layer, 14, 68, 212, COL_TELEMETRY, s->lan_mode ? "当前：Wi-Fi" : "当前：蓝牙",
         false, LV_TEXT_ALIGN_LEFT);
    text(layer, 14, 88, 212, COL_DIM, address, false, LV_TEXT_ALIGN_LEFT);
    rule(layer, 14, 114, 212, COL_LINE);
    for (unsigned i = 0; i < BUDDY_WIFI_COUNT; ++i) {
        int y = 130 + (int)i * 36;
        bool selected = i == s->info_page;
        if (selected) box(layer, 14, y - 4, 212, 26, COL_TITLE, COL_TITLE, 0, 0);
        text(layer, 22, y, 196, selected ? COL_BG : COL_INK, items[i], false, LV_TEXT_ALIGN_LEFT);
    }
}

static void draw_region(int top, int end)
{
    lv_layer_t *layer = NULL;
    if (!s_canvas || !s_have_snapshot) return;
    s_clip_top = top;
    s_clip_end = end;
    memset(s_canvas_buffer + I4_PALETTE_BYTES + top * (UI_W / 2), 0,
           (end - top) * (UI_W / 2));
    draw_status_bar(layer, &s_snapshot);
    switch (s_snapshot.page) {
    case BUDDY_PAGE_DEEPSEEK: draw_deepseek(layer, &s_snapshot); break;
    case BUDDY_PAGE_PET: draw_companion(layer, &s_snapshot); break;
    case BUDDY_PAGE_INFO: draw_info(layer, &s_snapshot); break;
    case BUDDY_PAGE_SETTINGS: draw_settings(layer, &s_snapshot); break;
    case BUDDY_PAGE_WIFI: draw_wifi(layer, &s_snapshot); break;
    default: draw_home(layer, &s_snapshot); break;
    }
    draw_overlay(layer, &s_snapshot);
    s_clip_top = 0;
    s_clip_end = UI_H;
    lv_area_t area = {.x1 = 0, .y1 = top, .x2 = UI_W - 1, .y2 = end - 1};
    lv_obj_invalidate_area(s_canvas, &area);
}

static void redraw(void)
{
    draw_region(0, UI_H);
}

static bool selection_rows(const buddy_ui_snapshot_t *next, int *old_y, int *new_y)
{
    if (!s_have_snapshot || s_snapshot.screen_off || next->screen_off ||
        next->confirmation_pending || next->passkey_visible || next->lan_pair_id ||
        next->prompt_id[0] || s_snapshot.page != next->page ||
        s_snapshot.menu_open != next->menu_open || s_snapshot.reset_open != next->reset_open) return false;

    buddy_ui_snapshot_t previous = s_snapshot;
    int first, step, old, selected;
    if (next->menu_open) {
        first = 96; step = 25;
        old = previous.menu_selection; selected = next->menu_selection;
        previous.menu_selection = next->menu_selection;
    } else if (next->page == BUDDY_PAGE_SETTINGS) {
        first = 69; step = 25;
        if (next->reset_open) {
            old = previous.reset_selection; selected = next->reset_selection;
            previous.reset_selection = next->reset_selection;
        } else {
            old = previous.settings_selection; selected = next->settings_selection;
            previous.settings_selection = next->settings_selection;
        }
    } else if (next->page == BUDDY_PAGE_WIFI) {
        first = 126; step = 36;
        old = previous.info_page; selected = next->info_page;
        previous.info_page = next->info_page;
    } else {
        return false;
    }
    if (old == selected || memcmp(&previous, next, sizeof(previous)) != 0) return false;
    *old_y = first + step * old;
    *new_y = first + step * selected;
    return true;
}

void buddy_ui_init(void)
{
    unsigned i;
    if (s_screen) return;
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, UI_W, UI_H);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    s_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_buffer(s_canvas, s_canvas_buffer, UI_W, UI_H, LV_COLOR_FORMAT_I4);
    lv_obj_set_pos(s_canvas, 0, 0);
    buddy_i4_surface_init(&s_surface, s_canvas_buffer + I4_PALETTE_BYTES,
                          UI_W, UI_H, UI_W / 2);
    for (i = 0; i < sizeof(s_palette_rgb) / sizeof(s_palette_rgb[0]); ++i)
        lv_canvas_set_palette(s_canvas, i, lv_color_to_32(lv_color_hex(s_palette_rgb[i]), LV_OPA_COVER));
    lv_screen_load(s_screen);
}

void buddy_ui_render(const buddy_ui_snapshot_t *snapshot)
{
    if (!snapshot) return;
    buddy_ui_init();
    static uint64_t last_full_second = UINT64_MAX;
    uint64_t second = s_elapsed_ms / 1000U;
    bool unchanged = s_have_snapshot && memcmp(&s_snapshot, snapshot, sizeof(*snapshot)) == 0;
    if (unchanged && (snapshot->screen_off || last_full_second == second)) return;

    int old_y, new_y;
    bool partial = last_full_second == second && selection_rows(snapshot, &old_y, &new_y);
    s_snapshot = *snapshot;
    s_have_snapshot = true;
    if (snapshot->screen_off) return;
    if (partial) {
        int row_height = snapshot->menu_open ? 25 : snapshot->page == BUDDY_PAGE_WIFI ? 36 : 25;
        draw_region(old_y, old_y + row_height);
        draw_region(new_y, new_y + row_height);
    } else {
        s_scroll = 0;
        redraw();
        last_full_second = second;
    }
}

void buddy_ui_show_passkey(uint32_t passkey)
{
    buddy_ui_snapshot_t snapshot = {.passkey_visible = true, .passkey = passkey};
    buddy_ui_render(&snapshot);
}

void buddy_ui_tick(uint64_t elapsed_ms)
{
    s_elapsed_ms = elapsed_ms;
    s_tick = (uint32_t)(elapsed_ms / 200U);
}

void buddy_ui_scroll(int delta)
{
    s_scroll += delta;
    if (s_scroll < 0) s_scroll = 0;
    if (s_scroll > 160) s_scroll = 160;
    redraw();
}

bool buddy_ui_write_status(FILE *stream)
{
    if (stream == NULL || !s_have_snapshot) return false;
    return fprintf(stream, "FAP_UI {\"running\":%u,\"completion_seq\":%llu,"
                   "\"stale\":%s,\"screen_off\":%s,\"page\":%u,"
                   "\"sound_mode\":%u,\"character\":%u,"
                   "\"sleep_mode\":%u,\"brightness_level\":%u}\n",
                   s_snapshot.running,
                   (unsigned long long)s_snapshot.codex_usage.completion_sequence,
                   s_snapshot.heartbeat_stale ? "true" : "false",
                   s_snapshot.screen_off ? "true" : "false",
                   (unsigned)s_snapshot.page, (unsigned)s_snapshot.sound_mode,
                   (unsigned)s_snapshot.character, (unsigned)s_snapshot.sleep_mode,
                   (unsigned)s_snapshot.brightness_level) > 0 && fflush(stream) == 0;
}

bool buddy_ui_write_screenshot(FILE *stream)
{
    uint8_t row[UI_W * 2U];
    unsigned x;
    unsigned y;

    if (stream == NULL || !s_have_snapshot) {
        return false;
    }
    if (fprintf(stream, "FAP_SCREENSHOT_V1 %u %u RGB565LE %u\n",
                UI_W, UI_H, UI_W * UI_H * 2U) < 0) {
        return false;
    }
    for (y = 0; y < UI_H; ++y) {
        for (x = 0; x < UI_W; ++x) {
            uint8_t palette_index = buddy_i4_get_pixel(
                s_canvas_buffer + I4_PALETTE_BYTES, UI_W,
                (uint16_t)x, (uint16_t)y);
            uint32_t rgb = s_palette_rgb[palette_index & 0x0fU];
            uint16_t rgb565 = (uint16_t)(((rgb >> 8) & 0xf800U) |
                                         ((rgb >> 5) & 0x07e0U) |
                                         ((rgb >> 3) & 0x001fU));

            row[x * 2U] = (uint8_t)(rgb565 & 0xffU);
            row[x * 2U + 1U] = (uint8_t)(rgb565 >> 8);
        }
        if (fwrite(row, 1, sizeof(row), stream) != sizeof(row)) {
            return false;
        }
    }
    return fflush(stream) == 0;
}
