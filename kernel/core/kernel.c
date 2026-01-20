// kernel/core/kernel.c
// LightOS 4 - simple UEFI framebuffer "kernel" with desktop + Command Block

#include <stdint.h>
#include "boot.h"

// ---------------------------------------------------------------------
// Global framebuffer info
// ---------------------------------------------------------------------

static uint32_t *g_fb      = 0;
static uint32_t  g_width   = 0;
static uint32_t  g_height  = 0;
static uint32_t  g_pitch   = 0;   // pixels per row

// ---------------------------------------------------------------------
// Basic drawing primitives
// ---------------------------------------------------------------------

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_width || y >= g_height) return;
    g_fb[y * g_pitch + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h,
                      uint32_t color) {
    if (x >= g_width || y >= g_height) return;
    if (x + w > g_width)  w = g_width  - x;
    if (y + h > g_height) h = g_height - y;

    for (uint32_t yy = 0; yy < h; ++yy) {
        uint32_t row = (y + yy) * g_pitch;
        for (uint32_t xx = 0; xx < w; ++xx) {
            g_fb[row + x + xx] = color;
        }
    }
}

static void draw_rect(uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h,
                      uint32_t color) {
    if (w == 0 || h == 0) return;
    if (x >= g_width || y >= g_height) return;

    uint32_t x2 = x + w - 1;
    uint32_t y2 = y + h - 1;

    if (x2 >= g_width)  x2 = g_width  - 1;
    if (y2 >= g_height) y2 = g_height - 1;

    for (uint32_t xx = x; xx <= x2; ++xx) {
        put_pixel(xx, y,  color);
        put_pixel(xx, y2, color);
    }

    for (uint32_t yy = y; yy <= y2; ++yy) {
        put_pixel(x,  yy, color);
        put_pixel(x2, yy, color);
    }
}

static void fill_circle(int32_t cx, int32_t cy,
                        int32_t r, uint32_t color) {
    if (r <= 0) return;
    int32_t r2 = r * r;

    for (int32_t dy = -r; dy <= r; ++dy) {
        int32_t yy = cy + dy;
        if (yy < 0 || yy >= (int32_t)g_height) continue;

        int32_t dx_limit_sq = r2 - dy * dy;
        if (dx_limit_sq < 0) continue;

        // simple integer sqrt using float, we don't need precision
        int32_t dx_limit = (int32_t)(dx_limit_sq > 0
            ? __builtin_sqrt((double)dx_limit_sq)
            : 0);

        int32_t start_x = cx - dx_limit;
        int32_t end_x   = cx + dx_limit;

        if (start_x < 0) start_x = 0;
        if (end_x >= (int32_t)g_width) end_x = (int32_t)g_width - 1;

        uint32_t row = yy * g_pitch;
        for (int32_t xx = start_x; xx <= end_x; ++xx) {
            g_fb[row + xx] = color;
        }
    }
}

// ---------------------------------------------------------------------
// Tiny 8x8 bitmap font for ASCII subset
// ---------------------------------------------------------------------

typedef struct {
    char    c;
    uint8_t rows[8];
} Glyph8;

static const Glyph8 FONT8[] = {
    { ' ', { 0b00000000, 0b00000000, 0b00000000, 0b00000000,
             0b00000000, 0b00000000, 0b00000000, 0b00000000 } },
    { '\'', { 0b00010000, 0b00010000, 0b00100000, 0b00000000,
              0b00000000, 0b00000000, 0b00000000, 0b00000000 } },
    { '-', { 0b00000000, 0b00000000, 0b00000000, 0b01111110,
             0b00000000, 0b00000000, 0b00000000, 0b00000000 } },
    { '.', { 0b00000000, 0b00000000, 0b00000000, 0b00000000,
             0b00011000, 0b00011000, 0b00000000, 0b00000000 } },
    { '/', { 0b00000010, 0b00000100, 0b00000100, 0b00001000,
             0b00001000, 0b00010000, 0b00010000, 0b00000000 } },
    { '0', { 0b00011100, 0b00100010, 0b00100110, 0b00101010,
             0b00110010, 0b00100010, 0b00011100, 0b00000000 } },
    { '1', { 0b00001000, 0b00011000, 0b00001000, 0b00001000,
             0b00001000, 0b00001000, 0b00011100, 0b00000000 } },
    { '2', { 0b00011100, 0b00100010, 0b00000010, 0b00001100,
             0b00010000, 0b00100000, 0b00111110, 0b00000000 } },
    { '3', { 0b00111100, 0b00000010, 0b00000010, 0b00011100,
             0b00000010, 0b00000010, 0b00111100, 0b00000000 } },
    { '4', { 0b00010010, 0b00010010, 0b00010010, 0b00111110,
             0b00000010, 0b00000010, 0b00000010, 0b00000000 } },
    { '5', { 0b00111110, 0b00100000, 0b00111100, 0b00000010,
             0b00000010, 0b00100010, 0b00011100, 0b00000000 } },
    { '6', { 0b00011100, 0b00100000, 0b00100000, 0b00111100,
             0b00100010, 0b00100010, 0b00011100, 0b00000000 } },
    { '7', { 0b00111110, 0b00000010, 0b00000100, 0b00001000,
             0b00010000, 0b00010000, 0b00010000, 0b00000000 } },
    { '8', { 0b00011100, 0b00100010, 0b00100010, 0b00011100,
             0b00100010, 0b00100010, 0b00011100, 0b00000000 } },
    { '9', { 0b00011100, 0b00100010, 0b00100010, 0b00011110,
             0b00000010, 0b00000010, 0b00011100, 0b00000000 } },
    { ':', { 0b00000000, 0b00001000, 0b00001000, 0b00000000,
             0b00001000, 0b00001000, 0b00000000, 0b00000000 } },
    { '?', { 0b00011100, 0b00100010, 0b00000100, 0b00001000,
             0b00001000, 0b00000000, 0b00001000, 0b00000000 } },
    { 'A', { 0b00011000, 0b00100100, 0b00100100, 0b00111100,
             0b00100100, 0b00100100, 0b00000000, 0b00000000 } },
    { 'B', { 0b00111000, 0b00100100, 0b00111000, 0b00100100,
             0b00100100, 0b00111000, 0b00000000, 0b00000000 } },
    { 'C', { 0b00011100, 0b00100010, 0b00100000, 0b00100000,
             0b00100010, 0b00011100, 0b00000000, 0b00000000 } },
    { 'D', { 0b00111000, 0b00100100, 0b00100010, 0b00100010,
             0b00100100, 0b00111000, 0b00000000, 0b00000000 } },
    { 'E', { 0b00111110, 0b00100000, 0b00111000, 0b00100000,
             0b00100000, 0b00111110, 0b00000000, 0b00000000 } },
    { 'F', { 0b00111110, 0b00100000, 0b00111000, 0b00100000,
             0b00100000, 0b00100000, 0b00000000, 0b00000000 } },
    { 'G', { 0b00011100, 0b00100010, 0b00100000, 0b00111110,
             0b00100010, 0b00011100, 0b00000000, 0b00000000 } },
    { 'H', { 0b00100010, 0b00100010, 0b00111110, 0b00100010,
             0b00100010, 0b00100010, 0b00000000, 0b00000000 } },
    { 'I', { 0b00011100, 0b00001000, 0b00001000, 0b00001000,
             0b00001000, 0b00011100, 0b00000000, 0b00000000 } },
    { 'J', { 0b00001110, 0b00000010, 0b00000010, 0b00000010,
             0b00100010, 0b00011100, 0b00000000, 0b00000000 } },
    { 'K', { 0b00100010, 0b00100100, 0b00111000, 0b00100100,
             0b00100010, 0b00100010, 0b00000000, 0b00000000 } },
    { 'L', { 0b00100000, 0b00100000, 0b00100000, 0b00100000,
             0b00100000, 0b00111110, 0b00000000, 0b00000000 } },
    { 'M', { 0b00100010, 0b00110110, 0b00101010, 0b00100010,
             0b00100010, 0b00100010, 0b00000000, 0b00000000 } },
    { 'N', { 0b00100010, 0b00110010, 0b00101010, 0b00100110,
             0b00100010, 0b00100010, 0b00000000, 0b00000000 } },
    { 'O', { 0b00011100, 0b00100010, 0b00100010, 0b00100010,
             0b00100010, 0b00011100, 0b00000000, 0b00000000 } },
    { 'P', { 0b00111000, 0b00100100, 0b00100100, 0b00111000,
             0b00100000, 0b00100000, 0b00000000, 0b00000000 } },
    { 'Q', { 0b00011100, 0b00100010, 0b00100010, 0b00100010,
             0b00100110, 0b00011110, 0b00000000, 0b00000000 } },
    { 'R', { 0b00111000, 0b00100100, 0b00100100, 0b00111000,
             0b00101000, 0b00100100, 0b00000000, 0b00000000 } },
    { 'S', { 0b00011100, 0b00100000, 0b00011000, 0b00000100,
             0b00000100, 0b00111000, 0b00000000, 0b00000000 } },
    { 'T', { 0b00111110, 0b00001000, 0b00001000, 0b00001000,
             0b00001000, 0b00001000, 0b00000000, 0b00000000 } },
    { 'U', { 0b00100010, 0b00100010, 0b00100010, 0b00100010,
             0b00100010, 0b00011100, 0b00000000, 0b00000000 } },
    { 'V', { 0b00100010, 0b00100010, 0b00100010, 0b00100010,
             0b00010100, 0b00001000, 0b00000000, 0b00000000 } },
    { 'W', { 0b00100010, 0b00100010, 0b00100010, 0b00101010,
             0b00110110, 0b00100010, 0b00000000, 0b00000000 } },
    { 'X', { 0b00100010, 0b00010100, 0b00001000, 0b00001000,
             0b00010100, 0b00100010, 0b00000000, 0b00000000 } },
    { 'Y', { 0b00100010, 0b00010100, 0b00001000, 0b00001000,
             0b00001000, 0b00001000, 0b00000000, 0b00000000 } },
    { 'Z', { 0b00111110, 0b00000010, 0b00000100, 0b00001000,
             0b00010000, 0b00111110, 0b00000000, 0b00000000 } },
};

static const uint8_t* font_lookup(char c) {
    // normalize to uppercase so lowercase still shows
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    for (uint32_t i = 0; i < (uint32_t)(sizeof(FONT8) / sizeof(FONT8[0])); ++i) {
        if (FONT8[i].c == c) return FONT8[i].rows;
    }

    // fallback: '?' if present, else space
    for (uint32_t i = 0; i < (uint32_t)(sizeof(FONT8) / sizeof(FONT8[0])); ++i) {
        if (FONT8[i].c == '?') return FONT8[i].rows;
    }
    return FONT8[0].rows; // assume first is space
}

static void draw_char(uint32_t x, uint32_t y,
                      char c, uint32_t color,
                      uint32_t scale) {
    const uint8_t *rows = font_lookup(c);

    for (uint32_t row = 0; row < 8; ++row) {
        uint8_t bits = rows[row];
        for (uint32_t col = 0; col < 8; ++col) {
            if (bits & (1u << (7 - col))) {
                uint32_t px = x + col * scale;
                uint32_t py = y + row * scale;
                fill_rect(px, py, scale, scale, color);
            }
        }
    }
}

static void draw_text(uint32_t x, uint32_t y,
                      const char *s, uint32_t color,
                      uint32_t scale) {
    if (!s) return;
    uint32_t cursor_x = x;
    while (*s) {
        if (*s == '\n') {
            cursor_x = x;
            y += 8 * scale + 2;
        } else {
            draw_char(cursor_x, y, *s, color, scale);
            cursor_x += 8 * scale;
        }
        ++s;
    }
}

// ---------------------------------------------------------------------
// Light bulb + spinner for boot splash
// ---------------------------------------------------------------------

static void draw_bulb(uint32_t cx, uint32_t cy, uint32_t r,
                      uint32_t body_color,
                      uint32_t outline_color,
                      uint32_t base_color) {
    if (r < 16) r = 16;

    // main bulb
    fill_circle((int32_t)cx, (int32_t)cy, (int32_t)r, body_color);

    // outline
    uint32_t outline_r = r + 2;
    for (int32_t dy = -(int32_t)outline_r; dy <= (int32_t)outline_r; ++dy) {
        for (int32_t dx = -(int32_t)outline_r; dx <= (int32_t)outline_r; ++dx) {
            int32_t xx = (int32_t)cx + dx;
            int32_t yy = (int32_t)cy + dy;
            if (xx < 0 || yy < 0 ||
                xx >= (int32_t)g_width ||
                yy >= (int32_t)g_height) continue;

            int32_t d2 = dx*dx + dy*dy;
            if (d2 <= (int32_t)outline_r*(int32_t)outline_r &&
                d2 >= (int32_t)r*(int32_t)r) {
                g_fb[yy * g_pitch + xx] = outline_color;
            }
        }
    }

    // metal base
    uint32_t base_w  = r;
    uint32_t base_h  = r / 3;
    if (base_h < 8) base_h = 8;
    uint32_t base_x  = cx - base_w / 2;
    uint32_t base_y  = cy + r - base_h / 2;

    fill_rect(base_x, base_y, base_w, base_h, base_color);

    // ridges
    uint32_t ridge_h = base_h / 4;
    if (ridge_h == 0) ridge_h = 1;
    uint32_t ridge_y1 = base_y;
    uint32_t ridge_y2 = base_y + ridge_h * 2;
    uint32_t ridge_col = (base_color & 0xFEFEFE) >> 1;

    fill_rect(base_x, ridge_y1, base_w, ridge_h, ridge_col);
    fill_rect(base_x, ridge_y2, base_w, ridge_h, ridge_col);
}

static void draw_spinner_frame(uint32_t cx, uint32_t cy,
                               uint32_t radius,
                               uint32_t frame,
                               uint32_t bg_color) {
    const uint32_t segments = 12;
    uint32_t active  = frame % segments;

    for (uint32_t i = 0; i < segments; ++i) {
        double angle = 2.0 * 3.1415926535 * (double)i / (double)segments;
        uint32_t x = cx + (uint32_t)(radius * 0.8 * __builtin_cos(angle));
        uint32_t y = cy + (uint32_t)(radius * 0.8 * __builtin_sin(angle));

        uint32_t color = (i == active)
            ? 0xFFFFFF
            : ((bg_color & 0xFCFCFC) >> 2);

        fill_circle((int32_t)x, (int32_t)y, (int32_t)(radius / 8 + 1), color);
    }
}

static void run_boot_splash(void) {
    const uint32_t bg           = 0x101020;
    const uint32_t bulb_body    = 0xFFF7CC;
    const uint32_t bulb_outline = 0xD0C080;
    const uint32_t bulb_base    = 0x303040;

    // clear whole screen
    fill_rect(0, 0, g_width, g_height, bg);

    uint32_t cx = g_width  / 2;
    uint32_t cy = g_height / 3;
    uint32_t r  = g_height / 10;
    if (r < 50) r = 50;

    uint32_t text_scale = 3;
    uint32_t text_h     = 8 * text_scale;

    uint32_t base_h      = r / 3;
    if (base_h < 16) base_h = 16;

    uint32_t base_bottom = cy + r + 16 + base_h;
    uint32_t title_y     = base_bottom + 30;
    uint32_t text_w      = 9 * 8 * text_scale;  // "LightOS 4"
    uint32_t title_x     = (cx > text_w / 2) ? (cx - text_w / 2) : 0;

    uint32_t spinner_r   = r / 2;
    if (spinner_r < 20) spinner_r = 20;
    uint32_t spinner_y   = title_y + text_h + 40;

    for (uint32_t frame = 0; frame < 48; ++frame) {
        fill_rect(0, 0, g_width, g_height, bg);
        draw_bulb(cx, cy, r, bulb_body, bulb_outline, bulb_base);
        draw_text(title_x, title_y, "LightOS 4", 0xFFFFFF, text_scale);
        draw_spinner_frame(cx, spinner_y, spinner_r, frame, bg);

        // crude delay for animation
        for (volatile uint64_t wait = 0; wait < 12000000ULL; ++wait) {
            __asm__ volatile("");
        }
    }
}

// ---------------------------------------------------------------------
// Simple keyboard (PS/2) polling
// ---------------------------------------------------------------------

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static int keyboard_poll(uint8_t *out_scancode) {
    if ((inb(0x64) & 0x01) == 0) {
        return 0;
    }
    *out_scancode = inb(0x60);
    return 1;
}

// ---------------------------------------------------------------------
// Command Block (terminal) implementation
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

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) ++n;
    return n;
}

static void str_copy(char *dst, const char *src, uint32_t max_len) {
    if (!dst || !src || max_len == 0) return;
    uint32_t i = 0;
    for (; i + 1 < max_len && src[i]; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static int str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s != *prefix) return 0;
        ++s;
        ++prefix;
    }
    return 1;
}

static void term_add_line(TerminalState *t, const char *line) {
    if (!t) return;
    if (t->line_count >= TERM_MAX_LINES) {
        // scroll up
        for (uint32_t i = 1; i < TERM_MAX_LINES; ++i) {
            str_copy(t->lines[i - 1], t->lines[i], TERM_MAX_COLS);
        }
        t->line_count = TERM_MAX_LINES - 1;
    }
    str_copy(t->lines[t->line_count], line ? line : "", TERM_MAX_COLS);
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

    // HELP
    if (str_starts_with(cmd, "help")) {
        term_add_line(t, "Supported commands:");
        term_add_line(t, "  help");
        term_add_line(t, "  cls / clear");
        term_add_line(t, "  dir / ls");
        term_add_line(t, "  ver / uname");
        term_add_line(t, "  time / date");
        term_add_line(t, "  echo <text>");
        term_add_line(t, "");
        term_add_line(t, "Note: only very basic DOS/Linux-like");
        term_add_line(t, "      commands are simulated.");
        return;
    }

    // CLS / CLEAR
    if (str_starts_with(cmd, "cls") || str_starts_with(cmd, "clear")) {
        term_reset(t);
        return;
    }

    // DIR / LS
    if (str_starts_with(cmd, "dir") || str_starts_with(cmd, "ls")) {
        term_add_line(t, "Directory listing is not implemented");
        term_add_line(t, "(this is a demo Command Block).");
        return;
    }

    // VER / UNAME
    if (str_starts_with(cmd, "ver") || str_starts_with(cmd, "uname")) {
        term_add_line(t, "LightOS 4 (UEFI demo kernel)");
        return;
    }

    // TIME / DATE  (we don't have RTC yet, so fake it)
    if (str_starts_with(cmd, "time") || str_starts_with(cmd, "date")) {
        term_add_line(t, "2026-01-19 12:34:56 (static demo time)");
        return;
    }

    // ECHO
    if (str_starts_with(cmd, "echo")) {
        const char *p = cmd + 4;
        if (*p == ' ') ++p;
        if (*p == '\0') term_add_line(t, "");
        else            term_add_line(t, p);
        return;
    }

    term_add_line(t, "Unknown command. Type 'help'.");
}

static void term_handle_scancode(uint8_t sc, TerminalState *t,
                                 int *selected_icon, int *open_app) {
    (void)selected_icon; // not used while inside Command Block

    if (sc == 0xE0) return;      // ignore extended prefix for now
    if (sc & 0x80) return;       // ignore key releases

    if (sc == 0x01) {            // Esc: close Command Block
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

    // limited scancode -> ASCII (only letters, digits, space)
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

    if (c != 0 && t->input_len < TERM_MAX_COLS - 1) {
        t->input[t->input_len++] = c;
        t->input[t->input_len]   = '\0';
    }
}

// ---------------------------------------------------------------------
// Desktop + windows
// ---------------------------------------------------------------------

static void draw_dock_icon(uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h,
                           uint32_t color) {
    fill_rect(x, y, w, h, color);
    draw_rect(x, y, w, h, 0x000000);
}

static void draw_desktop_background(void) {
    fill_rect(0, 0, g_width, g_height, 0x003366); // blue wallpaper
}

static void draw_taskbar(void) {
    uint32_t bar_h = g_height / 10;
    if (bar_h < 40) bar_h = 40;
    uint32_t y = g_height - bar_h;
    fill_rect(0, y, g_width, bar_h, 0x202020);

    // fake clock and battery (bottom-right)
    char buf[32];
    // static time, the real RTC is not wired yet
    buf[0] = '1'; buf[1] = '2'; buf[2] = ':'; buf[3] = '3'; buf[4] = '4';
    buf[5] = '\0';
    draw_text(g_width - 160, y + 8, buf, 0xFFFFFF, 1);
    draw_text(g_width - 160, y + 24, "2026-01-19", 0xFFFFFF, 1);

    // simple battery icon
    uint32_t bx = g_width - 60;
    uint32_t by = y + 8;
    uint32_t bw = 40;
    uint32_t bh = 20;
    draw_rect(bx, by, bw, bh, 0xFFFFFF);
    fill_rect(bx + 2, by + 2, bw - 4, bh - 4, 0x00CC00);
    fill_rect(bx + bw, by + bh / 4, 4, bh / 2, 0xFFFFFF);
}

static void draw_icons_column(int selected_icon) {
    uint32_t icon_w = g_width / 20;
    if (icon_w < 40) icon_w = 40;
    uint32_t icon_h = icon_w;
    uint32_t gap    = icon_h / 4;

    uint32_t x = g_width / 40;
    uint32_t y = g_height / 12;

    for (int i = 0; i < 5; ++i) {
        uint32_t col = 0xAAAAAA;
        if (i == selected_icon) col = 0xFFFFFF;

        draw_dock_icon(x, y, icon_w, icon_h, col);

        // simple inner glyph so each icon looks different
        uint32_t inner_x = x + icon_w / 4;
        uint32_t inner_y = y + icon_h / 4;
        uint32_t inner_w = icon_w / 2;
        uint32_t inner_h = icon_h / 2;

        switch (i) {
            case 0: // Settings: gear-like
                draw_rect(inner_x, inner_y, inner_w, inner_h, 0x000000);
                break;
            case 1: // File Block: folder
                fill_rect(inner_x, inner_y + inner_h/3,
                          inner_w, inner_h*2/3, 0xFFFFAA);
                fill_rect(inner_x, inner_y,
                          inner_w/2, inner_h/3, 0xFFFFAA);
                break;
            case 2: // Command Block: terminal
                fill_rect(inner_x, inner_y,
                          inner_w, inner_h, 0x000000);
                draw_text(inner_x+2, inner_y+2, ">", 0x00FF00, 1);
                break;
            case 3: // Browser: globe-ish
                fill_circle(inner_x + inner_w/2,
                            inner_y + inner_h/2,
                            inner_w/2, 0x99CCFF);
                break;
            case 4: // placeholder app
                fill_rect(inner_x, inner_y,
                          inner_w, inner_h, 0xFFFFFF);
                break;
        }

        y += icon_h + gap;
    }
}

static void draw_terminal_contents(uint32_t win_x, uint32_t win_y,
                                   uint32_t win_w, uint32_t win_h,
                                   uint32_t title_h) {
    uint32_t x = win_x + 10;
    uint32_t y = win_y + title_h + 10;

    for (uint32_t i = 0; i < g_term.line_count; ++i) {
        draw_text(x, y, g_term.lines[i], 0xFFFFFF, 1);
        y += 12;
        if (y + 12 >= win_y + win_h) break;
    }

    // prompt
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
        draw_text(x, y + 4, buf, 0xFFFFFF, 1);
    }
}

static void draw_window_frame(uint32_t win_x, uint32_t win_y,
                              uint32_t win_w, uint32_t win_h,
                              const char *title,
                              int open_app) {
    uint32_t title_h = 24;

    // background
    fill_rect(win_x, win_y, win_w, win_h, 0x202020);
    draw_rect(win_x, win_y, win_w, win_h, 0x000000);

    // title bar
    fill_rect(win_x, win_y, win_w, title_h, 0x303030);
    draw_rect(win_x, win_y, win_w, title_h, 0x000000);
    draw_text(win_x + 8, win_y + 6, title, 0xFFFFFF, 1);

    // close button
    uint32_t close_w = 18;
    uint32_t close_x = win_x + win_w - close_w - 6;
    uint32_t close_y = win_y + 4;
    fill_rect(close_x, close_y, close_w, title_h - 8, 0x880000);

    // window contents
    switch (open_app) {
        case 0: // Settings
            draw_text(win_x + 10, win_y + title_h + 10,
                      "Settings app is not implemented yet.",
                      0xFFFFFF, 1);
            break;
        case 1: // File Block
            draw_text(win_x + 10, win_y + title_h + 10,
                      "File Block (file manager) is not implemented yet.",
                      0xFFFFFF, 1);
            break;
        case 2: // Command Block (terminal)
            draw_terminal_contents(win_x, win_y, win_w, win_h, title_h);
            break;
        case 3: // Browser placeholder
            draw_text(win_x + 10, win_y + title_h + 10,
                      "Browser is not implemented yet.",
                      0xFFFFFF, 1);
            draw_text(win_x + 10, win_y + title_h + 26,
                      "(Any working browser here will be a big W.)",
                      0xFFFFFF, 1);
            break;
        case 4: // extra app
            draw_text(win_x + 10, win_y + title_h + 10,
                      "Extra app placeholder.",
                      0xFFFFFF, 1);
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

        draw_window_frame(win_x, win_y, win_w, win_h, title, open_app);
    }
}

// ---------------------------------------------------------------------
// Navigation / input on desktop
// ---------------------------------------------------------------------

static void handle_nav_scancode(uint8_t sc,
                                int *selected_icon,
                                int *open_app) {
    if (sc == 0xE0) return;      // ignore extended prefix
    if (sc & 0x80) return;       // ignore key releases

    // arrow up / down
    if (sc == 0x48) {            // up arrow
        if (*selected_icon > 0) (*selected_icon)--;
        return;
    }
    if (sc == 0x50) {            // down arrow
        if (*selected_icon < 4) (*selected_icon)++;
        return;
    }

    // Enter: open app
    if (sc == 0x1C) {
        if (*selected_icon >= 0 && *selected_icon <= 4) {
            *open_app = *selected_icon;
            // entering Command Block: reset / greet
            if (*open_app == 2) {
                term_reset(&g_term);
            }
        }
        return;
    }

    // Esc: close window
    if (sc == 0x01) {
        *open_app = -1;
        return;
    }
}

// ---------------------------------------------------------------------
// Kernel entry
// ---------------------------------------------------------------------

void kernel_main(BootInfo *boot) {
    g_fb     = (uint32_t*)(uintptr_t)boot->framebuffer_base;
    g_width  = boot->framebuffer_width;
    g_height = boot->framebuffer_height;
    g_pitch  = boot->framebuffer_pitch ? boot->framebuffer_pitch : g_width;

    run_boot_splash();

    int selected_icon = 2;  // default highlight Command Block
    int open_app      = -1;

    draw_desktop(selected_icon, open_app);

    while (1) {
        uint8_t sc;
        if (keyboard_poll(&sc)) {
            int prev_sel  = selected_icon;
            int prev_open = open_app;

            if (open_app == 2) {
                // Command Block takes over keyboard
                term_handle_scancode(sc, &g_term, &selected_icon, &open_app);
            } else {
                handle_nav_scancode(sc, &selected_icon, &open_app);
            }

            // redraw on change, or while typing into terminal
            if (selected_icon != prev_sel ||
                open_app      != prev_open ||
                open_app == 2) {
                draw_desktop(selected_icon, open_app);
            }
        }
    }
}
