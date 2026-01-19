// LightOS 4 kernel
// Simple UEFI framebuffer desktop + Command Block shell.
// Uses BootInfo from boot.h and PS/2 keyboard polling.
// No mouse, no real filesystem, no real RTC (all placeholders).

#include <stdint.h>
#include "boot.h"

// ---------------------------------------------------------------------
// Global framebuffer state
// ---------------------------------------------------------------------

static uint32_t *g_fb    = 0;
static uint32_t  g_width = 0;
static uint32_t  g_height = 0;
static uint32_t  g_pitch  = 0;   // pixels per scanline

// ---------------------------------------------------------------------
// CPU / I/O helpers
// ---------------------------------------------------------------------

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

// Non-blocking PS/2 keyboard poll.
// Returns 1 if a scancode was read into *sc, 0 if none.
static int keyboard_poll(uint8_t *sc) {
    uint8_t status = inb(0x64);
    if ((status & 0x01) == 0)
        return 0;
    *sc = inb(0x60);
    return 1;
}

// ---------------------------------------------------------------------
// Drawing primitives
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
            if (x * x + y * y <= (int32_t)r * (int32_t)r) {
                put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

// ---------------------------------------------------------------------
// Tiny 8×8 bitmap font (only chars we need)
// ---------------------------------------------------------------------

typedef struct {
    char c;
    uint8_t rows[8];
} Glyph8;

static const Glyph8 FONT8[] = {
    { 'L', { 0b10000000,
             0b10000000,
             0b10000000,
             0b10000000,
             0b10000000,
             0b10000000,
             0b11111110,
             0 } },
    { 'i', { 0b00100000,
             0b00000000,
             0b00100000,
             0b00100000,
             0b00100000,
             0b00100000,
             0b00000000,
             0 } },
    { 'g', { 0b00111100,
             0b01000100,
             0b01000100,
             0b00111100,
             0b00000100,
             0b00111000,
             0 } },
    { 'h', { 0b10000000,
             0b10000000,
             0b10111000,
             0b11000100,
             0b10000100,
             0b10000100,
             0 } },
    { 't', { 0b00100000,
             0b00100000,
             0b11111000,
             0b00100000,
             0b00100000,
             0b00011000,
             0 } },
    { 'O', { 0b00111000,
             0b01000100,
             0b10000010,
             0b10000010,
             0b01000100,
             0b00111000,
             0 } },
    { 'S', { 0b00111100,
             0b01000000,
             0b00111100,
             0b00000010,
             0b00000010,
             0b00111100,
             0 } },
    { '4', { 0b00011000,
             0b00101000,
             0b01001000,
             0b11111100,
             0b00001000,
             0b00001000,
             0 } },
    { ':', { 0b00000000,
             0b00000000,
             0b00110000,
             0b00110000,
             0b00000000,
             0b00110000,
             0b00110000,
             0 } },
    { '2', { 0b00111100,
             0b01000010,
             0b00001100,
             0b00010000,
             0b00100000,
             0b01111110,
             0 } },
    { '3', { 0b00111100,
             0b01000010,
             0b00001100,
             0b00000010,
             0b01000010,
             0b00111100,
             0 } },
    { '0', { 0b00111100,
             0b01000010,
             0b01000010,
             0b01000010,
             0b01000010,
             0b00111100,
             0 } },
    { '6', { 0b00011100,
             0b00100000,
             0b01111100,
             0b01000010,
             0b01000010,
             0b00111100,
             0 } },
    { '-', { 0b00000000,
             0b00000000,
             0b00000000,
             0b01111110,
             0b00000000,
             0b00000000,
             0 } },
    { ' ', { 0,0,0,0,0,0,0,0 } },
};

static const uint8_t* font_lookup(char c) {
    for (unsigned i = 0; i < sizeof(FONT8)/sizeof(FONT8[0]); ++i) {
        if (FONT8[i].c == c) return FONT8[i].rows;
    }
    return FONT8[sizeof(FONT8)/sizeof(FONT8[0]) - 1].rows; // space
}

static void draw_char(uint32_t x, uint32_t y,
                      char c, uint32_t color, uint32_t scale) {
    const uint8_t *rows = font_lookup(c);
    for (uint32_t row = 0; row < 8; ++row) {
        uint8_t bits = rows[row];
        for (uint32_t col = 0; col < 8; ++col) {
            if (bits & (0x80 >> col)) {
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
    while (*s) {
        if (*s == '\n') {
            y += 8 * scale + 2;
            s++;
            continue;
        }
        draw_char(x, y, *s, color, scale);
        x += 8 * scale;
        s++;
    }
}

// ---------------------------------------------------------------------
// Boot splash: bulb + "LightOS 4" + spinner UNDER the text
// ---------------------------------------------------------------------

static void draw_bulb(uint32_t cx, uint32_t cy, uint32_t r,
                      uint32_t body, uint32_t outline, uint32_t base) {
    if (r < 32) r = 32;

    // Circle
    fill_circle(cx, cy, r, outline);
    fill_circle(cx, cy, r - 3, body);

    // Short base so text has room below
    uint32_t base_w = (r * 4) / 3;
    if (base_w < 32) base_w = 32;
    uint32_t base_h = r / 3;
    if (base_h < 12) base_h = 12;

    uint32_t base_x = cx - base_w / 2;
    uint32_t base_y = cy + r + 8;
    fill_rect(base_x, base_y, base_w, base_h, base);
}

static void draw_spinner_frame(uint32_t cx, uint32_t cy,
                               uint32_t radius, uint32_t frame,
                               uint32_t bg) {
    const int8_t dx[8] = { 0,  1,  1,  1,  0, -1, -1, -1 };
    const int8_t dy[8] = { -1, -1,  0,  1,  1,  1,  0, -1 };

    uint32_t dot = radius / 3;
    if (dot < 3) dot = 3;

    // Clear area around spinner
    uint32_t box = radius * 2 + dot * 2;
    fill_rect(cx - box / 2, cy - box / 2, box, box, bg);

    for (uint32_t i = 0; i < 8; ++i) {
        int32_t px = (int32_t)cx + dx[i] * (int32_t)radius;
        int32_t py = (int32_t)cy + dy[i] * (int32_t)radius;
        uint32_t col = (i == (frame & 7)) ? 0xFFFFFF : 0x505060;
        fill_rect((uint32_t)(px - (int32_t)(dot / 2)),
                  (uint32_t)(py - (int32_t)(dot / 2)),
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
    uint32_t r  = g_height / 12;
    if (r < 40) r = 40;

    draw_bulb(cx, cy, r, bulb_body, bulb_outline, bulb_base);

    uint32_t text_scale = 3;
    uint32_t text_h     = 8 * text_scale;

    uint32_t base_h      = r / 3;
    if (base_h < 12) base_h = 12;
    uint32_t base_bottom = cy + r + 8 + base_h;

    uint32_t title_y = base_bottom + 16;       // BELOW bulb
    uint32_t text_w  = 9 * 8 * text_scale;     // "LightOS 4"
    uint32_t title_x = (cx > text_w / 2) ? cx - text_w / 2 : 0;

    uint32_t spinner_r = r / 2;
    uint32_t spinner_y = title_y + text_h + 24; // CLEARLY under text

    for (uint32_t frame = 0; frame < 48; ++frame) {
        fill_rect(0, 0, g_width, g_height, bg);
        draw_bulb(cx, cy, r, bulb_body, bulb_outline, bulb_base);
        draw_text(title_x, title_y, "LightOS 4", 0xFFFFFF, text_scale);
        draw_spinner_frame(cx, spinner_y, spinner_r, frame, bg);

        // crude delay loop for animation
        for (volatile uint64_t wait = 0; wait < 12000000ULL; ++wait) {
            __asm__ volatile("");
        }
    }
}

// ---------------------------------------------------------------------
// Command Block terminal state
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

static int str_eq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        ++i;
    }
    return a[i] == 0 && b[i] == 0;
}

static int str_starts_with(const char *s, const char *prefix) {
    uint32_t i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        ++i;
    }
    return 1;
}

static void term_add_line(TerminalState *t, const char *s) {
    if (!t) return;

    if (t->line_count == TERM_MAX_LINES) {
        // scroll up
        for (uint32_t i = 1; i < TERM_MAX_LINES; ++i) {
            for (uint32_t j = 0; j < TERM_MAX_COLS; ++j)
                t->lines[i - 1][j] = t->lines[i][j];
        }
        t->line_count = TERM_MAX_LINES - 1;
    }

    uint32_t len = str_len(s);
    if (len >= TERM_MAX_COLS) len = TERM_MAX_COLS - 1;

    uint32_t idx = t->line_count++;
    uint32_t j   = 0;
    for (; j < len; ++j) t->lines[idx][j] = s[j];
    t->lines[idx][j] = '\0';
}

static void term_clear(TerminalState *t) {
    if (!t) return;
    t->line_count = 0;
    t->input_len  = 0;
    t->input[0]   = '\0';
}

static void term_init(TerminalState *t) {
    term_clear(t);
    term_add_line(t, "LightOS 4 Command Block");
    term_add_line(t, "Type 'help' for commands.");
}

// Convert PS/2 scancode (set 1) to ASCII (no shift)
static char scancode_to_char(uint8_t sc) {
    switch (sc) {
        // number row
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        case 0x0C: return '-';
        case 0x0D: return '=';
        // QWERTY rows
        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';
        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
        case 0x33: return ',';
        case 0x34: return '.';
        case 0x35: return '/';
        case 0x39: return ' ';
        default:   return 0;
    }
}

static void term_execute_command(TerminalState *t, const char *cmd_raw) {
    // Trim leading spaces
    const char *cmd = cmd_raw;
    while (*cmd == ' ') ++cmd;

    uint32_t len = str_len(cmd);
    if (len == 0) return;

    // Echo the command itself
    char echo_buf[TERM_MAX_COLS];
    uint32_t pos = 0;
    echo_buf[pos++] = '>';
    echo_buf[pos++] = ' ';
    for (uint32_t i = 0; i < len && pos < TERM_MAX_COLS - 1; ++i) {
        echo_buf[pos++] = cmd[i];
    }
    echo_buf[pos
