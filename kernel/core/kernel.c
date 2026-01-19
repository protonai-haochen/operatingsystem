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

// Boot screen --------------------------------------------------------

static void draw_boot_screen(void) {
    const uint32_t bg           = 0x101020; // dark blue
    const uint32_t bulb_body    = 0xFFFFCC;
    const uint32_t bulb_outline = 0xCCCC99;
    const uint32_t bar_bg       = 0x202030;
    const uint32_t bar_fg       = 0x60A0FF;

    fill_rect(0, 0, g_width, g_height, bg);

    uint32_t cx = g_width / 2;
    uint32_t cy = g_height / 3;

    uint32_t bulb_w = g_width / 12;
    uint32_t bulb_h = g_height / 6;
    if (bulb_w < 40) bulb_w = 40;
    if (bulb_h < 60) bulb_h = 60;

    uint32_t bulb_x = (cx > bulb_w/2) ? (cx - bulb_w/2) : 0;
    uint32_t bulb_y = (cy > bulb_h/2) ? (cy - bulb_h/2) : 0;

    // bulb body
    fill_rect(bulb_x, bulb_y, bulb_w, bulb_h, bulb_body);

    // outline
    fill_rect(bulb_x, bulb_y, bulb_w, 2, bulb_outline);
    fill_rect(bulb_x, bulb_y + bulb_h - 2, bulb_w, 2, bulb_outline);
    fill_rect(bulb_x, bulb_y, 2, bulb_h, bulb_outline);
    fill_rect(bulb_x + bulb_w - 2, bulb_y, 2, bulb_h, bulb_outline);

    // socket
    uint32_t sock_h = bulb_h / 5;
    if (sock_h < 6) sock_h = 6;
    fill_rect(bulb_x, bulb_y + bulb_h, bulb_w, sock_h, bar_bg);

    // progress bar
    uint32_t bar_y = bulb_y + bulb_h + sock_h + 20;
    uint32_t bar_w = g_width / 3;
    uint32_t bar_h = (g_height / 100) + 6;
    if (bar_w < 60) bar_w = 60;
    if (bar_h < 4)  bar_h = 4;

    uint32_t x = (cx > bar_w/2) ? (cx - bar_w/2) : 0;
    fill_rect(x, bar_y, bar_w, bar_h, bar_bg);
    uint32_t filled = (bar_w * 70) / 100; // fake 70% loaded
    fill_rect(x, bar_y, filled, bar_h, bar_fg);
}

// Tray icons ---------------------------------------------------------

static void draw_wifi_icon(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t bar_h = size / 5;
    if (bar_h < 2) bar_h = 2;

    fill_rect(x,              y + size - bar_h,       size / 3,       bar_h, 0xC0C0C0);
    fill_rect(x + size/3,     y + size - 2*bar_h - 1, size / 3,       bar_h, 0xA0A0A0);
    fill_rect(x + 2*size/3,   y + size - 3*bar_h - 2, size / 3,       bar_h, 0x808080);
}

static void draw_speaker_icon(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t sq = size / 2;
    if (sq < 4) sq = 4;
    fill_rect(x, y + (size - sq)/2, sq, sq, 0xD0D0D0);

    uint32_t bx = x + sq + 2;
    fill_rect(bx,     y + size/2 - 3, 3,  6, 0xB0B0B0);
    fill_rect(bx + 4, y + size/2 - 5, 3, 10, 0x909090);
}

static void draw_battery_icon(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (w < 12) w = 12;
    if (h < 8)  h = 8;

    // outer
    fill_rect(x, y, w, h, 0x303030);
    // "charge" 80% filled
    uint32_t inner_w = (w - 4) * 4 / 5;
    fill_rect(x + 2, y + 2, inner_w, h - 4, 0x70C070);

    // nub
    uint32_t nub_w = w / 6;
    if (nub_w < 3) nub_w = 3;
    fill_rect(x + w, y + h/4, nub_w, h/2, 0x303030);
}

// Desktop app icons --------------------------------------------------

static void draw_app_icons(void) {
    // 5 vertical icons: Settings, File Block, Command Block, Browser, App Store
    uint32_t icon_w = g_width / 16;
    uint32_t icon_h = g_height / 10;
    if (icon_w < 64) icon_w = 64;
    if (icon_h < 64) icon_h = 64;

    uint32_t x   = 32;
    uint32_t y   = 40;
    uint32_t gap = 16;

    // Settings (gear-ish)
    fill_rect(x, y, icon_w, icon_h, 0x2E4F7F);
    fill_rect(x + icon_w/4, y + icon_h/4, icon_w/2, icon_h/2, 0xB0B0B0);
    y += icon_h + gap;

    // File Block (folder)
    fill_rect(x, y, icon_w, icon_h, 0x355C8B);
    fill_rect(x + 6, y + icon_h/3,     icon_w - 12, icon_h/2,     0xE0E090);
    fill_rect(x + 6, y + icon_h/3 - 10, icon_w/2,   10,           0xE0E090);
    y += icon_h + gap;

    // Command Block (terminal)
    fill_rect(x, y, icon_w, icon_h, 0x284060);
    fill_rect(x + 8, y + 8, icon_w - 16, icon_h - 16, 0x101010);
    fill_rect(x + 14, y + icon_h/2 - 2, 10, 3, 0x40FF40);
    fill_rect(x + 22, y + icon_h/2 - 5, 3,  9, 0x40FF40);
    y += icon_h + gap;

    // Browser (globe)
    fill_rect(x, y, icon_w, icon_h, 0x305070);
    uint32_t cx = x + icon_w / 2;
    uint32_t cy = y + icon_h / 2;
    uint32_t r  = icon_h / 3;
    for (uint32_t yy = cy - r; yy <= cy + r; ++yy) {
        for (uint32_t xx = cx - r; xx <= cx + r; ++xx) {
            int32_t dx = (int32_t)xx - (int32_t)cx;
            int32_t dy = (int32_t)yy - (int32_t)cy;
            if (dx*dx + dy*dy <= (int32_t)r*(int32_t)r) {
                put_pixel(xx, yy, 0x50C0FF);
            }
        }
    }
    y += icon_h + gap;

    // App Store (shopping bag)
    fill_rect(x, y, icon_w, icon_h, 0x364F6F);
    fill_rect(x + 10, y + icon_h/3,     icon_w - 20, icon_h/2, 0xF0F0F0);
    fill_rect(x + 18, y + icon_h/3 - 8, icon_w - 36, 8,        0xF0F0F0);
}

// 7-segment digit rendering ------------------------------------------

static const uint8_t digit_mask[10] = {
    0x77, // 0
    0x24, // 1
    0x5D, // 2
    0x6D, // 3
    0x2E, // 4
    0x6B, // 5
    0x7B, // 6
    0x25, // 7
    0x7F, // 8
    0x6F  // 9
};

static void draw_digit7(uint32_t x, uint32_t y,
                        uint32_t scale,
                        uint8_t digit,
                        uint32_t fg,
                        uint32_t bg) {
    if (digit > 9) digit = 0;

    // overall cell background
    uint32_t cell_w = 4 * scale;
    uint32_t cell_h = 7 * scale;
    fill_rect(x, y, cell_w, cell_h, bg);

    uint8_t mask = digit_mask[digit];
    uint32_t t = scale;          // thickness

    // segments coordinates
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

static void draw_colon(uint32_t x, uint32_t y,
                       uint32_t scale,
                       uint32_t fg,
                       uint32_t bg) {
    uint32_t cell_w = 2 * scale;
    uint32_t cell_h = 7 * scale;
    fill_rect(x, y, cell_w, cell_h, bg);
    uint32_t dot_size = scale;
    fill_rect(x, y + 2 * scale, dot_size, dot_size, fg);
    fill_rect(x, y + 5 * scale, dot_size, dot_size, fg);
}

static void draw_dash(uint32_t x, uint32_t y,
                      uint32_t scale,
                      uint32_t fg,
                      uint32_t bg) {
    uint32_t cell_w = 4 * scale;
    uint32_t cell_h = 7 * scale;
    fill_rect(x, y, cell_w, cell_h, bg);
    fill_rect(x + scale, y + 3 * scale, 2 * scale, scale, fg);
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
    // :
    draw_colon(cursor, y, scale, fg, bg);
    cursor += adv_colon;
    // 3
    draw_digit7(cursor, y, scale, 3, fg, bg);
    cursor += adv_digit;
    // 4
    draw_digit7(cursor, y, scale, 4, fg, bg);

    // ---- date under it: "2026-01-19" ----
    uint32_t y2 = y + 8 * scale;
    cursor = x;

    // 2
    draw_digit7(cursor, y2, scale, 2, fg, bg);
    cursor += adv_digit;
    // 0
    draw_digit7(cursor, y2, scale, 0, fg, bg);
    cursor += adv_digit;
    // 2
    draw_digit7(cursor, y2, scale, 2, fg, bg);
    cursor += adv_digit;
    // 6
    draw_digit7(cursor, y2, scale, 6, fg, bg);
    cursor += adv_digit;
    // -
    draw_dash(cursor, y2, scale, fg, bg);
    cursor += adv_dash;
    // 0
    draw_digit7(cursor, y2, scale, 0, fg, bg);
    cursor += adv_digit;
    // 1
    draw_digit7(cursor, y2, scale, 1, fg, bg);
    cursor += adv_digit;
    // -
    draw_dash(cursor, y2, scale, fg, bg);
    cursor += adv_dash;
    // 1
    draw_digit7(cursor, y2, scale, 1, fg, bg);
    cursor += adv_digit;
    // 9
    draw_digit7(cursor, y2, scale, 9, fg, bg);
}

// Desktop composition ------------------------------------------------

static void draw_desktop(void) {
    const uint32_t desktop_bg   = 0x003366;
    const uint32_t taskbar_bg   = 0x202020;
    const uint32_t tray_bg      = 0x303030;
    const uint32_t window_border = 0x000000;
    const uint32_t window_title  = 0x004080;
    const uint32_t window_body   = 0xC0C0C0;
    const uint32_t close_btn     = 0x800000;

    // background
    fill_rect(0, 0, g_width, g_height, desktop_bg);

    // taskbar at bottom
    uint32_t taskbar_h = g_height / 14;
    if (taskbar_h < 28) taskbar_h = 28;
    uint32_t taskbar_y = g_height - taskbar_h;
    fill_rect(0, taskbar_y, g_width, taskbar_h, taskbar_bg);

    // start button (left)
    uint32_t start_margin = 4;
    uint32_t start_w = 90;
    uint32_t start_h = taskbar_h - 2 * start_margin;
    fill_rect(start_margin, taskbar_y + start_margin, start_w, start_h, 0x404040);

    // tray (right)
    uint32_t tray_w = g_width / 5;
    if (tray_w < 160) tray_w = 160;
    uint32_t tray_x = g_width - tray_w - 4;
    fill_rect(tray_x, taskbar_y + 2, tray_w, taskbar_h - 4, tray_bg);

    // tray icons
    uint32_t size   = taskbar_h - 8;
    if (size > tray_w / 4) size = tray_w / 4;
    uint32_t icon_y = taskbar_y + 4;
    uint32_t ix     = tray_x + 8;

    draw_wifi_icon(ix, icon_y, size);
    ix += size + 6;
    draw_speaker_icon(ix, icon_y, size);
    ix += size + 10;
    draw_battery_icon(ix, icon_y + (size/4), size + 16, size / 2);
    ix += size + 26;

    // time/date area
    uint32_t remaining = (tray_x + tray_w) - ix - 4;
    if (remaining > 0) {
        fill_rect(ix, icon_y, remaining, size, 0x404040);
        // draw static time/date in that area
        uint32_t scale = (size / 7);
        if (scale < 2) scale = 2;
        uint32_t tx = ix + 4;
        uint32_t ty = icon_y + 2;
        draw_time_date(tx, ty, scale);
    }

    // app icons on desktop
    draw_app_icons();

    // main window (e.g., File Block/App Store)
    uint32_t win_w = (g_width * 3) / 5;
    uint32_t win_h = (g_height * 3) / 5;
    uint32_t win_x = (g_width - win_w) / 2;
    uint32_t win_y = (g_height - win_h) / 2;
    if (win_y < 10) win_y = 10;

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

__attribute__((noreturn))
void kernel_main(BootInfo *bi) {
    g_fb     = (uint32_t *)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch;

    // 1) Boot screen
    draw_boot_screen();

    // crude delay so you can see the boot screen
    for (volatile uint64_t i = 0; i < 500000000ULL; ++i) {
        __asm__ volatile("");
    }

    // 2) Desktop UI
    draw_desktop();

    // 3) Halt forever
    for (;;) {
        hlt();
    }
}
