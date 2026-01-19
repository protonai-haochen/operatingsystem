// kernel/core/kernel.c
// LightOS 4 kernel: draws boot logo and desktop UI.

#include <stdint.h>
#include "boot.h"

static uint32_t *g_fb    = 0;
static uint32_t  g_width = 0;
static uint32_t  g_height = 0;
static uint32_t  g_pitch  = 0;

// Halt helper
static inline void hlt(void) {
    __asm__ volatile("hlt");
}

// Basic drawing ------------------------------------------------------

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_width || y >= g_height) return;
    g_fb[y * g_pitch + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h,
                      uint32_t color) {
    if (x >= g_width || y >= g_height) return;

    uint32_t max_x = x + w;
    uint32_t max_y = y + h;
    if (max_x > g_width)  max_x = g_width;
    if (max_y > g_height) max_y = g_height;

    for (uint32_t yy = y; yy < max_y; ++yy) {
        uint32_t *row = g_fb + (uint64_t)yy * g_pitch;
        for (uint32_t xx = x; xx < max_x; ++xx) {
            row[xx] = color;
        }
    }
}

// 7-segment style digits for tray clock / static text ----------------

static void draw_digit7_segments(uint32_t x, uint32_t y,
                                 uint32_t s, uint8_t mask,
                                 uint32_t fg, uint32_t bg)
{
    // clear background for this digit
    uint32_t w = 4 * s;
    uint32_t h = 7 * s;
    fill_rect(x, y, w, h, bg);

    // segment thickness
    uint32_t t = s;

    // segments:
    // 0: top
    if (mask & (1 << 0)) {
        fill_rect(x + t, y, 2 * t, t, fg);
    }
    // 1: upper-left
    if (mask & (1 << 1)) {
        fill_rect(x, y + t, t, 3 * t, fg);
    }
    // 2: upper-right
    if (mask & (1 << 2)) {
        fill_rect(x + 3 * t, y + t, t, 3 * t, fg);
    }
    // 3: middle
    if (mask & (1 << 3)) {
        fill_rect(x + t, y + 3 * t, 2 * t, t, fg);
    }
    // 4: lower-left
    if (mask & (1 << 4)) {
        fill_rect(x, y + 3 * t, t, 3 * t, fg);
    }
    // 5: lower-right
    if (mask & (1 << 5)) {
        fill_rect(x + 3 * t, y + 3 * t, t, 3 * t, fg);
    }
    // 6: bottom
    if (mask & (1 << 6)) {
        fill_rect(x + t, y + 6 * t, 2 * t, t, fg);
    }
}

static uint8_t digit7_mask(int d) {
    switch (d) {
        case 0: return 0b0111111;
        case 1: return 0b0000110;
        case 2: return 0b1011011;
        case 3: return 0b1001111;
        case 4: return 0b1100110;
        case 5: return 0b1101101;
        case 6: return 0b1111101;
        case 7: return 0b0000111;
        case 8: return 0b1111111;
        case 9: return 0b1101111;
        default: return 0;
    }
}

// Draw a single digit 0–9
static void draw_digit7(uint32_t x, uint32_t y, uint32_t scale, int d,
                        uint32_t fg, uint32_t bg)
{
    draw_digit7_segments(x, y, scale, digit7_mask(d), fg, bg);
}

// Draw colon for HH:MM
static void draw_colon(uint32_t x, uint32_t y, uint32_t scale,
                       uint32_t fg, uint32_t bg)
{
    uint32_t w = 2 * scale;
    uint32_t h = 7 * scale;
    fill_rect(x, y, w, h, bg);

    uint32_t dot = scale;
    fill_rect(x,         y + 2 * scale, dot, dot, fg);
    fill_rect(x,         y + 4 * scale, dot, dot, fg);
}

// Boot screen --------------------------------------------------------

static void draw_spinner_frame(uint32_t cx, uint32_t cy, uint32_t radius,
                               uint32_t active, uint32_t bg)
{
    // 8-dot spinner, one bright, others dim
    const int8_t dx[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };
    const int8_t dy[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };

    uint32_t dot_size = radius / 3;
    if (dot_size < 3) dot_size = 3;

    // clear area
    uint32_t box = radius * 2 + dot_size * 2;
    fill_rect(cx - box/2, cy - box/2, box, box, bg);

    for (uint32_t i = 0; i < 8; ++i) {
        int32_t px = (int32_t)cx + (int32_t)dx[i] * (int32_t)radius;
        int32_t py = (int32_t)cy + (int32_t)dy[i] * (int32_t)radius;
        uint32_t col = (i == (active & 7)) ? 0xFFFFFF : 0x606070;
        fill_rect((uint32_t)(px - (int32_t)(dot_size/2)),
                  (uint32_t)(py - (int32_t)(dot_size/2)),
                  dot_size, dot_size, col);
    }
}

// Simple block letters for "LightOS 4"
static void draw_letter_L(uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t t = w / 5; if (t < 2) t = 2;
    fill_rect(x, y, t, h, c);
    fill_rect(x, y + h - t, w, t, c);
}

static void draw_letter_i(uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t t = w / 6; if (t < 2) t = 2;
    fill_rect(x + (w/2 - t/2), y + h/4, t, h*3/4, c);
    fill_rect(x + (w/2 - t/2), y, t, t, c);
}

static void draw_letter_g(uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t t = w / 5; if (t < 2) t = 2;
    // big O
    fill_rect(x + t, y,         w - 2*t, t, c);
    fill_rect(x + t, y + h - t, w - 2*t, t, c);
    fill_rect(x,     y + t,     t,       h - 2*t, c);
    fill_rect(x + w - t, y + t, t,       h - 2*t, c);
    // opening on right-bottom
    fill_rect(x + w - t, y + h/2, t, h/2, 0);
    // tail
    fill_rect(x + w/2, y + h/2, t, h/2, c);
}

static void draw_letter_h(uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t t = w / 6; if (t < 2) t = 2;
    fill_rect(x, y, t, h, c);
    fill_rect(x + w - t, y + h/3, t, 2*h/3, c);
    fill_rect(x, y + h/3, w - t, t, c);
}

static void draw_letter_t(uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t t = w / 5; if (t < 2) t = 2;
    fill_rect(x, y, w, t, c);
    fill_rect(x + w/2 - t/2, y, t, h, c);
}

static void draw_letter_O(uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t t = w / 5; if (t < 2) t = 2;
    fill_rect(x + t, y,         w - 2*t, t, c);
    fill_rect(x + t, y + h - t, w - 2*t, t, c);
    fill_rect(x,     y + t,     t,       h - 2*t, c);
    fill_rect(x + w - t, y + t, t,       h - 2*t, c);
}

static void draw_letter_S(uint32_t x, uint32_t y,
                          uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t t = w / 5; if (t < 2) t = 2;
    fill_rect(x + t, y,         w - 2*t, t, c);
    fill_rect(x,     y + t,     t,       h/2 - t, c);
    fill_rect(x + t, y + h/2 - t/2, w - 2*t, t, c);
    fill_rect(x + w - t, y + h/2, t,       h/2 - t, c);
    fill_rect(x + t, y + h - t,   w - 2*t, t, c);
}

static void draw_digit_4(uint32_t x, uint32_t y,
                         uint32_t w, uint32_t h, uint32_t c)
{
    uint32_t t = w / 5; if (t < 2) t = 2;
    fill_rect(x, y + h/3, w/2, t, c);
    fill_rect(x + w/2 - t/2, y, t, h, c);
    fill_rect(x, y, t, h/2, c);
}

static void draw_title_lightos4(uint32_t center_x, uint32_t y,
                                uint32_t scale, uint32_t color)
{
    uint32_t w = 8 * scale;
    uint32_t h = 14 * scale;
    uint32_t space = 3 * scale;

    // L i g h t O S [space] 4  -> 9 slots
    uint32_t char_w_used = w;
    uint32_t total_w = 0;
    total_w += 7 * (char_w_used + space);  // "LightOS"
    total_w += (w/2);                      // space
    total_w += char_w_used;               // "4"

    uint32_t x = (center_x > total_w/2) ? (center_x - total_w/2) : 0;

    // L
    draw_letter_L(x, y, w, h, color);
    x += char_w_used + space;
    // i
    draw_letter_i(x, y, w, h, color);
    x += char_w_used + space;
    // g
    draw_letter_g(x, y, w, h, color);
    x += char_w_used + space;
    // h
    draw_letter_h(x, y, w, h, color);
    x += char_w_used + space;
    // t
    draw_letter_t(x, y, w, h, color);
    x += char_w_used + space;
    // O
    draw_letter_O(x, y, w, h, color);
    x += char_w_used + space;
    // S
    draw_letter_S(x, y, w, h, color);
    x += char_w_used + space;

    // space
    x += w/2;

    // 4
    draw_digit_4(x, y, w, h, color);
}

static void draw_boot_screen(void) {
    const uint32_t bg           = 0x001020; // darkish blue
    const uint32_t bulb_body    = 0xFFF7CC;
    const uint32_t bulb_outline = 0xD0C080;
    const uint32_t socket_col   = 0x404040;

    uint32_t cx = g_width / 2;
    uint32_t cy = g_height / 3;

    // Animate for a fixed number of frames
    for (uint32_t frame = 0; frame < 64; ++frame) {
        // background
        fill_rect(0, 0, g_width, g_height, bg);

        // --- light bulb ---
        uint32_t bulb_radius = g_height / 12;
        if (bulb_radius < 40) bulb_radius = 40;

        uint32_t bulb_cx = cx;
        uint32_t bulb_cy = cy;

        // circular-ish top (poor man's circle)
        for (int32_t yy = -(int32_t)bulb_radius; yy <= (int32_t)bulb_radius; ++yy) {
            for (int32_t xx = -(int32_t)bulb_radius; xx <= (int32_t)bulb_radius; ++xx) {
                int32_t dx = xx;
                int32_t dy = yy;
                if (dx*dx + dy*dy <= (int32_t)bulb_radius*(int32_t)bulb_radius) {
                    put_pixel(bulb_cx + dx, bulb_cy + dy, bulb_body);
                }
            }
        }
        // outline ring
        for (int32_t yy = -(int32_t)bulb_radius; yy <= (int32_t)bulb_radius; ++yy) {
            for (int32_t xx = -(int32_t)bulb_radius; xx <= (int32_t)bulb_radius; ++xx) {
                int32_t dx = xx;
                int32_t dy = yy;
                int32_t d2 = dx*dx + dy*dy;
                int32_t r2 = (int32_t)bulb_radius*(int32_t)bulb_radius;
                int32_t inner = (int32_t)(bulb_radius - 2);
                if (d2 <= r2 && d2 >= inner*inner) {
                    put_pixel(bulb_cx + dx, bulb_cy + dy, bulb_outline);
                }
            }
        }

        // socket
        uint32_t sock_w = bulb_radius * 2;
        uint32_t sock_h = bulb_radius / 2;
        if (sock_h < 16) sock_h = 16;
        uint32_t sock_x = bulb_cx - sock_w/2;
        uint32_t sock_y = bulb_cy + bulb_radius + 4;
        fill_rect(sock_x, sock_y, sock_w, sock_h, socket_col);

        // --- title "LightOS 4" ---
        uint32_t title_y = sock_y + sock_h + 10;
        draw_title_lightos4(cx, title_y, 2, 0xFFFFFF);

        // --- spinner ---
        uint32_t spin_y = title_y + 40;
        uint32_t spin_radius = 18;
        draw_spinner_frame(cx, spin_y, spin_radius, frame, bg);

        // small busy-wait so frames are visible
        for (volatile uint64_t wait = 0; wait < 15000000ULL; ++wait) {
            __asm__ volatile("");
        }
    }
}

// Tray icons ---------------------------------------------------------

static void draw_wifi_icon(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t bar_h = size / 5;
    if (bar_h < 2) bar_h = 2;

    fill_rect(x,              y + size - bar_h,      size / 4, bar_h, 0xFFFFFF);
    fill_rect(x + size / 3,   y + size - 2 * bar_h,  size / 4, bar_h, 0xFFFFFF);
    fill_rect(x + 2 * size/3, y + size - 3 * bar_h,  size / 4, bar_h, 0xFFFFFF);
}

static void draw_battery_icon(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                              uint32_t level_color) {
    uint32_t border = 2;
    if (w < 6) w = 6;
    if (h < 6) h = 6;

    // border
    fill_rect(x, y, w, h, 0xFFFFFF);
    fill_rect(x + border, y + border,
              w - 2 * border, h - 2 * border, 0x202020);

    // fill (70% fake level)
    uint32_t inner_w = w - 2 * border;
    uint32_t fill_w  = (inner_w * 70) / 100;
    fill_rect(x + border, y + border, fill_w, h - 2 * border,
              level_color);

    // positive terminal
    uint32_t term_w = w / 8;
    if (term_w < 2) term_w = 2;
    fill_rect(x + w, y + h/3, term_w, h/3, 0xFFFFFF);
}

static void draw_speaker_icon(uint32_t x, uint32_t y, uint32_t size) {
    // little square speaker + one "sound wave"
    uint32_t box = size / 2;
    if (box < 4) box = 4;

    fill_rect(x, y + (size - box)/2, box, box, 0xFFFFFF);
    fill_rect(x + box, y + (size - box)/2 + box/4,
              box/2, box/2, 0xFFFFFF);
}

// draw a hard-coded time/date: "12:34" and "2026-01-19"
static void draw_time_date(uint32_t x, uint32_t y, uint32_t scale) {
    uint32_t fg = 0xFFFFFF;
    uint32_t bg = 0x404040;

    uint32_t cursor = x;
    uint32_t adv_digit = 5 * scale;
    uint32_t adv_colon = 3 * scale;
    uint32_t adv_dash  = 5 * scale;
    uint32_t adv_space = 3 * scale; (void)adv_space;

    // ---- time "12:34" ----
    // 1
    draw_digit7(cursor, y, scale, 1, fg, bg);
    cursor += adv_digit;
    // 2
    draw_digit7(cursor, y, scale, 2, fg, bg);
    cursor += adv_digit;
    // colon
    draw_colon(cursor, y, scale, fg, bg);
    cursor += adv_colon;
    // 3
    draw_digit7(cursor, y, scale, 3, fg, bg);
    cursor += adv_digit;
    // 4
    draw_digit7(cursor, y, scale, 4, fg, bg);
    cursor += adv_digit;

    // small gap
    cursor += adv_digit;

    // ---- date "2026-01-19" ----
    uint32_t date_y = y + 9 * scale;
    cursor = x;

    int digits[] = {2,0,2,6};
    for (int i = 0; i < 4; ++i) {
        draw_digit7(cursor, date_y, scale, digits[i], fg, bg);
        cursor += adv_digit;
    }

    cursor += adv_space;

    int month[] = {0,1};
    for (int i = 0; i < 2; ++i) {
        draw_digit7(cursor, date_y, scale, month[i], fg, bg);
        cursor += adv_digit;
    }

    cursor += adv_space;

    int day[] = {1,9};
    for (int i = 0; i < 2; ++i) {
        draw_digit7(cursor, date_y, scale, day[i], fg, bg);
        cursor += adv_digit;
    }
}

// App icons on the left side -----------------------------------------

static void draw_app_icons(void) {
    uint32_t icon_w = g_width / 16;
    uint32_t icon_h = g_height / 10;
    if (icon_w < 48) icon_w = 48;
    if (icon_h < 48) icon_h = 48;

    uint32_t gap = icon_h / 6;
    uint32_t x = icon_w / 2;
    uint32_t y = icon_h / 2;

    uint32_t bg_panel   = 0x003366;
    uint32_t icon_bg    = 0x234567;
    uint32_t icon_fg    = 0xF0F0F0;
    uint32_t folder_col = 0xFFD880;
    uint32_t term_bg    = 0x000000;
    uint32_t term_fg    = 0x00FF00;
    uint32_t globe_bg   = 0x3399FF;
    uint32_t store_bg   = 0xEEEEEE;
    uint32_t store_fg   = 0x666666;

    // left side background panel
    fill_rect(0, 0, x + icon_w + gap, g_height, bg_panel);

    // 1) Settings (gear-ish square)
    fill_rect(x, y, icon_w, icon_h, icon_bg);
    uint32_t gear_x = x + icon_w/4;
    uint32_t gear_y = y + icon_h/4;
    uint32_t gear_w = icon_w/2;
    uint32_t gear_h = icon_h/2;
    fill_rect(gear_x, gear_y, gear_w, gear_h, icon_fg);
    fill_rect(gear_x + gear_w/4, gear_y + gear_h/4,
              gear_w/2, gear_h/2, icon_bg);

    y += icon_h + gap;

    // 2) File Block (folder)
    fill_rect(x, y, icon_w, icon_h, icon_bg);
    uint32_t folder_x = x + icon_w/8;
    uint32_t folder_y = y + icon_h/4;
    uint32_t folder_w = icon_w * 3 / 4;
    uint32_t folder_h = icon_h / 2;
    fill_rect(folder_x, folder_y, folder_w, folder_h, folder_col);
    fill_rect(folder_x, folder_y - folder_h/2,
              folder_w/2, folder_h/2, folder_col);

    y += icon_h + gap;

    // 3) Command Block (terminal)
    fill_rect(x, y, icon_w, icon_h, icon_bg);
    uint32_t term_x = x + icon_w/8;
    uint32_t term_y = y + icon_h/8;
    uint32_t term_w = icon_w * 3/4;
    uint32_t term_h = icon_h * 3/4;
    fill_rect(term_x, term_y, term_w, term_h, term_bg);
    // little green "prompt"
    fill_rect(term_x + term_w/6, term_y + term_h/2,
              term_w/6, term_h/12, term_fg);

    y += icon_h + gap;

    // 4) Browser (blue "globe")
    fill_rect(x, y, icon_w, icon_h, icon_bg);
    uint32_t globe_x = x + icon_w/2;
    uint32_t globe_y = y + icon_h/2;
    uint32_t r = icon_h/3;
    for (int32_t yy = - (int32_t)r; yy <= (int32_t)r; ++yy) {
        for (int32_t xx = - (int32_t)r; xx <= (int32_t)r; ++xx) {
            if (xx*xx + yy*yy <= (int32_t)r*(int32_t)r) {
                put_pixel(globe_x + xx, globe_y + yy, globe_bg);
            }
        }
    }

    y += icon_h + gap;

    // 5) App Store (shopping bag)
    fill_rect(x, y, icon_w, icon_h, icon_bg);
    uint32_t bag_x = x + icon_w/6;
    uint32_t bag_y = y + icon_h/4;
    uint32_t bag_w = icon_w * 2/3;
    uint32_t bag_h = icon_h / 2;
    fill_rect(bag_x, bag_y, bag_w, bag_h, store_bg);
    // handle
    fill_rect(bag_x + bag_w/4, bag_y - bag_h/4,
              bag_w/2, bag_h/6, store_bg);
    fill_rect(bag_x + bag_w/4, bag_y - bag_h/4,
              bag_w/8, bag_h/4, store_fg);
    fill_rect(bag_x + bag_w*5/8, bag_y - bag_h/4,
              bag_w/8, bag_h/4, store_fg);
}

// Desktop ------------------------------------------------------------

static void draw_desktop(void) {
    uint32_t desktop_bg   = 0x003366;
    uint32_t panel_bg     = 0x202020;
    uint32_t panel_height = g_height / 12;
    if (panel_height < 40) panel_height = 40;

    // desktop background
    fill_rect(0, 0, g_width, g_height, desktop_bg);

    // bottom panel (taskbar)
    uint32_t panel_y = g_height - panel_height;
    fill_rect(0, panel_y, g_width, panel_height, panel_bg);

    // left "start" block on the taskbar
    uint32_t start_w = panel_height;
    fill_rect(0, panel_y, start_w, panel_height, 0x404040);

    // right system tray region
    uint32_t tray_w = g_width / 4;
    if (tray_w < 160) tray_w = 160;
    uint32_t tray_x = g_width - tray_w;
    fill_rect(tray_x, panel_y, tray_w, panel_height, 0x303030);

    // draw wifi, speaker, battery, time/date in tray
    uint32_t icon_size = panel_height / 2;
    if (icon_size < 16) icon_size = 16;
    uint32_t icon_y = panel_y + (panel_height - icon_size) / 2;

    uint32_t cursor_x = tray_x + tray_w - icon_size - 8;
    draw_wifi_icon(cursor_x, icon_y, icon_size);
    cursor_x -= icon_size + 8;

    draw_speaker_icon(cursor_x, icon_y, icon_size);
    cursor_x -= icon_size + 8;

    draw_battery_icon(cursor_x, icon_y, icon_size * 3/2, icon_size,
                      0x00FF00);
    cursor_x -= icon_size * 2;

    // time/date block
    uint32_t clock_scale = icon_size / 4;
    if (clock_scale < 2) clock_scale = 2;
    uint32_t clock_w = 12 * clock_scale;
    uint32_t clock_h = 16 * clock_scale;
    uint32_t clock_x = tray_x + 8;
    uint32_t clock_y = panel_y + (panel_height - clock_h) / 2;
    fill_rect(clock_x - 2, clock_y - 2,
              clock_w + 4, clock_h + 4, 0x404040);
    draw_time_date(clock_x, clock_y, clock_scale);

    // app icons on desktop
    draw_app_icons();

    // main window (e.g., File Block/App Store)
    uint32_t win_w = (g_width * 3) / 5;
    uint32_t win_h = (g_height * 3) / 5;
    uint32_t win_x = (g_width - win_w) / 2;
    uint32_t win_y = (g_height - win_h) / 2;
    if (win_y < 10) win_y = 10;

    uint32_t window_border = 0x000000;
    uint32_t window_title  = 0x004080;
    uint32_t window_body   = 0xC0C0C0;
    uint32_t close_btn     = 0x800000;

    // border
    fill_rect(win_x - 1, win_y - 1, win_w + 2, win_h + 2, window_border);
    // title bar
    uint32_t title_h = 28;
    fill_rect(win_x, win_y, win_w, title_h, window_title);
    // body
    fill_rect(win_x, win_y + title_h, win_w, win_h - title_h, window_body);

    // close button (square)
    uint32_t btn_size = (title_h > 8) ? (title_h - 8) : (title_h / 2);
    uint32_t btn_x = win_x + win_w - btn_size - 4;
    uint32_t btn_y = win_y + (title_h - btn_size) / 2;
    fill_rect(btn_x, btn_y, btn_size, btn_size, close_btn);
}

// Kernel entry -------------------------------------------------------

__attribute__((noreturn, section(".entry")))
void kernel_main(BootInfo *bi) {
    g_fb     = (uint32_t *)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch;

    // 1) Boot screen with animated spinner
    draw_boot_screen();

    // 2) Desktop UI
    draw_desktop();

    // 3) Halt forever
    for (;;) {
        hlt();
    }
}
