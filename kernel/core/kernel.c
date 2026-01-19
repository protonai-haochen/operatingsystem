// kernel/core/kernel.c
//
// LightOS 4 kernel (graphics-only mock UI):
//  - Boot splash: light bulb + "LightOS 4" + spinner
//  - First-boot setup OR login screen (compile-time toggle)
//  - Desktop with taskbar, icons, and tray
//
// NOTE: This is all drawn directly into the framebuffer. No input yet.

#include <stdint.h>
#include "boot.h"

// Set to 1 for first-boot setup screen, 0 for login screen:
#define FIRST_BOOT 1

// Global framebuffer state
static uint32_t *g_fb    = 0;
static uint32_t  g_width = 0;
static uint32_t  g_height = 0;
static uint32_t  g_pitch  = 0;   // pixels per scanline

static inline void hlt(void) {
    __asm__ volatile("hlt");
}

static inline void cpu_relax(void) {
    __asm__ volatile("pause");
}

// Super crude delay just so animations are visible.
// (Kernel is single-core busy-waiting here.)
static void delay(volatile uint64_t cycles) {
    while (cycles--) {
        cpu_relax();
    }
}

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_width || y >= g_height) {
        return;
    }
    g_fb[y * g_pitch + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h,
                      uint32_t color) {
    if (x >= g_width || y >= g_height) {
        return;
    }

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

// ---------------------------------------------------------------------
// Minimal "vector letters" just for drawing the boot text "LightOS 4".
// Everything is built out of rectangles (no font engine yet).
// ---------------------------------------------------------------------

static void draw_char_L(uint32_t x, uint32_t y, uint32_t color) {
    fill_rect(x,     y,     3, 18, color);
    fill_rect(x, y + 15,   10,  3, color);
}

static void draw_char_I(uint32_t x, uint32_t y, uint32_t color) {
    fill_rect(x + 4, y,     3, 18, color);
}

static void draw_char_G(uint32_t x, uint32_t y, uint32_t color) {
    // Rough "G" shape using box + inner bar.
    fill_rect(x + 1, y,      8,  3, color);
    fill_rect(x + 1, y + 13, 8,  3, color);
    fill_rect(x,     y,      3, 16, color);
    fill_rect(x + 7, y + 8,  3,  8, color);
    fill_rect(x + 4, y + 8,  6,  3, color);
}

static void draw_char_H(uint32_t x, uint32_t y, uint32_t color) {
    fill_rect(x,     y,     3, 16, color);
    fill_rect(x + 7, y,     3, 16, color);
    fill_rect(x, y + 7,    10,  3, color);
}

static void draw_char_T(uint32_t x, uint32_t y, uint32_t color) {
    fill_rect(x,     y,    10,  3, color);
    fill_rect(x + 4, y,     3, 16, color);
}

static void draw_char_O(uint32_t x, uint32_t y, uint32_t color) {
    fill_rect(x + 1, y,      8,  3, color);
    fill_rect(x + 1, y + 13, 8,  3, color);
    fill_rect(x,     y,      3, 16, color);
    fill_rect(x + 7, y,      3, 16, color);
}

static void draw_char_S(uint32_t x, uint32_t y, uint32_t color) {
    fill_rect(x + 1, y,      8,  3, color);
    fill_rect(x + 1, y + 7,  8,  3, color);
    fill_rect(x + 1, y + 13, 8,  3, color);
    fill_rect(x,     y + 1,  3,  5, color);
    fill_rect(x + 7, y + 8,  3,  5, color);
}

static void draw_char_4(uint32_t x, uint32_t y, uint32_t color) {
    fill_rect(x,     y,      3, 10, color);
    fill_rect(x + 7, y,      3, 16, color);
    fill_rect(x,     y + 7, 10,  3, color);
}

static void draw_text_lightos4(uint32_t x, uint32_t y, uint32_t color) {
    const uint32_t step = 12;

    draw_char_L(x, y, color); x += step;
    draw_char_I(x, y, color); x += step;
    draw_char_G(x, y, color); x += step;
    draw_char_H(x, y, color); x += step;
    draw_char_T(x, y, color); x += step;
    draw_char_O(x, y, color); x += step;
    draw_char_S(x, y, color); x += step;
    // space
    x += step / 2;
    draw_char_4(x, y, color);
}

// ---------------------------------------------------------------------
// Spinner (simple 8-dot loader around a circle)
// ---------------------------------------------------------------------

static void draw_spinner(uint32_t cx, uint32_t cy, uint32_t radius, uint32_t frame) {
    const uint32_t inactive = 0x404040;
    const uint32_t active   = 0xFFFFFF;
    const int steps = 8;
    static const int8_t dx[8] = { 0, 1, 1, 1,  0, -1, -1, -1 };
    static const int8_t dy[8] = { -1, -1, 0, 1,  1,  1,  0, -1 };

    int active_idx = (int)(frame % steps);

    for (int i = 0; i < steps; ++i) {
        uint32_t color = (i == active_idx) ? active : inactive;
        int px = (int)cx + dx[i] * (int)radius;
        int py = (int)cy + dy[i] * (int)radius;
        if (px < 0 || py < 0) {
            continue;
        }
        uint32_t ux = (uint32_t)px;
        uint32_t uy = (uint32_t)py;
        // small square "dot"
        fill_rect(ux > 2 ? ux - 2 : 0,
                  uy > 2 ? uy - 2 : 0,
                  5, 5, color);
    }
}

// ---------------------------------------------------------------------
// Boot splash: bulb + "LightOS 4" + spinner
// ---------------------------------------------------------------------

static void show_boot_animation(void) {
    const uint32_t bg_color    = 0x101020;
    const uint32_t bulb_color  = 0xFFF5C0;
    const uint32_t bulb_inner  = 0xFFE680;
    const uint32_t base_color  = 0xC0C0C0;
    const uint32_t text_color  = 0xFFFFFF;

    uint32_t frames = 96; // how long the boot screen shows

    for (uint32_t f = 0; f < frames; ++f) {
        // Background
        fill_rect(0, 0, g_width, g_height, bg_color);

        // Bulb size & position
        uint32_t bulb_w = g_width / 8;
        if (bulb_w < 80)  bulb_w = 80;
        if (bulb_w > g_width - 40) bulb_w = g_width - 40;

        uint32_t bulb_h = bulb_w * 4 / 3;
        uint32_t cx = g_width  / 2;
        uint32_t cy = g_height / 2 - g_height / 10;

        uint32_t bx = cx - bulb_w / 2;
        uint32_t by = (cy > bulb_h / 2) ? (cy - bulb_h / 2) : 0;

        // Bulb main body
        fill_rect(bx, by, bulb_w, bulb_h, bulb_color);

        // Inner "glow"
        fill_rect(bx + bulb_w / 4, by + bulb_h / 6,
                  bulb_w / 2, bulb_h * 2 / 3, bulb_inner);

        // Bulb base (socket)
        uint32_t base_h = bulb_h / 4;
        uint32_t base_y = by + bulb_h;
        uint32_t base_w = bulb_w / 2;
        uint32_t base_x = bx + (bulb_w - base_w) / 2;

        fill_rect(base_x, base_y, base_w, base_h / 2, base_color);
        fill_rect(base_x, base_y + base_h / 2, base_w, base_h / 2, 0x808080);

        // "LightOS 4" text under bulb
        uint32_t text_y = base_y + base_h + 16;
        uint32_t text_w = 7 * 12 + 12; // 7 letters + '4'
        uint32_t text_x = (g_width > text_w) ? (g_width - text_w) / 2 : 10;

        draw_text_lightos4(text_x, text_y, text_color);

        // Spinner below text
        uint32_t spinner_y = text_y + 40;
        if (spinner_y + 10 >= g_height) {
            spinner_y = g_height - 20;
        }
        draw_spinner(cx, spinner_y, 14, f);

        // crude delay so animation is visible
        delay(2000000);
    }
}

// ---------------------------------------------------------------------
// Icons for Settings / Files / Command Block / Browser
// ---------------------------------------------------------------------

static void draw_icon_settings(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t bg   = 0x303030;
    uint32_t gear = 0xCCCCCC;

    fill_rect(x, y, size, size, bg);

    uint32_t cx = x + size / 2;
    uint32_t cy = y + size / 2;
    uint32_t r1 = size / 4;
    uint32_t r2 = size / 6;

    static const int8_t dx[8] = { 0, 1, 1, 1,  0, -1, -1, -1 };
    static const int8_t dy[8] = { -1, -1, 0, 1,  1,  1,  0, -1 };

    // crude "teeth"
    for (int i = 0; i < 8; ++i) {
        int px = (int)cx + dx[i] * (int)r1;
        int py = (int)cy + dy[i] * (int)r1;
        if (px < 0 || py < 0) continue;
        fill_rect((uint32_t)px - 3, (uint32_t)py - 3, 6, 6, gear);
    }
    // inner hub
    fill_rect(cx - r2, cy - r2, r2 * 2, r2 * 2, gear);
}

static void draw_icon_files(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t bg     = 0x303030;
    uint32_t folder = 0xE0C060;
    uint32_t tab    = 0xF0D070;

    fill_rect(x, y, size, size, bg);

    uint32_t w = size - 8;
    uint32_t h = size - 12;
    uint32_t fx = x + 4;
    uint32_t fy = y + size / 3;

    // folder body
    fill_rect(fx, fy, w, h, folder);
    // tab
    fill_rect(fx, fy - h / 3, w / 2, h / 3, tab);
}

static void draw_icon_cmd(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t border = 0x303030;
    uint32_t bg     = 0x101010;
    uint32_t prompt = 0x00FF00;

    fill_rect(x, y, size, size, border);
    fill_rect(x + 2, y + 2, size - 4, size - 4, bg);

    // ">" arrow
    uint32_t px = x + 6;
    uint32_t py = y + size / 2;

    fill_rect(px,     py - 2, 2, 2, prompt);
    fill_rect(px + 2, py - 3, 2, 2, prompt);
    fill_rect(px + 4, py - 4, 2, 2, prompt);
    fill_rect(px + 2, py - 1, 2, 2, prompt);
    fill_rect(px + 4, py,     2, 2, prompt);
    fill_rect(px + 6, py + 1, 2, 2, prompt);
    fill_rect(px + 2, py + 1, 2, 2, prompt);
    fill_rect(px + 4, py + 2, 2, 2, prompt);

    // command line
    fill_rect(x + 6, y + size - 8, size - 12, 2, prompt);
}

static void draw_icon_browser(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t bg    = 0x303030;
    uint32_t globe = 0x2080D0;
    uint32_t band  = 0x60B0FF;

    fill_rect(x, y, size, size, bg);

    uint32_t cx = x + size / 2;
    uint32_t cy = y + size / 2;
    uint32_t r  = size / 3;

    // rough "globe"
    fill_rect(cx - r, cy - r,   r * 2,   r * 2, globe);
    fill_rect(cx - r, cy - r / 4, r * 2, r / 2, band);  // horizontal
    fill_rect(cx - r / 4, cy - r, r / 2, r * 2, band);  // vertical
}

// ---------------------------------------------------------------------
// System tray: WiFi, speaker, battery, clock block
// ---------------------------------------------------------------------

static void draw_tray_wifi(uint32_t x, uint32_t y, uint32_t h) {
    uint32_t color = 0xFFFFFF;
    uint32_t w = h;
    uint32_t base_y = y + h - 3;

    // 3 "arcs" approximated with horizontal bars
    fill_rect(x + w/2 - 2, base_y - 2, 4, 2, color);
    fill_rect(x + w/2 - 6, base_y - 5, 12, 2, color);
    fill_rect(x + w/2 - 10, base_y - 8, 20, 2, color);
}

static void draw_tray_speaker(uint32_t x, uint32_t y, uint32_t h) {
    uint32_t color = 0xFFFFFF;
    uint32_t w = h;

    // rectangle + crude triangle
    fill_rect(x, y + h/4, 4, h/2, color); // body

    uint32_t tri_x = x + 4;
    uint32_t tri_y = y + h/4;
    for (uint32_t i = 0; i < h/2; ++i) {
        // upper diagonal
        fill_rect(tri_x + i, tri_y + i, 1, 1, color);
        // lower diagonal
        fill_rect(tri_x + i, tri_y + (h/2) - i - 1, 1, 1, color);
    }
}

static void draw_tray_battery(uint32_t x, uint32_t y, uint32_t h) {
    uint32_t outline = 0xFFFFFF;
    uint32_t fill    = 0x80FF80;

    uint32_t w = h * 2;
    if (w < 20) w = 20;

    // outline
    fill_rect(x,         y,          w, 2, outline);
    fill_rect(x,         y + h - 2,  w, 2, outline);
    fill_rect(x,         y,          2, h, outline);
    fill_rect(x + w - 2, y,          2, h, outline);

    // tip
    uint32_t tip_w = 4;
    uint32_t tip_h = h / 3;
    fill_rect(x + w, y + (h - tip_h) / 2, tip_w, tip_h, outline);

    // "charge"
    fill_rect(x + 3, y + 3, w - 6, h - 6, fill);
}

static void draw_tray_clock(uint32_t right_x, uint32_t y, uint32_t h) {
    uint32_t bg = 0x303030;
    uint32_t fg = 0xFFFFFF;
    uint32_t w  = h * 2;
    if (w < 30) w = 30;

    uint32_t x = right_x - w;
    fill_rect(x, y, w, h, bg);

    // two "text" lines (time + date placeholders)
    fill_rect(x + 4, y + 3,      w - 8, 3, fg);
    fill_rect(x + 8, y + h - 6,  w - 16, 3, fg);
}

// ---------------------------------------------------------------------
// Generic square-corner window frame for apps
// ---------------------------------------------------------------------

static void draw_window_frame(uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h,
                              uint32_t title_color,
                              uint32_t body_color) {
    const uint32_t border     = 0x000000;
    const uint32_t close_col  = 0x800000;

    // border
    fill_rect(x - 1, y - 1, w + 2, h + 2, border);

    // title bar
    uint32_t title_h = 24;
    fill_rect(x, y, w, title_h, title_color);

    // body
    fill_rect(x, y + title_h, w, h - title_h, body_color);

    // square close button (no rounded corners)
    uint32_t btn_size = (title_h > 8) ? (title_h - 8) : (title_h / 2);
    uint32_t btn_x = x + w - btn_size - 4;
    uint32_t btn_y = y + (title_h - btn_size) / 2;
    fill_rect(btn_x, btn_y, btn_size, btn_size, close_col);
}

// ---------------------------------------------------------------------
// First-boot setup screen (username/password placeholders)
// ---------------------------------------------------------------------

static void draw_setup_screen(void) {
    const uint32_t bg        = 0x202020;
    const uint32_t panel     = 0x303030;
    const uint32_t input_bg  = 0xFFFFFF;
    const uint32_t button_bg = 0x0050A0;

    fill_rect(0, 0, g_width, g_height, bg);

    uint32_t panel_w = (g_width * 3) / 4;
    uint32_t panel_h = (g_height * 3) / 4;
    uint32_t panel_x = (g_width  - panel_w) / 2;
    uint32_t panel_y = (g_height - panel_h) / 2;

    fill_rect(panel_x, panel_y, panel_w, panel_h, panel);

    // Header: "LightOS 4"
    uint32_t header_x = panel_x + 40;
    uint32_t header_y = panel_y + 30;
    draw_text_lightos4(header_x, header_y, 0xFFFFFF);

    // Username field
    uint32_t field_w = panel_w - 80;
    uint32_t field_x = panel_x + 40;
    uint32_t field_y = header_y + 60;
    fill_rect(field_x, field_y, field_w, 28, input_bg);

    // Password field
    uint32_t field2_y = field_y + 50;
    fill_rect(field_x, field2_y, field_w, 28, input_bg);

    // "Remember this device" row (checkbox + fake text line)
    uint32_t cb_y = field2_y + 50;
    fill_rect(field_x, cb_y, 16, 16, input_bg);
    fill_rect(field_x + 26, cb_y + 4, field_w - 26, 8, 0x505050);

    // Continue button
    uint32_t btn_w = 140;
    uint32_t btn_h = 32;
    uint32_t btn_x = panel_x + panel_w - btn_w - 40;
    uint32_t btn_y = panel_y + panel_h - btn_h - 40;
    fill_rect(btn_x, btn_y, btn_w, btn_h, button_bg);
    // fake text label
    fill_rect(btn_x + 20, btn_y + 11, btn_w - 40, 10, 0xFFFFFF);
}

// ---------------------------------------------------------------------
// Login screen (for non-first boot)
// ---------------------------------------------------------------------

static void draw_login_screen(void) {
    const uint32_t bg        = 0x101020;
    const uint32_t panel     = 0x202030;
    const uint32_t input_bg  = 0xFFFFFF;
    const uint32_t button_bg = 0x0050A0;

    fill_rect(0, 0, g_width, g_height, bg);

    uint32_t panel_w = g_width / 3;
    if (panel_w < 260) panel_w = 260;
    uint32_t panel_h = g_height / 2;
    uint32_t panel_x = (g_width  - panel_w) / 2;
    uint32_t panel_y = (g_height - panel_h) / 2;

    fill_rect(panel_x, panel_y, panel_w, panel_h, panel);

    // Header: "LightOS 4"
    uint32_t header_x = panel_x + 30;
    uint32_t header_y = panel_y + 25;
    draw_text_lightos4(header_x, header_y, 0xFFFFFF);

    // Username / password boxes
    uint32_t field_w = panel_w - 60;
    uint32_t field_x = panel_x + 30;
    uint32_t field_y = header_y + 50;

    fill_rect(field_x, field_y,          field_w, 26, input_bg); // username
    fill_rect(field_x, field_y + 40,     field_w, 26, input_bg); // password

    // Login button
    uint32_t btn_w = field_w;
    uint32_t btn_h = 30;
    uint32_t btn_x = field_x;
    uint32_t btn_y = panel_y + panel_h - btn_h - 30;
    fill_rect(btn_x, btn_y, btn_w, btn_h, button_bg);
    fill_rect(btn_x + 20, btn_y + 10, btn_w - 40, 10, 0xFFFFFF);
}

// ---------------------------------------------------------------------
// Desktop: background + taskbar + icons + tray + one open window
// ---------------------------------------------------------------------

static void draw_desktop(void) {
    const uint32_t desktop_bg = 0x004080;   // blue-ish
    const uint32_t taskbar_bg = 0x202020;
    const uint32_t start_bg   = 0x404040;

    // Background
    fill_rect(0, 0, g_width, g_height, desktop_bg);

    // Taskbar (bottom, square corners)
    uint32_t taskbar_h = g_height / 14;
    if (taskbar_h < 32) taskbar_h = 32;
    uint32_t taskbar_y = g_height - taskbar_h;
    fill_rect(0, taskbar_y, g_width, taskbar_h, taskbar_bg);

    // Start button (square, no rounding)
    uint32_t start_w = 80;
    uint32_t start_h = taskbar_h - 8;
    uint32_t start_x = 4;
    uint32_t start_y = taskbar_y + 4;
    fill_rect(start_x, start_y, start_w, start_h, start_bg);

    // Simple 4-pane "LightOS" logo inside Start
    uint32_t pane_w = (start_w - 20) / 2;
    uint32_t pane_h = (start_h - 20) / 2;
    uint32_t pane_x = start_x + 10;
    uint32_t pane_y = start_y + 10;
    fill_rect(pane_x,               pane_y,               pane_w, pane_h, 0xFFFFFF);
    fill_rect(pane_x + pane_w + 2,  pane_y,               pane_w, pane_h, 0xFFFFFF);
    fill_rect(pane_x,               pane_y + pane_h + 2,  pane_w, pane_h, 0xFFFFFF);
    fill_rect(pane_x + pane_w + 2,  pane_y + pane_h + 2,  pane_w, pane_h, 0xFFFFFF);

    // Taskbar app icons (Settings, File Block, Command Block, Browser)
    uint32_t icon_size = taskbar_h - 12;
    uint32_t icon_y = taskbar_y + 6;
    uint32_t icon_x = start_x + start_w + 16;

    draw_icon_settings(icon_x, icon_y, icon_size);
    icon_x += icon_size + 8;
    draw_icon_files(icon_x, icon_y, icon_size);
    icon_x += icon_size + 8;
    draw_icon_cmd(icon_x, icon_y, icon_size);
    icon_x += icon_size + 8;
    draw_icon_browser(icon_x, icon_y, icon_size);

    // System tray (bottom-right)
    uint32_t tray_h = icon_size;
    uint32_t tray_y = icon_y;
    uint32_t right  = g_width - 8;

    // Clock block (time+date placeholder)
    draw_tray_clock(right, tray_y, tray_h);
    right -= tray_h * 2 + 6;

    // Battery
    draw_tray_battery(right, tray_y + 2, tray_h - 4);
    right -= tray_h * 2 + 6;

    // Speaker
    draw_tray_speaker(right, tray_y + 2, tray_h - 4);
    right -= tray_h + 6;

    // WiFi (furthest left in tray)
    draw_tray_wifi(right, tray_y + 2, tray_h - 4);

    // Desktop icons (left side, stacked)
    uint32_t d_icon_size = 64;
    uint32_t margin_x = 24;
    uint32_t margin_y = 24;
    uint32_t gap_y    = d_icon_size + 24;

    // Top to bottom: Settings, File Block, Command Block, Browser
    draw_icon_settings(margin_x, margin_y, d_icon_size);
    draw_icon_files(margin_x, margin_y + gap_y, d_icon_size);
    draw_icon_cmd(margin_x, margin_y + 2 * gap_y, d_icon_size);
    draw_icon_browser(margin_x, margin_y + 3 * gap_y, d_icon_size);

    // One active "app" window in the center (square corners)
    uint32_t win_w = g_width * 3 / 5;
    uint32_t win_h = g_height * 3 / 5;
    uint32_t win_x = (g_width  - win_w) / 2;
    uint32_t win_y = (g_height - win_h) / 2;
    if (win_y < 10) win_y = 10;

    draw_window_frame(win_x, win_y, win_w, win_h,
                      0x004080,   // title bar color
                      0xC0C0C0);  // window body

    // Some placeholder "content" lines inside the window
    uint32_t body_y = win_y + 30;
    for (int i = 0; i < 6; ++i) {
        fill_rect(win_x + 20,
                  body_y + (uint32_t)i * 24,
                  win_w - 40,
                  12,
                  0xE0E0E0);
    }
}

// ---------------------------------------------------------------------
// Kernel entry: called by UEFI bootloader.
// ---------------------------------------------------------------------

__attribute__((noreturn))
void kernel_main(BootInfo *bi) {
    // Framebuffer setup from UEFI-provided BootInfo
    g_fb     = (uint32_t*)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch;   // pixels per scanline

    // 1) Boot splash
    show_boot_animation();

    // 2) First-boot setup OR login screen (static, no real input yet)
#if FIRST_BOOT
    draw_setup_screen();
    delay(8000000);
#else
    draw_login_screen();
    delay(8000000);
#endif

    // 3) Desktop UI
    draw_desktop();

    // Kernel never returns.
    for (;;) {
        hlt();
    }
}
