// kernel/core/kernel.c

#include <stdint.h>
#include "boot.h"

// Simple wrapper so we can halt the CPU cleanly forever.
static inline void hlt(void) {
    __asm__ volatile("hlt");
}

// Global framebuffer info
static uint32_t *g_fb    = 0;
static uint32_t  g_width = 0;
static uint32_t  g_height = 0;
static uint32_t  g_pitch  = 0;  // pixels per scanline

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
        uint32_t *row = g_fb + yy * g_pitch;
        for (uint32_t xx = x; xx < max_x; ++xx) {
            row[xx] = color;
        }
    }
}

// Simple horizontal progress bar for boot spinner
static void draw_progress_bar(uint32_t cx, uint32_t cy,
                              uint32_t w, uint32_t h,
                              uint32_t percent,
                              uint32_t bg, uint32_t fg) {
    if (w == 0 || h == 0) return;
    if (percent > 100) percent = 100;

    uint32_t x = (cx > w/2) ? (cx - w/2) : 0;
    uint32_t y = (cy > h/2) ? (cy - h/2) : 0;

    // background
    fill_rect(x, y, w, h, bg);

    // foreground portion
    uint32_t filled = (w * percent) / 100;
    fill_rect(x, y, filled, h, fg);
}

static void draw_boot_screen(void) {
    const uint32_t bg = 0x101020; // dark blue-ish
    const uint32_t bulb_body = 0xFFFFCC;
    const uint32_t bulb_outline = 0xCCCC99;
    const uint32_t text_bar = 0x202030;

    // Clear background
    fill_rect(0, 0, g_width, g_height, bg);

    // Light bulb "icon" in center-ish
    uint32_t cx = g_width / 2;
    uint32_t cy = g_height / 3;

    uint32_t bulb_w = g_width / 12;
    uint32_t bulb_h = g_height / 6;
    if (bulb_w < 40) bulb_w = 40;
    if (bulb_h < 60) bulb_h = 60;

    // Bulb body (simple rounded rectangle-ish via two rects)
    uint32_t bulb_x = (cx > bulb_w/2) ? (cx - bulb_w/2) : 0;
    uint32_t bulb_y = (cy > bulb_h/2) ? (cy - bulb_h/2) : 0;

    fill_rect(bulb_x, bulb_y, bulb_w, bulb_h, bulb_body);

    // Outline
    fill_rect(bulb_x, bulb_y, bulb_w, 2, bulb_outline);
    fill_rect(bulb_x, bulb_y + bulb_h - 2, bulb_w, 2, bulb_outline);
    fill_rect(bulb_x, bulb_y, 2, bulb_h, bulb_outline);
    fill_rect(bulb_x + bulb_w - 2, bulb_y, 2, bulb_h, bulb_outline);

    // "Socket" bar under bulb
    uint32_t sock_h = bulb_h / 5;
    if (sock_h < 6) sock_h = 6;
    fill_rect(bulb_x, bulb_y + bulb_h, bulb_w, sock_h, text_bar);

    // Progress bar under the bulb
    uint32_t bar_y = bulb_y + bulb_h + sock_h + 20;
    draw_progress_bar(cx, bar_y, g_width / 3, g_height / 100 + 6,
                      70, // static 70% just to show something
                      0x202030, 0x60A0FF);
}

static void draw_desktop(void) {
    // Colors in 0xRRGGBB
    const uint32_t desktop_bg   = 0x003366;  // dark blue
    const uint32_t taskbar_bg   = 0x202020;  // dark gray
    const uint32_t tray_bg      = 0x303030;  // slightly lighter gray
    const uint32_t icon_bg      = 0x406080;  // bluish icon block
    const uint32_t window_border = 0x000000; // black
    const uint32_t window_title  = 0x004080; // blue title bar
    const uint32_t window_body   = 0xC0C0C0; // light gray
    const uint32_t close_btn     = 0x800000; // dark red

    // 1) Desktop background
    fill_rect(0, 0, g_width, g_height, desktop_bg);

    // 2) Taskbar at bottom
    uint32_t taskbar_h = g_height / 14;
    if (taskbar_h < 28) taskbar_h = 28;

    uint32_t taskbar_y = g_height - taskbar_h;
    fill_rect(0, taskbar_y, g_width, taskbar_h, taskbar_bg);

    // 3) "Start" button area on left side of taskbar
    uint32_t start_btn_margin = 4;
    uint32_t start_btn_w = 80;
    uint32_t start_btn_h = taskbar_h - 2 * start_btn_margin;

    fill_rect(
        start_btn_margin,
        taskbar_y + start_btn_margin,
        start_btn_w,
        start_btn_h,
        0x404040
    );

    // 4) System tray area at right side of taskbar
    uint32_t tray_w = g_width / 5;
    if (tray_w < 140) tray_w = 140;
    uint32_t tray_x = g_width - tray_w - 4;

    fill_rect(tray_x, taskbar_y + 2, tray_w, taskbar_h - 4, tray_bg);

    // Fake icons in tray (wifi, speaker, battery block, time slot)
    uint32_t icon_size = taskbar_h / 2;
    if (icon_size < 12) icon_size = 12;
    uint32_t icon_y = taskbar_y + (taskbar_h - icon_size) / 2;
    uint32_t ix = tray_x + 8;

    // Wifi block
    fill_rect(ix, icon_y, icon_size, icon_size, 0x808080);
    ix += icon_size + 6;

    // Speaker block
    fill_rect(ix, icon_y, icon_size, icon_size, 0xA0A0A0);
    ix += icon_size + 6;

    // Battery block
    fill_rect(ix, icon_y, icon_size + 4, icon_size, 0x70A070);
    ix += icon_size + 10;

    // Time/date block
    uint32_t time_w = tray_x + tray_w - ix - 8;
    if (time_w > 0) {
        fill_rect(ix, icon_y, time_w, icon_size, 0x505050);
    }

    // 5) Desktop icons on left side (Settings, Files, Command Block, Browser)
    uint32_t icon_block_w = g_width / 14;
    uint32_t icon_block_h = g_height / 10;
    if (icon_block_w < 72) icon_block_w = 72;
    if (icon_block_h < 72) icon_block_h = 72;

    uint32_t col_x = 20;
    uint32_t row_y = 40;
    uint32_t v_gap = 20;

    // Settings icon block
    fill_rect(col_x, row_y, icon_block_w, icon_block_h, icon_bg);
    row_y += icon_block_h + v_gap;

    // Files (File Block)
    fill_rect(col_x, row_y, icon_block_w, icon_block_h, icon_bg);
    row_y += icon_block_h + v_gap;

    // Command Block (Terminal)
    fill_rect(col_x, row_y, icon_block_w, icon_block_h, icon_bg);
    row_y += icon_block_h + v_gap;

    // Integrated Browser
    fill_rect(col_x, row_y, icon_block_w, icon_block_h, icon_bg);

    // 6) One sample top-level window (square corners)
    uint32_t win_w = (g_width * 3) / 5;   // 60% of screen width
    uint32_t win_h = (g_height * 3) / 5;  // 60% of screen height

    uint32_t win_x = (g_width - win_w) / 2;
    uint32_t win_y = (g_height - win_h) / 2;
    if (win_y < 10) win_y = 10;

    // Outer border
    fill_rect(win_x - 1, win_y - 1, win_w + 2, win_h + 2, window_border);

    // Title bar (square corners, no rounding)
    uint32_t title_h = 28;
    fill_rect(win_x, win_y, win_w, title_h, window_title);

    // Window body
    fill_rect(win_x, win_y + title_h, win_w, win_h - title_h, window_body);

    // Fake close button on the right side of the title bar
    uint32_t btn_size = (title_h > 8) ? (title_h - 8) : (title_h / 2);
    uint32_t btn_x = win_x + win_w - btn_size - 4;
    uint32_t btn_y = win_y + (title_h - btn_size) / 2;

    fill_rect(btn_x, btn_y, btn_size, btn_size, close_btn);
}

// This is the ONLY entry point the bootloader calls.
// It must never return.
__attribute__((noreturn))
void kernel_main(BootInfo *bi) {
    // Framebuffer info from BootInfo
    g_fb     = (uint32_t*)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch;

    // 1) Boot screen
    draw_boot_screen();

    // Crude delay so you can see the boot screen.
    for (volatile uint64_t i = 0; i < 500000000ULL; ++i) {
        __asm__ volatile("");
    }

    // 2) Desktop UI
    draw_desktop();

    // Kernel MUST NOT return – just halt forever.
    for (;;) {
        hlt();
    }
}

