// kernel/core/kernel.c
// LightOS 4 kernel: UEFI framebuffer desktop + boot logo + basic keyboard.

#include <stdint.h>
#include "boot.h"

static uint32_t *g_fb    = 0;
static uint32_t  g_width = 0;
static uint32_t  g_height = 0;
static uint32_t  g_pitch  = 0;

// ---------------------------------------------------------------------
// CPU / I/O helpers
// ---------------------------------------------------------------------

static inline void hlt(void) {
    __asm__ volatile("hlt");
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

// Non-blocking PS/2 keyboard poll.
// Returns 1 if a scancode was read into *sc, 0 if no key available.
static int keyboard_poll(uint8_t *sc) {
    uint8_t status = inb(0x64);
    if ((status & 0x01) == 0) {
        return 0; // no data
    }
    *sc = inb(0x60);
    return 1;
}

// ---------------------------------------------------------------------
// Basic drawing primitives
// ---------------------------------------------------------------------

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_width || y >= g_height) return;
    g_fb[(uint64_t)y * g_pitch + x] = color;
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

static void fill_circle(uint32_t cx, uint32_t cy, uint32_t r, uint32_t color) {
    for (int32_t y = -(int32_t)r; y <= (int32_t)r; ++y) {
        for (int32_t x = -(int32_t)r; x <= (int32_t)r; ++x) {
            if (x*x + y*y <= (int32_t)r*(int32_t)r) {
                put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

// ---------------------------------------------------------------------
// Tiny 8×8 bitmap font for boot text + tray text
// Only includes chars we actually use: L i g h t O S 4 space
// ---------------------------------------------------------------------

typedef struct {
    char c;
    uint8_t rows[8];
} Glyph8;

static const Glyph8 FONT8[] = {
    // 'L'
    { 'L', {
        0b10000000,
        0b10000000,
        0b10000000,
        0b10000000,
        0b10000000,
        0b10000000,
        0b11111110,
        0b00000000
    }},
    // 'i'
    { 'i', {
        0b00000000,
        0b00100000,
        0b00000000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00000000
    }},
    // 'g'
    { 'g', {
        0b00000000,
        0b00111100,
        0b01000100,
        0b01000100,
        0b00111100,
        0b00000100,
        0b00111000,
        0b00000000
    }},
    // 'h'
    { 'h', {
        0b10000000,
        0b10000000,
        0b10111000,
        0b11000100,
        0b10000100,
        0b10000100,
        0b10000100,
        0b00000000
    }},
    // 't'
    { 't', {
        0b00100000,
        0b00100000,
        0b11111000,
        0b00100000,
        0b00100000,
        0b00100000,
        0b00011000,
        0b00000000
    }},
    // 'O'
    { 'O', {
        0b00111000,
        0b01000100,
        0b10000010,
        0b10000010,
        0b10000010,
        0b01000100,
        0b00111000,
        0b00000000
    }},
    // 'S'
    { 'S', {
        0b00111100,
        0b01000000,
        0b01000000,
        0b00111100,
        0b00000010,
        0b00000010,
        0b00111100,
        0b00000000
    }},
    // '4'
    { '4', {
        0b00011000,
        0b00101000,
        0b01001000,
        0b11111100,
        0b00001000,
        0b00001000,
        0b00001000,
        0b00000000
    }},
    // space
    { ' ', {
        0,0,0,0,0,0,0,0
    }},
};

static const uint8_t* glyph_for(char c) {
    for (unsigned i = 0; i < sizeof(FONT8)/sizeof(FONT8[0]); ++i) {
        if (FONT8[i].c == c) return FONT8[i].rows;
    }
    // unknown -> blank
    return FONT8[sizeof(FONT8)/sizeof(FONT8[0]) - 1].rows; // space
}

static void draw_char8(uint32_t x, uint32_t y,
                       char c, uint32_t color)
{
    const uint8_t *rows = glyph_for(c);
    for (uint32_t row = 0; row < 8; ++row) {
        uint8_t bits = rows[row];
        for (uint32_t col = 0; col < 8; ++col) {
            if (bits & (0x80 >> col)) {
                put_pixel(x + col, y + row, color);
            }
        }
    }
}

static void draw_text(uint32_t x, uint32_t y,
                      const char *s, uint32_t color, uint32_t scale)
{
    uint32_t cx = x;
    uint32_t cy = y;
    while (*s) {
        if (*s == '\n') {
            cx  = x;
            cy += 8 * scale + 2;
            ++s;
            continue;
        }
        // draw scaled char
        const uint8_t *rows = glyph_for(*s);
        for (uint32_t row = 0; row < 8; ++row) {
            uint8_t bits = rows[row];
            for (uint32_t col = 0; col < 8; ++col) {
                if (bits & (0x80 >> col)) {
                    for (uint32_t dy = 0; dy < scale; ++dy) {
                        for (uint32_t dx = 0; dx < scale; ++dx) {
                            put_pixel(cx + col*scale + dx,
                                      cy + row*scale + dy,
                                      color);
                        }
                    }
                }
            }
        }
        cx += 8 * scale;
        ++s;
    }
}

// ---------------------------------------------------------------------
// Boot splash: light bulb + "LightOS 4" + spinner
// ---------------------------------------------------------------------

static void draw_bulb(uint32_t cx, uint32_t cy, uint32_t r,
                      uint32_t body, uint32_t outline, uint32_t base)
{
    if (r < 16) r = 16;

    // bulb
    fill_circle(cx, cy, r, outline);
    if (r > 3) fill_circle(cx, cy, r - 3, body);

    // base / socket
    uint32_t base_w = (r * 4) / 3;
    if (base_w < 24) base_w = 24;
    uint32_t base_h = r / 2;
    if (base_h < 10) base_h = 10;

    uint32_t base_x = cx - base_w / 2;
    uint32_t base_y = cy + r + 4;
    fill_rect(base_x, base_y, base_w, base_h, base);
}

// 8-dot spinner
static void draw_spinner_frame(uint32_t cx, uint32_t cy,
                               uint32_t radius, uint32_t active,
                               uint32_t bg)
{
    const int8_t dx[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };
    const int8_t dy[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };

    uint32_t dot = radius / 3;
    if (dot < 3) dot = 3;

    // clear region
    uint32_t box = radius * 2 + dot * 2;
    fill_rect(cx - box/2, cy - box/2, box, box, bg);

    for (uint32_t i = 0; i < 8; ++i) {
        int32_t px = (int32_t)cx + dx[i] * (int32_t)radius;
        int32_t py = (int32_t)cy + dy[i] * (int32_t)radius;
        uint32_t col = (i == (active & 7)) ? 0xFFFFFF : 0x505060;
        fill_rect((uint32_t)(px - (int32_t)(dot/2)),
                  (uint32_t)(py - (int32_t)(dot/2)),
                  dot, dot, col);
    }
}

static void draw_boot_sequence(void) {
    const uint32_t bg           = 0x101020;
    const uint32_t bulb_body    = 0xFFF7CC;
    const uint32_t bulb_outline = 0xD0C080;
    const uint32_t bulb_base    = 0x303040;

    uint32_t cx = g_width  / 2;
    uint32_t cy = g_height / 3;
    uint32_t r  = g_height / 12;
    if (r < 40) r = 40;

    // positions: bulb center at (cx,cy)
    // text below bulb + base
    uint32_t text_scale = 3;
    uint32_t text_h     = 8 * text_scale;
    uint32_t title_y    = cy + r + 20;
    uint32_t text_w     = 9 * 8 * text_scale; // "LightOS 4" = 9 chars
    uint32_t title_x    = (cx > text_w/2) ? cx - text_w/2 : 0;

    uint32_t spinner_r  = r / 2;
    uint32_t spinner_y  = title_y + text_h + 20; // moved well under text

    for (uint32_t frame = 0; frame < 48; ++frame) {
        fill_rect(0, 0, g_width, g_height, bg);

        draw_bulb(cx, cy, r, bulb_body, bulb_outline, bulb_base);
        draw_text(title_x, title_y, "LightOS 4", 0xFFFFFF, text_scale);
        draw_spinner_frame(cx, spinner_y, spinner_r, frame, bg);

        // crude delay per frame
        for (volatile uint64_t wait = 0; wait < 15000000ULL; ++wait) {
            __asm__ volatile("");
        }
    }
}

// ---------------------------------------------------------------------
// Desktop: tray icons, app icons, main window
// ---------------------------------------------------------------------

static void draw_wifi_icon(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t bar_h = size / 5; if (bar_h < 2) bar_h = 2;
    fill_rect(x,              y + size - bar_h,      size / 4, bar_h, 0xFFFFFF);
    fill_rect(x + size / 3,   y + size - 2 * bar_h,  size / 4, bar_h, 0xFFFFFF);
    fill_rect(x + 2 * size/3, y + size - 3 * bar_h,  size / 4, bar_h, 0xFFFFFF);
}

static void draw_battery_icon(uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h) {
    if (w < 10) w = 10;
    if (h < 6)  h = 6;
    uint32_t border = 2;

    fill_rect(x, y, w, h, 0xFFFFFF);
    fill_rect(x + border, y + border,
              w - 2*border, h - 2*border, 0x202020);

    uint32_t inner_w = w - 2*border;
    uint32_t fill_w  = inner_w * 4 / 5;
    fill_rect(x + border, y + border,
              fill_w, h - 2*border, 0x00C000);

    uint32_t nub_w = w / 8; if (nub_w < 2) nub_w = 2;
    fill_rect(x + w, y + h/3, nub_w, h/3, 0xFFFFFF);
}

static void draw_speaker_icon(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t box = size / 2; if (box < 4) box = 4;
    fill_rect(x, y + (size - box)/2, box, box, 0xFFFFFF);
    fill_rect(x + box, y + (size - box)/2 + box/4,
              box/2, box/2, 0xFFFFFF);
}

static void draw_clock_box(uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h) {
    fill_rect(x, y, w, h, 0x404040);
    // top line: time string
    draw_text(x + 4, y + 2, "12:34", 0xFFFFFF, 1);
    // bottom line: date string
    draw_text(x + 4, y + h/2, "2026-01-19", 0xC0C0C0, 1);
}

// Left column of app icons; 'selected' gets a brighter frame.
static void draw_app_icons(int selected) {
    uint32_t icon_w = g_width / 18;
    uint32_t icon_h = g_height / 11;
    if (icon_w < 40) icon_w = 40;
    if (icon_h < 40) icon_h = 40;

    uint32_t gap = icon_h / 5;
    uint32_t x   = icon_w / 2;
    uint32_t y   = icon_h / 2;

    uint32_t panel_bg = 0x003366;
    fill_rect(0, 0, x + icon_w + gap, g_height, panel_bg);

    for (int idx = 0; idx < 5; ++idx) {
        uint32_t bg = (idx == selected) ? 0x4C7AB5 : 0x234567;
        fill_rect(x, y, icon_w, icon_h, bg);

        uint32_t inner_x = x + icon_w / 8;
        uint32_t inner_y = y + icon_h / 8;
        uint32_t inner_w = icon_w * 3 / 4;
        uint32_t inner_h = icon_h * 3 / 4;

        switch (idx) {
            case 0: // Settings
                fill_rect(inner_x, inner_y, inner_w, inner_h, 0xE0E0E0);
                fill_rect(inner_x + inner_w/4, inner_y + inner_h/4,
                          inner_w/2, inner_h/2, bg);
                break;
            case 1: // File Block
                fill_rect(inner_x, inner_y + inner_h/4,
                          inner_w, inner_h*3/4, 0xFFE79C);
                fill_rect(inner_x, inner_y,
                          inner_w/2, inner_h/3, 0xFFE79C);
                break;
            case 2: // Command Block
                fill_rect(inner_x, inner_y, inner_w, inner_h, 0x000000);
                fill_rect(inner_x + inner_w/6,
                          inner_y + inner_h/2,
                          inner_w/5, inner_h/10, 0x00FF00);
                break;
            case 3: { // Browser
                uint32_t cx = inner_x + inner_w/2;
                uint32_t cy = inner_y + inner_h/2;
                uint32_t r  = inner_h/3;
                for (int32_t yy = -(int32_t)r; yy <= (int32_t)r; ++yy) {
                    for (int32_t xx = -(int32_t)r; xx <= (int32_t)r; ++xx) {
                        if (xx*xx + yy*yy <= (int32_t)r*(int32_t)r) {
                            put_pixel(cx + xx, cy + yy, 0x3399FF);
                        }
                    }
                }
                break;
            }
            case 4: // App Store
                fill_rect(inner_x, inner_y + inner_h/3,
                          inner_w, inner_h*2/3, 0xFFFFFF);
                fill_rect(inner_x + inner_w/4,
                          inner_y + inner_h/3 - inner_h/5,
                          inner_w/2, inner_h/5, 0xFFFFFF);
                break;
        }

        y += icon_h + gap;
    }
}

// Central window; color depends on open_app (-1 = neutral)
static void draw_main_window(int open_app) {
    uint32_t win_w = (g_width * 3) / 5;
    uint32_t win_h = (g_height * 3) / 5;
    uint32_t win_x = (g_width - win_w) / 2;
    uint32_t win_y = (g_height - win_h) / 2;
    if (win_y < 10) win_y = 10;

    uint32_t border_col = 0x000000;
    uint32_t title_col  = 0x004080;
    uint32_t body_col   = 0xC0C0C0;

    switch (open_app) {
        case 0: body_col = 0xCCE8FF; title_col = 0x0055AA; break; // Settings
        case 1: body_col = 0xFFF0C0; title_col = 0xAA7700; break; // File Block
        case 2: body_col = 0x101010; title_col = 0x202020; break; // Command
        case 3: body_col = 0xE0F4FF; title_col = 0x0066BB; break; // Browser
        case 4: body_col = 0xF4E0FF; title_col = 0x664488; break; // App Store
        default: break;
    }

    // border
    fill_rect(win_x - 1, win_y - 1, win_w + 2, win_h + 2, border_col);
    // title bar
    uint32_t title_h = 28;
    fill_rect(win_x, win_y, win_w, title_h, title_col);
    // body
    fill_rect(win_x, win_y + title_h, win_w, win_h - title_h, body_col);

    // close button
    uint32_t btn = (title_h > 8) ? (title_h - 8) : (title_h/2);
    uint32_t btn_x = win_x + win_w - btn - 4;
    uint32_t btn_y = win_y + (title_h - btn)/2;
    fill_rect(btn_x, btn_y, btn, btn, 0x800000);
}

static void draw_desktop(int selected_icon, int open_app) {
    uint32_t desktop_bg = 0x003366;
    uint32_t panel_bg   = 0x202020;

    // background
    fill_rect(0, 0, g_width, g_height, desktop_bg);

    // taskbar
    uint32_t panel_h = g_height / 12;
    if (panel_h < 40) panel_h = 40;
    uint32_t panel_y = g_height - panel_h;
    fill_rect(0, panel_y, g_width, panel_h, panel_bg);

    // start block (left)
    uint32_t start_w = panel_h;
    fill_rect(0, panel_y, start_w, panel_h, 0x404040);

    // tray (right)
    uint32_t tray_w = g_width / 4;
    if (tray_w < 200) tray_w = 200;
    uint32_t tray_x = g_width - tray_w;
    fill_rect(tray_x, panel_y, tray_w, panel_h, 0x303030);

    // tray icons
    uint32_t icon_size = panel_h / 2;
    if (icon_size < 16) icon_size = 16;
    uint32_t icon_y = panel_y + (panel_h - icon_size)/2;

    uint32_t cursor_x = tray_x + tray_w - icon_size - 8;
    draw_wifi_icon(cursor_x, icon_y, icon_size);
    cursor_x -= icon_size + 8;

    draw_speaker_icon(cursor_x, icon_y, icon_size);
    cursor_x -= icon_size + 8;

    draw_battery_icon(cursor_x, icon_y, icon_size * 3/2, icon_size);
    cursor_x -= icon_size * 2;

    // clock box on left side of tray
    uint32_t clock_w = tray_w / 3;
    uint32_t clock_h = icon_size + 6;
    uint32_t clock_x = tray_x + 8;
    uint32_t clock_y = panel_y + (panel_h - clock_h)/2;
    draw_clock_box(clock_x, clock_y, clock_w, clock_h);

    // app icons + main window
    draw_app_icons(selected_icon);
    draw_main_window(open_app);
}

// ---------------------------------------------------------------------
// Keyboard handling: arrows + Enter/Esc
// ---------------------------------------------------------------------

static void handle_scancode(uint8_t sc,
                            int *selected_icon,
                            int *open_app)
{
    static uint8_t ext = 0;

    if (sc == 0xE0) {
        ext = 1;
        return;
    }

    if (ext) {
        uint8_t code = sc & 0x7F;
        if (!(sc & 0x80)) {
            switch (code) {
                case 0x48: // Up
                    if (*selected_icon > 0) (*selected_icon)--;
                    else *selected_icon = 4;
                    break;
                case 0x50: // Down
                    if (*selected_icon < 4) (*selected_icon)++;
                    else *selected_icon = 0;
                    break;
                default:
                    break;
            }
        }
        ext = 0;
        return;
    }

    if (sc & 0x80) return; // key release

    switch (sc) {
        case 0x1C: // Enter
            *open_app = *selected_icon;
            break;
        case 0x01: // Esc
            *open_app = -1;
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------
// Kernel entry
// ---------------------------------------------------------------------

__attribute__((noreturn, section(".entry")))
void kernel_main(BootInfo *bi) {
    g_fb     = (uint32_t *)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch;

    // 1) Boot animation: light bulb + "LightOS 4" + spinner (no overlap)
    draw_boot_sequence();

    // 2) Desktop + basic keyboard loop
    int selected_icon = 0;   // 0..4
    int open_app      = -1;  // -1 = neutral

    draw_desktop(selected_icon, open_app);

    for (;;) {
        uint8_t sc;
        if (keyboard_poll(&sc)) {
            int prev_sel  = selected_icon;
            int prev_open = open_app;

            handle_scancode(sc, &selected_icon, &open_app);

            if (selected_icon != prev_sel || open_app != prev_open) {
                draw_desktop(selected_icon, open_app);
            }
        }
        // (Optional) add hlt() occasionally to save CPU.
    }
}
