// kernel/core/kernel.c
// LightOS 4 - UEFI framebuffer kernel: boot splash + desktop + Command Block

#include <stdint.h>
#include "boot.h"

// ---------------------------------------------------------------------
// Global framebuffer
// ---------------------------------------------------------------------

static uint32_t *g_fb    = 0;
static uint32_t  g_width = 0;
static uint32_t  g_height = 0;
static uint32_t  g_pitch  = 0;  // pixels per row

// ---------------------------------------------------------------------
// Basic drawing
// ---------------------------------------------------------------------

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_width || y >= g_height) return;
    g_fb[(uint64_t)y * g_pitch + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h,
                      uint32_t color) {
    if (x >= g_width || y >= g_height) return;
    if (x + w > g_width)  w = g_width  - x;
    if (y + h > g_height) h = g_height - y;

    for (uint32_t yy = 0; yy < h; ++yy) {
        uint32_t *row = g_fb + (uint64_t)(y + yy) * g_pitch;
        for (uint32_t xx = 0; xx < w; ++xx) {
            row[x + xx] = color;
        }
    }
}

// Slow but simple circle fill: O(r^2), fine for small r
static void fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    if (r <= 0) return;
    int32_t r2 = r * r;

    for (int32_t dy = -r; dy <= r; ++dy) {
        int32_t yy = cy + dy;
        if (yy < 0 || yy >= (int32_t)g_height) continue;

        for (int32_t dx = -r; dx <= r; ++dx) {
            int32_t xx = cx + dx;
            if (xx < 0 || xx >= (int32_t)g_width) continue;

            if (dx*dx + dy*dy <= r2) {
                g_fb[(uint64_t)yy * g_pitch + (uint32_t)xx] = color;
            }
        }
    }
}

// ---------------------------------------------------------------------
// Tiny 8×8 bitmap font
// ---------------------------------------------------------------------

typedef struct {
    char    c;
    uint8_t rows[8];
} Glyph8;

// Simple 5×7-ish glyphs centered in 8×8; good enough to be readable.
static const Glyph8 FONT8[] = {
    { ' ', { 0,0,0,0,0,0,0,0 } },
    { '.', { 0,0,0,0,0,0,0b00011000,0 } },
    { ':', { 0,0b00011000,0b00011000,0,0b00011000,0b00011000,0,0 } },
    { '-', { 0,0,0,0b00111100,0,0,0,0 } },
    { '\'', { 0b00010000,0b00010000,0b00010000,0,0,0,0,0 } },
    { '0', { 0b00011100,0b00100010,0b00100110,0b00101010,
             0b00110010,0b00100010,0b00011100,0 } },
    { '1', { 0b00001000,0b00011000,0b00001000,0b00001000,
             0b00001000,0b00001000,0b00011100,0 } },
    { '2', { 0b00011100,0b00100010,0b00000010,0b00001100,
             0b00010000,0b00100000,0b00111110,0 } },
    { '3', { 0b00111100,0b00000010,0b00000010,0b00011100,
             0b00000010,0b00000010,0b00111100,0 } },
    { '4', { 0b00010010,0b00010010,0b00010010,0b00111110,
             0b00000010,0b00000010,0b00000010,0 } },
    { '5', { 0b00111110,0b00100000,0b00111100,0b00000010,
             0b00000010,0b00100010,0b00011100,0 } },
    { '6', { 0b00011100,0b00100000,0b00100000,0b00111100,
             0b00100010,0b00100010,0b00011100,0 } },
    { '7', { 0b00111110,0b00000010,0b00000100,0b00001000,
             0b00010000,0b00010000,0b00010000,0 } },
    { '8', { 0b00011100,0b00100010,0b00100010,0b00011100,
             0b00100010,0b00100010,0b00011100,0 } },
    { '9', { 0b00011100,0b00100010,0b00100010,0b00011110,
             0b00000010,0b00000010,0b00011100,0 } },
    { 'A', { 0b00011100,0b00100010,0b00100010,0b00111110,
             0b00100010,0b00100010,0b00100010,0 } },
    { 'B', { 0b00111100,0b00100010,0b00100010,0b00111100,
             0b00100010,0b00100010,0b00111100,0 } },
    { 'C', { 0b00011100,0b00100010,0b00100000,0b00100000,
             0b00100000,0b00100010,0b00011100,0 } },
    { 'D', { 0b00111000,0b00100100,0b00100010,0b00100010,
             0b00100010,0b00100100,0b00111000,0 } },
    { 'E', { 0b00111110,0b00100000,0b00100000,0b00111100,
             0b00100000,0b00100000,0b00111110,0 } },
    { 'F', { 0b00111110,0b00100000,0b00100000,0b00111100,
             0b00100000,0b00100000,0b00100000,0 } },
    { 'G', { 0b00011100,0b00100010,0b00100000,0b00101110,
             0b00100010,0b00100010,0b00011100,0 } },
    { 'H', { 0b00100010,0b00100010,0b00100010,0b00111110,
             0b00100010,0b00100010,0b00100010,0 } },
    { 'I', { 0b00011100,0b00001000,0b00001000,0b00001000,
             0b00001000,0b00001000,0b00011100,0 } },
    { 'J', { 0b00001110,0b00000100,0b00000100,0b00000100,
             0b00100100,0b00100100,0b00011000,0 } },
    { 'K', { 0b00100010,0b00100100,0b00101000,0b00110000,
             0b00101000,0b00100100,0b00100010,0 } },
    { 'L', { 0b00100000,0b00100000,0b00100000,0b00100000,
             0b00100000,0b00100000,0b00111110,0 } },
    { 'M', { 0b00100010,0b00110110,0b00101010,0b00101010,
             0b00100010,0b00100010,0b00100010,0 } },
    { 'N', { 0b00100010,0b00110010,0b00101010,0b00100110,
             0b00100010,0b00100010,0b00100010,0 } },
    { 'O', { 0b00011100,0b00100010,0b00100010,0b00100010,
             0b00100010,0b00100010,0b00011100,0 } },
    { 'P', { 0b00111100,0b00100010,0b00100010,0b00111100,
             0b00100000,0b00100000,0b00100000,0 } },
    { 'Q', { 0b00011100,0b00100010,0b00100010,0b00100010,
             0b00101010,0b00100110,0b00011110,0 } },
    { 'R', { 0b00111100,0b00100010,0b00100010,0b00111100,
             0b00101000,0b00100100,0b00100010,0 } },
    { 'S', { 0b00011110,0b00100000,0b00100000,0b00011100,
             0b00000010,0b00000010,0b00111100,0 } },
    { 'T', { 0b00111110,0b00001000,0b00001000,0b00001000,
             0b00001000,0b00001000,0b00001000,0 } },
    { 'U', { 0b00100010,0b00100010,0b00100010,0b00100010,
             0b00100010,0b00100010,0b00011100,0 } },
    { 'V', { 0b00100010,0b00100010,0b00100010,0b00100010,
             0b00010100,0b00010100,0b00001000,0 } },
    { 'W', { 0b00100010,0b00100010,0b00100010,0b00101010,
             0b00101010,0b00110110,0b00100010,0 } },
    { 'X', { 0b00100010,0b00100010,0b00010100,0b00001000,
             0b00010100,0b00100010,0b00100010,0 } },
    { 'Y', { 0b00100010,0b00100010,0b00010100,0b00001000,
             0b00001000,0b00001000,0b00001000,0 } },
    { 'Z', { 0b00111110,0b00000010,0b00000100,0b00001000,
             0b00010000,0b00100000,0b00111110,0 } },
};

static const uint8_t *font_lookup(char c) {
    // lowercase → uppercase
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    uint32_t n = (uint32_t)(sizeof(FONT8) / sizeof(FONT8[0]));
    for (uint32_t i = 0; i < n; ++i) {
        if (FONT8[i].c == c) return FONT8[i].rows;
    }

    // fallback to 'Z' (arbitrary) if not found
    for (uint32_t i = 0; i < n; ++i) {
        if (FONT8[i].c == 'Z') return FONT8[i].rows;
    }
    return FONT8[0].rows;
}

static void draw_char(uint32_t x, uint32_t y,
                      char c, uint32_t color, uint32_t scale) {
    const uint8_t *rows = font_lookup(c);
    for (uint32_t row = 0; row < 8; ++row) {
        uint8_t bits = rows[row];
        for (uint32_t col = 0; col < 8; ++col) {
            if (bits & (1u << (7 - col))) {
                for (uint32_t dy = 0; dy < scale; ++dy) {
                    for (uint32_t dx = 0; dx < scale; ++dx) {
                        put_pixel(x + col * scale + dx,
                                  y + row * scale + dy,
                                  color);
                    }
                }
            }
        }
    }
}

static void draw_text(uint32_t x, uint32_t y,
                      const char *s, uint32_t color, uint32_t scale) {
    uint32_t cx = x;
    while (s && *s) {
        if (*s == '\n') {
            cx = x;
            y += 8 * scale + 2;
        } else {
            draw_char(cx, y, *s, color, scale);
            cx += 8 * scale;
        }
        ++s;
    }
}

// ---------------------------------------------------------------------
// Boot splash: bulb + "LightOS 4" + spinner UNDER text
// ---------------------------------------------------------------------

static void draw_bulb(uint32_t cx, uint32_t cy, uint32_t r,
                      uint32_t body, uint32_t outline, uint32_t base) {
    if (r < 32) r = 32;

    // main circle
    fill_circle((int32_t)cx, (int32_t)cy, (int32_t)r, body);

    // simple outer ring
    int32_t or = (int32_t)r + 2;
    int32_t r2 = (int32_t)r * (int32_t)r;
    int32_t or2 = or * or;
    for (int32_t dy = -or; dy <= or; ++dy) {
        for (int32_t dx = -or; dx <= or; ++dx) {
            int32_t xx = (int32_t)cx + dx;
            int32_t yy = (int32_t)cy + dy;
            if (xx < 0 || yy < 0 ||
                xx >= (int32_t)g_width ||
                yy >= (int32_t)g_height) continue;
            int32_t d2 = dx*dx + dy*dy;
            if (d2 <= or2 && d2 >= r2) {
                g_fb[(uint64_t)yy * g_pitch + (uint32_t)xx] = outline;
            }
        }
    }

    // base
    uint32_t base_w = r * 4 / 3;
    uint32_t base_h = r / 3;
    if (base_h < 12) base_h = 12;
    uint32_t base_x = cx - base_w / 2;
    uint32_t base_y = cy + r + 8;
    fill_rect(base_x, base_y, base_w, base_h, base);
}

static void draw_spinner_frame(uint32_t cx, uint32_t cy,
                               uint32_t radius, uint32_t frame,
                               uint32_t bg_color) {
    (void)bg_color;

    static const int8_t dx[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };
    static const int8_t dy[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };

    uint32_t active = frame & 7;
    uint32_t dot = radius / 3;
    if (dot < 4) dot = 4;

    for (uint32_t i = 0; i < 8; ++i) {
        int32_t px = (int32_t)cx + dx[i] * (int32_t)radius;
        int32_t py = (int32_t)cy + dy[i] * (int32_t)radius;
        uint32_t col = (i == active) ? 0xFFFFFFu : 0x505060u;
        fill_rect((uint32_t)(px - (int32_t)dot/2),
                  (uint32_t)(py - (int32_t)dot/2),
                  dot, dot, col);
    }
}

static void run_boot_splash(void) {
    const uint32_t bg           = 0x101020;
    const uint32_t bulb_body    = 0xFFF7CC;
    const uint32_t bulb_outline = 0xD0C080;
    const uint32_t bulb_base    = 0x303040;

    fill_rect(0, 0, g_width, g_height, bg);

    uint32_t cx = g_width  / 2;
    uint32_t cy = g_height / 3;
    uint32_t r  = g_height / 10;
    if (r < 50) r = 50;

    uint32_t text_scale = 3;
    uint32_t text_h     = 8 * text_scale;

    uint32_t base_h      = r / 3;
    if (base_h < 12) base_h = 12;
    uint32_t base_bottom = cy + r + 8 + base_h;

    uint32_t title_y   = base_bottom + 24;          // below bulb
    const char *title  = "LightOS 4";
    uint32_t text_w    = 9 * 8 * text_scale;        // approximate
    uint32_t title_x   = (cx > text_w / 2) ? cx - text_w / 2 : 0;

    uint32_t spinner_r = r / 2;
    if (spinner_r < 20) spinner_r = 20;
    uint32_t spinner_y = title_y + text_h + 64;     // further below text

    for (uint32_t frame = 0; frame < 40; ++frame) {
        fill_rect(0, 0, g_width, g_height, bg);
        draw_bulb(cx, cy, r, bulb_body, bulb_outline, bulb_base);
        draw_text(title_x, title_y, title, 0xFFFFFFu, text_scale);
        draw_spinner_frame(cx, spinner_y, spinner_r, frame, bg);

        // crude delay loop
        for (volatile uint64_t w = 0; w < 9000000ULL; ++w) {
            __asm__ volatile("");
        }
    }
}

// ---------------------------------------------------------------------
// Keyboard (PS/2) poll
// ---------------------------------------------------------------------

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static int keyboard_poll(uint8_t *sc) {
    if ((inb(0x64) & 0x01) == 0) return 0;
    *sc = inb(0x60);
    return 1;
}

// ---------------------------------------------------------------------
// Command Block terminal
// ---------------------------------------------------------------------

#define TERM_MAX_LINES 32
#define TERM_MAX_COLS  72

typedef struct {
    char     lines[TERM_MAX_LINES][TERM_MAX_COLS];
    uint32_t line_count;
    char     input[TERM_MAX_COLS];
    uint32_t input_len;
} TerminalState;

static TerminalState g_term;

static void str_copy(char *dst, const char *src, uint32_t max_len) {
    if (!dst || !src || max_len == 0) return;
    uint32_t i = 0;
    for (; i + 1 < max_len && src[i]; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int str_starts_with(const char *s, const char *pfx) {
    if (!s || !pfx) return 0;
    while (*pfx) {
        if (*s != *pfx) return 0;
        ++s; ++pfx;
    }
    return 1;
}

static void term_add_line(TerminalState *t, const char *text) {
    if (!t) return;
    if (t->line_count >= TERM_MAX_LINES) {
        // scroll up
        for (uint32_t i = 1; i < TERM_MAX_LINES; ++i) {
            str_copy(t->lines[i - 1], t->lines[i], TERM_MAX_COLS);
        }
        t->line_count = TERM_MAX_LINES - 1;
    }
    str_copy(t->lines[t->line_count], text ? text : "", TERM_MAX_COLS);
    t->line_count++;
}

static void term_reset(TerminalState *t) {
    t->line_count = 0;
    t->input_len  = 0;
    t->input[0]   = '\0';

    term_add_line(t, "LightOS 4 Command Block");
    term_add_line(t, "Type 'help' for commands.");
    term_add_line(t, "");
}

static void term_execute_command(TerminalState *t, const char *cmd) {
    if (!cmd || !*cmd) return;

    // Skip leading whitespace so commands like "   help" still work.
    while (*cmd == ' ' || *cmd == '\t') {
        ++cmd;
    }
    if (!*cmd) return;

    if (str_starts_with(cmd, "help")) {
        term_add_line(t, "Supported commands:");
        term_add_line(t, "  help");
        term_add_line(t, "  cls / clear");
        term_add_line(t, "  dir / ls");
        term_add_line(t, "  mkdir / md");
        term_add_line(t, "  rmdir / rd");
        term_add_line(t, "  del / rm");
        term_add_line(t, "  copy / cp");
        term_add_line(t, "  move / mv");
        term_add_line(t, "  ver / uname");
        term_add_line(t, "  time / date");
        term_add_line(t, "  echo <text>");
        term_add_line(t, "  run <program> [args]");
        return;
    }

    if (str_starts_with(cmd, "cls") || str_starts_with(cmd, "clear")) {
        term_reset(t);
        return;
    }

    if (str_starts_with(cmd, "dir") || str_starts_with(cmd, "ls")) {
        term_add_line(t, "Directory listing is not implemented yet (no filesystem).");
        return;
    }

    if (str_starts_with(cmd, "mkdir") || str_starts_with(cmd, "md")) {
        term_add_line(t, "mkdir: creating directories is not implemented yet.");
        term_add_line(t, "       (This is a demo kernel without a real filesystem.)");
        return;
    }

    if (str_starts_with(cmd, "rmdir") || str_starts_with(cmd, "rd")) {
        term_add_line(t, "rmdir: removing directories is not implemented yet.");
        return;
    }

    if (str_starts_with(cmd, "del") || str_starts_with(cmd, "rm")) {
        term_add_line(t, "del/rm: deleting files is not implemented yet.");
        return;
    }

    if (str_starts_with(cmd, "copy") || str_starts_with(cmd, "cp")) {
        term_add_line(t, "copy/cp: file copy is not implemented yet.");
        return;
    }

    if (str_starts_with(cmd, "move") || str_starts_with(cmd, "mv")) {
        term_add_line(t, "move/mv: file move/rename is not implemented yet.");
        return;
    }

    if (str_starts_with(cmd, "ver") || str_starts_with(cmd, "uname")) {
        term_add_line(t, "LightOS 4 demo kernel (x86_64, UEFI).");
        return;
    }

    if (str_starts_with(cmd, "time") || str_starts_with(cmd, "date")) {
        term_add_line(t, "2026-01-19 12:34 (static demo time).");
        return;
    }

    if (str_starts_with(cmd, "run")) {
        term_add_line(t, "run: program execution is not implemented yet.");
        term_add_line(t, "     (Running real .exe / ELF binaries will need a loader.)");
        return;
    }

    if (str_starts_with(cmd, "echo")) {
        const char *p = cmd + 4;
        if (*p == ' ') ++p;
        term_add_line(t, p);
        return;
    }

    term_add_line(t, "Unknown command. Type 'help'.");
}

static void term_handle_scancode(uint8_t sc, TerminalState *t,
                                 int *selected_icon, int *open_app) {
    (void)selected_icon; // unused while inside Command Block

    if (sc == 0xE0) return;      // ignore extended prefix
    if (sc & 0x80) return;       // ignore key releases

    if (sc == 0x01) {            // Esc
        *open_app = -1;
        return;
    }

    if (sc == 0x0E) {            // Backspace
        if (t->input_len > 0) {
            t->input_len--;
            t->input[t->input_len] = '\0';
        }
        return;
    }

    if (sc == 0x1C) {            // Enter
        t->input[t->input_len] = '\0';
        term_add_line(t, t->input);
        term_execute_command(t, t->input);
        t->input_len = 0;
        t->input[0]  = '\0';
        return;
    }

    // basic scancode → ASCII (no shift)
    char c = 0;
    switch (sc) {
        case 0x02: c = '1'; break;
        case 0x03: c = '2'; break;
        case 0x04: c = '3'; break;
        case 0x05: c = '4'; break;
        case 0x06: c = '5'; break;
        case 0x07: c = '6'; break;
        case 0x08: c = '7'; break;
        case 0x09: c = '8'; break;
        case 0x0A: c = '9'; break;
        case 0x0B: c = '0'; break;
        case 0x10: c = 'q'; break;
        case 0x11: c = 'w'; break;
        case 0x12: c = 'e'; break;
        case 0x13: c = 'r'; break;
        case 0x14: c = 't'; break;
        case 0x15: c = 'y'; break;
        case 0x16: c = 'u'; break;
        case 0x17: c = 'i'; break;
        case 0x18: c = 'o'; break;
        case 0x19: c = 'p'; break;
        case 0x1E: c = 'a'; break;
        case 0x1F: c = 's'; break;
        case 0x20: c = 'd'; break;
        case 0x21: c = 'f'; break;
        case 0x22: c = 'g'; break;
        case 0x23: c = 'h'; break;
        case 0x24: c = 'j'; break;
        case 0x25: c = 'k'; break;
        case 0x26: c = 'l'; break;
        case 0x2C: c = 'z'; break;
        case 0x2D: c = 'x'; break;
        case 0x2E: c = 'c'; break;
        case 0x2F: c = 'v'; break;
        case 0x30: c = 'b'; break;
        case 0x31: c = 'n'; break;
        case 0x32: c = 'm'; break;
        case 0x39: c = ' '; break;
        default: break;
    }
    if (c && t->input_len < TERM_MAX_COLS - 1) {
        t->input[t->input_len++] = c;
        t->input[t->input_len]   = '\0';
    }
}

// ---------------------------------------------------------------------
// Desktop UI
// ---------------------------------------------------------------------

static void draw_desktop_background(void) {
    fill_rect(0, 0, g_width, g_height, 0x003366u);
}

static void draw_taskbar(void) {
    uint32_t bar_h = g_height / 10;
    if (bar_h < 40) bar_h = 40;
    uint32_t y = g_height - bar_h;

    fill_rect(0, y, g_width, bar_h, 0x202020u);

    // clock + date (static demo)
    draw_text(g_width - 180, y + 6, "12:34", 0xFFFFFFu, 1);
    draw_text(g_width - 180, y + 22, "2026-01-19", 0xC0C0C0u, 1);

    // simple battery block
    uint32_t bx = g_width - 70;
    uint32_t by = y + 8;
    uint32_t bw = 40;
    uint32_t bh = 18;
    fill_rect(bx, by, bw, bh, 0xFFFFFFu);
    fill_rect(bx + 2, by + 2, bw - 4, bh - 4, 0x00C000u);
    fill_rect(bx + bw, by + 4, 4, bh - 8, 0xFFFFFFu);
}

static void draw_dock_icon(uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h,
                           uint32_t color) {
    fill_rect(x, y, w, h, color);
    // simple border
    for (uint32_t i = 0; i < w; ++i) {
        put_pixel(x + i, y, 0x000000u);
        put_pixel(x + i, y + h - 1, 0x000000u);
    }
    for (uint32_t j = 0; j < h; ++j) {
        put_pixel(x, y + j, 0x000000u);
        put_pixel(x + w - 1, y + j, 0x000000u);
    }
}

static void draw_icons_column(int selected_icon) {
    uint32_t icon_w = g_width / 16;
    if (icon_w < 40) icon_w = 40;
    uint32_t icon_h = icon_w;
    uint32_t gap    = icon_h / 4;

    uint32_t x = g_width / 40;
    uint32_t y = g_height / 10;

    for (int i = 0; i < 5; ++i) {
        uint32_t col = (i == selected_icon) ? 0xFFFFFFu : 0xAAAAAAu;
        draw_dock_icon(x, y, icon_w, icon_h, col);

        uint32_t ix = x + icon_w / 4;
        uint32_t iy = y + icon_h / 4;
        uint32_t iw = icon_w / 2;
        uint32_t ih = icon_h / 2;

        switch (i) {
            case 0: // Settings
                fill_rect(ix, iy, iw, ih, 0xCCCCCCu);
                break;
            case 1: // File Block
                fill_rect(ix, iy + ih/3, iw, ih*2/3, 0xFFE79Cu);
                fill_rect(ix, iy, iw/2, ih/3, 0xFFE79Cu);
                break;
            case 2: // Command Block
                fill_rect(ix, iy, iw, ih, 0x000000u);
                draw_text(ix + 2, iy + 2, ">", 0x00FF00u, 1);
                break;
            case 3: // Browser
                fill_circle((int32_t)(ix + iw/2),
                            (int32_t)(iy + ih/2),
                            (int32_t)(iw/2), 0x66AAFFu);
                break;
            case 4: // Extra
                fill_rect(ix, iy, iw, ih, 0xFFFFFFu);
                break;
        }

        y += icon_h + gap;
    }
}

static void draw_terminal_contents(uint32_t win_x, uint32_t win_y,
                                   uint32_t win_w, uint32_t win_h,
                                   uint32_t title_h) {
    (void)win_w; // not currently used

    uint32_t x = win_x + 10;
    uint32_t y = win_y + title_h + 10;

    for (uint32_t i = 0; i < g_term.line_count; ++i) {
        draw_text(x, y, g_term.lines[i], 0xFFFFFFu, 1);
        y += 12;
        if (y + 12 >= win_y + win_h) break;
    }

    if (y + 16 < win_y + win_h) {
        char buf[TERM_MAX_COLS];
        buf[0] = '>';
        buf[1] = ' ';
        uint32_t len = g_term.input_len;
        if (len > TERM_MAX_COLS - 3) len = TERM_MAX_COLS - 3;
        for (uint32_t i = 0; i < len; ++i) {
            buf[2 + i] = g_term.input[i];
        }
        buf[2 + len] = '_';
        buf[3 + len] = '\0';
        draw_text(x, y + 4, buf, 0x00FF00u, 1);
    }
}

static void draw_window(uint32_t win_x, uint32_t win_y,
                        uint32_t win_w, uint32_t win_h,
                        const char *title, int open_app) {
    uint32_t title_h = 26;

    // body
    fill_rect(win_x, win_y, win_w, win_h, 0x202020u);
    // border
    for (uint32_t i = 0; i < win_w; ++i) {
        put_pixel(win_x + i, win_y, 0x000000u);
        put_pixel(win_x + i, win_y + win_h - 1, 0x000000u);
    }
    for (uint32_t j = 0; j < win_h; ++j) {
        put_pixel(win_x, win_y + j, 0x000000u);
        put_pixel(win_x + win_w - 1, win_y + j, 0x000000u);
    }

    // title bar
    fill_rect(win_x, win_y, win_w, title_h, 0x303030u);
    draw_text(win_x + 8, win_y + 6, title, 0xFFFFFFu, 1);

    // close button
    uint32_t cb_w = 18;
    uint32_t cb_x = win_x + win_w - cb_w - 6;
    uint32_t cb_y = win_y + 4;
    fill_rect(cb_x, cb_y, cb_w, title_h - 8, 0x880000u);

    // content
    switch (open_app) {
        case 0:
            draw_text(win_x + 10, win_y + title_h + 10,
                      "Settings app is not implemented yet.",
                      0xFFFFFFu, 1);
            break;
        case 1:
            draw_text(win_x + 10, win_y + title_h + 10,
                      "File Block (file manager) not implemented yet.",
                      0xFFFFFFu, 1);
            break;
        case 2:
            draw_terminal_contents(win_x, win_y, win_w, win_h, title_h);
            break;
        case 3:
            draw_text(win_x + 10, win_y + title_h + 10,
                      "Browser not implemented yet.",
                      0xFFFFFFu, 1);
            break;
        case 4:
            draw_text(win_x + 10, win_y + title_h + 10,
                      "Extra app placeholder.",
                      0xFFFFFFu, 1);
            break;
        default:
            break;
    }
}

static void draw_desktop(int selected_icon, int open_app) {
    draw_desktop_background();
    draw_taskbar();
    draw_icons_column(selected_icon);

    if (open_app >= 0) {
        uint32_t win_w = g_width * 3 / 5;
        uint32_t win_h = g_height * 3 / 5;
        uint32_t win_x = (g_width  - win_w) / 2;
        uint32_t win_y = (g_height - win_h) / 2 - g_height / 20;

        const char *title = "Window";
        switch (open_app) {
            case 0: title = "LightOS 4 Settings"; break;
            case 1: title = "LightOS 4 File Block"; break;
            case 2: title = "LightOS 4 Command Block"; break;
            case 3: title = "LightOS 4 Browser"; break;
            case 4: title = "LightOS 4 Extra"; break;
        }

        draw_window(win_x, win_y, win_w, win_h, title, open_app);
    }
}

// ---------------------------------------------------------------------
// Desktop keyboard nav (outside Command Block)
// ---------------------------------------------------------------------

static void handle_nav_scancode(uint8_t sc,
                                int *selected_icon,
                                int *open_app) {
    if (sc == 0xE0) return;      // ignore extended
    if (sc & 0x80) return;       // ignore releases

    if (sc == 0x48) {            // Up
        if (*selected_icon > 0) (*selected_icon)--;
        return;
    }
    if (sc == 0x50) {            // Down
        if (*selected_icon < 4) (*selected_icon)++;
        return;
    }

    if (sc == 0x1C) {            // Enter
        if (*selected_icon >= 0 && *selected_icon <= 4) {
            *open_app = *selected_icon;
            if (*open_app == 2) term_reset(&g_term);
        }
        return;
    }

    if (sc == 0x01) {            // Esc
        *open_app = -1;
        return;
    }
}

// ---------------------------------------------------------------------
// Kernel entry + stub
// ---------------------------------------------------------------------

__attribute__((noreturn))
void kernel_main(BootInfo *bi) {
    g_fb     = (uint32_t*)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch ? bi->framebuffer_pitch : bi->framebuffer_width;

    run_boot_splash();

    int selected_icon = 2;  // highlight Command Block
    int open_app      = -1; // none open yet

    term_reset(&g_term);
    draw_desktop(selected_icon, open_app);

    for (;;) {
        uint8_t sc;
        if (keyboard_poll(&sc)) {
            int prev_sel  = selected_icon;
            int prev_open = open_app;

            if (open_app == 2) {
                term_handle_scancode(sc, &g_term, &selected_icon, &open_app);
            } else {
                handle_nav_scancode(sc, &selected_icon, &open_app);
            }

            if (selected_icon != prev_sel ||
                open_app      != prev_open ||
                open_app == 2) {
                draw_desktop(selected_icon, open_app);
            }
        }
    }
}

__attribute__((noreturn, section(".entry")))
void _start(BootInfo *bi) {
    kernel_main(bi);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

