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
    echo_buf[pos] = '\0';
    term_add_line(t, echo_buf);

    // HELP
    if (str_eq(cmd, "help")) {
        term_add_line(t, "Supported commands:");
        term_add_line(t, "  help");
        term_add_line(t, "  cls / clear");
        term_add_line(t, "  dir / ls");
        term_add_line(t, "  ver / uname");
        term_add_line(t, "  time, date");
        term_add_line(t, "  echo <text>");
        return;
    }

    // CLEAR
    if (str_eq(cmd, "cls") || str_eq(cmd, "clear")) {
        term_init(t);
        return;
    }

    // DIR / LS (fake directory listing)
    if (str_eq(cmd, "dir") || str_eq(cmd, "ls")) {
        term_add_line(t, "Volume in drive C is LIGHTOS");
        term_add_line(t, "Directory of C:/");
        term_add_line(t, "  KERNEL   SYS");
        term_add_line(t, "  BOOT     UEFI");
        term_add_line(t, "  APPS     CMD");
        term_add_line(t, "<no real filesystem yet>");
        return;
    }

    // VER / UNAME
    if (str_eq(cmd, "ver") || str_eq(cmd, "uname") || str_eq(cmd, "uname -a")) {
        term_add_line(t, "LightOS 4 (kernel 0.1)");
        term_add_line(t, "x86_64, single core, no MMU");
        return;
    }

    // TIME / DATE (fake)
    if (str_eq(cmd, "time")) {
        term_add_line(t, "Current time: 12:34 (demo)");
        return;
    }
    if (str_eq(cmd, "date")) {
        term_add_line(t, "Current date: 2026-01-19 (demo)");
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
        term_execute_command(t, t->input);
        t->input_len = 0;
        t->input[0] = '\0';
        return;
    }

    char ch = scancode_to_char(sc);
    if (ch != 0 && t->input_len < TERM_MAX_COLS - 1) {
        t->input[t->input_len++] = ch;
        t->input[t->input_len]   = '\0';
    }
}

// ---------------------------------------------------------------------
// Desktop widgets & main window
// ---------------------------------------------------------------------

static void draw_wifi_icon(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t bar_h = size / 5;
    if (bar_h < 2) bar_h = 2;

    // Fake full signal bars
    fill_rect(x,              y + size - bar_h,      size / 4, bar_h, 0xFFFFFF);
    fill_rect(x + size / 3,   y + size - 2 * bar_h,  size / 4, bar_h, 0xFFFFFF);
    fill_rect(x + 2*size / 3, y + size - 3 * bar_h,  size / 4, bar_h, 0xFFFFFF);
}

static void draw_battery_icon(uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h,
                              uint32_t percent) {
    if (w < 18) w = 18;
    if (h < 8)  h = 8;
    uint32_t border = 2;

    // Frame
    fill_rect(x, y, w, h, 0xFFFFFF);
    fill_rect(x + border, y + border,
              w - 2 * border, h - 2 * border, 0x202020);

    if (percent > 100) percent = 100;
    uint32_t inner_w = w - 2 * border;
    uint32_t fill_w  = (inner_w * percent) / 100;
    uint32_t color   = (percent < 25) ? 0xC00000 : 0x00C000;

    fill_rect(x + border, y + border,
              fill_w, h - 2 * border, color);

    // Little nub
    uint32_t nub_w = w / 8;
    if (nub_w < 2) nub_w = 2;
    fill_rect(x + w, y + h / 3, nub_w, h / 3, 0xFFFFFF);
}

static void draw_speaker_icon(uint32_t x, uint32_t y, uint32_t size) {
    uint32_t box = size / 2;
    if (box < 4) box = 4;
    fill_rect(x, y + (size - box) / 2, box, box, 0xFFFFFF);
    fill_rect(x + box, y + (size - box) / 2 + box / 4,
              box / 2, box / 2, 0xFFFFFF);
}

static void draw_clock_box(uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h) {
    // Static time/date placeholder
    fill_rect(x, y, w, h, 0x404040);
    draw_text(x + 4, y + 2,       "12:34",      0xFFFFFF, 1);
    draw_text(x + 4, y + h / 2 + 1, "2026-01-19", 0xC0C0C0, 1);
}

// Left column: 5 app icons, with selection highlight
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
                fill_rect(inner_x + inner_w / 4, inner_y + inner_h / 4,
                          inner_w / 2, inner_h / 2, bg);
                break;
            case 1: // File Block
                fill_rect(inner_x, inner_y + inner_h / 4,
                          inner_w, inner_h * 3 / 4, 0xFFE79C);
                fill_rect(inner_x, inner_y,
                          inner_w / 2, inner_h / 3, 0xFFE79C);
                break;
            case 2: // Command Block
                fill_rect(inner_x, inner_y, inner_w, inner_h, 0x000000);
                fill_rect(inner_x + inner_w / 6,
                          inner_y + inner_h / 2,
                          inner_w / 5, inner_h / 10, 0x00FF00);
                break;
            case 3: { // Browser (blue circle)
                uint32_t cx = inner_x + inner_w / 2;
                uint32_t cy = inner_y + inner_h / 2;
                uint32_t r  = inner_h / 3;
                for (int32_t yy = -(int32_t)r; yy <= (int32_t)r; ++yy) {
                    for (int32_t xx = -(int32_t)r; xx <= (int32_t)r; ++xx) {
                        if (xx * xx + yy * yy <= (int32_t)r * (int32_t)r) {
                            put_pixel(cx + xx, cy + yy, 0x3399FF);
                        }
                    }
                }
                break;
            }
            case 4: // App Store
                fill_rect(inner_x, inner_y + inner_h / 3,
                          inner_w, inner_h * 2 / 3, 0xFFFFFF);
                fill_rect(inner_x + inner_w / 4,
                          inner_y + inner_h / 3 - inner_h / 5,
                          inner_w / 2, inner_h / 5, 0xFFFFFF);
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

    // Prompt
    if (y + 16 < win_y + win_h) {
        char buf[TERM_MAX_COLS];
        buf[0] = '>';
        buf[1] = ' ';
        uint32_t len = g_term.input_len;
        if (len > TERM_MAX_COLS - 3) len = TERM_MAX_COLS - 3;
        for (uint32_t i = 0; i < len; ++i) {
            buf[2 + i] = g_term.input[i];
        }
        buf[2 + len] = '\0';
        draw_text(x, y + 4, buf, 0x00FF00, 1);
    }
}

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
        case 2: body_col = 0x101010; title_col = 0x202020; break; // Command Block
        case 3: body_col = 0xE0F4FF; title_col = 0x0066BB; break; // Browser
        case 4: body_col = 0xF4E0FF; title_col = 0x664488; break; // App Store
        default: break;
    }

    // Border
    fill_rect(win_x - 1, win_y - 1, win_w + 2, win_h + 2, border_col);

    // Title bar
    uint32_t title_h = 28;
    fill_rect(win_x, win_y, win_w, title_h, title_col);

    // Window body
    fill_rect(win_x, win_y + title_h, win_w, win_h - title_h, body_col);

    // Close button (just visual)
    uint32_t btn   = (title_h > 8) ? (title_h - 8) : (title_h / 2);
    uint32_t btn_x = win_x + win_w - btn - 4;
    uint32_t btn_y = win_y + (title_h - btn) / 2;
    fill_rect(btn_x, btn_y, btn, btn, 0x800000);

    // If Command Block is open, draw its content
    if (open_app == 2) {
        draw_terminal_contents(win_x, win_y, win_w, win_h, title_h);
    }
}

static void draw_desktop(int selected_icon, int open_app) {
    uint32_t desktop_bg = 0x003366;
    uint32_t panel_bg   = 0x202020;

    // Background
    fill_rect(0, 0, g_width, g_height, desktop_bg);

    // Taskbar
    uint32_t panel_h = g_height / 12;
    if (panel_h < 40) panel_h = 40;
    uint32_t panel_y = g_height - panel_h;
    fill_rect(0, panel_y, g_width, panel_h, panel_bg);

    // Start block
    uint32_t start_w = panel_h;
    fill_rect(0, panel_y, start_w, panel_h, 0x404040);

    // Tray
    uint32_t tray_w = g_width / 4;
    if (tray_w < 220) tray_w = 220;
    uint32_t tray_x = g_width - tray_w;
    fill_rect(tray_x, panel_y, tray_w, panel_h, 0x303030);

    uint32_t icon_size = panel_h / 2;
    if (icon_size < 16) icon_size = 16;
    uint32_t icon_y = panel_y + (panel_h - icon_size) / 2;

    uint32_t cursor_x = tray_x + tray_w - icon_size - 8;
    draw_wifi_icon(cursor_x, icon_y, icon_size);
    cursor_x -= icon_size + 8;

    draw_speaker_icon(cursor_x, icon_y, icon_size);
    cursor_x -= icon_size + 12;

    draw_battery_icon(cursor_x, icon_y, icon_size * 3 / 2, icon_size, 76);
    cursor_x -= icon_size * 2;

    // Clock on left of tray
    uint32_t clock_w = tray_w / 3;
    uint32_t clock_h = icon_size + 10;
    uint32_t clock_x = tray_x + 8;
    uint32_t clock_y = panel_y + (panel_h - clock_h) / 2;
    draw_clock_box(clock_x, clock_y, clock_w, clock_h);

    // App icons + active window
    draw_app_icons(selected_icon);
    draw_main_window(open_app);
}

// ---------------------------------------------------------------------
// Keyboard navigation (desktop-level)
// ---------------------------------------------------------------------

static void handle_nav_scancode(uint8_t sc,
                                int *selected_icon,
                                int *open_app) {
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

__attribute__((noreturn))
void kernel_main(BootInfo *bi) {
    g_fb     = (uint32_t*)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch;

    // 1) Boot splash
    run_boot_splash();

    // 2) Desktop + Command Block
    term_init(&g_term);

    int selected_icon = 0;   // 0..4
    int open_app      = -1;  // -1 = nothing open

    draw_desktop(selected_icon, open_app);

    for (;;) {
        uint8_t sc;
        if (keyboard_poll(&sc)) {
            int prev_sel  = selected_icon;
            int prev_open = open_app;

            if (open_app == 2) {
                // Command Block takes over keyboard
                term_handle_scancode(sc, &g_term, &selected_icon, &open_app);
            } else {
                // Desktop navigation
                handle_nav_scancode(sc, &selected_icon, &open_app);
            }

            // Redraw when something changed, or while typing in Command Block
            if (selected_icon != prev_sel ||
                open_app      != prev_open ||
                open_app == 2) {
                draw_desktop(selected_icon, open_app);
            }
        }
    }
}

