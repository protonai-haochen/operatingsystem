// kernel/core/kernel.c
// LightOS 4 - UEFI framebuffer kernel: boot splash + desktop + apps

#include <stdint.h>
#include "boot.h"

// ---------------------------------------------------------------------
// Global framebuffer + basic system state
// ---------------------------------------------------------------------

static uint32_t *g_fb     = 0;
static uint32_t  g_width  = 0;
static uint32_t  g_height = 0;
static uint32_t  g_pitch  = 0;  // pixels per row

// Clock from BootInfo (RTC at boot)
static uint16_t g_year   = 0;
static uint8_t  g_month  = 0;
static uint8_t  g_day    = 0;
static uint8_t  g_hour   = 0;
static uint8_t  g_minute = 0;
static uint8_t  g_second = 0;

// Mouse state (PS/2)
static int32_t  g_mouse_x        = 0;
static int32_t  g_mouse_y        = 0;
static uint8_t  g_mouse_buttons  = 0;

// Desktop / UI state
static int g_selected_icon   = 2;   // which dock icon is highlighted
static int g_open_app        = -1;  // -1 = none, 0=settings,1=file,2=cmd,3=browser,4=extra
static int g_start_open      = 0;   // start menu visible?
static int g_start_selection = 2;   // which item in start menu
static int g_theme           = 0;   // 0 = dark blue, 1 = lighter theme
static int g_browser_page    = 0;   // 0=home,1=docs,2=about

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
// Small string + number helpers (no libc)
// ---------------------------------------------------------------------

static uint32_t str_len(const char *s) {
    if (!s) return 0;
    uint32_t n = 0;
    while (s[n]) ++n;
    return n;
}

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a; ++b;
    }
    return (*a == 0 && *b == 0);
}

static void str_copy(char *dst, const char *src, uint32_t max_len) {
    if (!dst || !src || max_len == 0) return;
    uint32_t i = 0;
    for (; i + 1 < max_len && src[i]; ++i) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void str_cat(char *dst, const char *src, uint32_t max_len) {
    uint32_t dlen = str_len(dst);
    if (dlen >= max_len) return;
    uint32_t i = 0;
    while (src && src[i] && (dlen + i + 1) < max_len) {
        dst[dlen + i] = src[i];
        ++i;
    }
    dst[dlen + i] = '\0';
}

static int str_starts_with(const char *s, const char *pfx) {
    if (!s || !pfx) return 0;
    while (*pfx) {
        if (*s != *pfx) return 0;
        ++s; ++pfx;
    }
    return 1;
}

static void u32_to_str(uint32_t v, char *out, uint32_t max_len) {
    if (!out || max_len < 2) return;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    char tmp[16];
    uint32_t i = 0;
    while (v > 0 && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    uint32_t pos = 0;
    while (i > 0 && pos + 1 < max_len) {
        out[pos++] = tmp[--i];
    }
    out[pos] = '\0';
}

static void fmt_two_digits(uint32_t v, char *out) {
    out[0] = (char)('0' + ((v / 10) % 10));
    out[1] = (char)('0' + (v % 10));
    out[2] = '\0';
}

static void build_time_string(char *buf, uint32_t max_len) {
    if (!buf || max_len < 9) return;
    char hh[3], mm[3], ss[3];
    fmt_two_digits(g_hour,   hh);
    fmt_two_digits(g_minute, mm);
    fmt_two_digits(g_second, ss);

    buf[0] = '\0';
    str_cat(buf, hh, max_len);
    str_cat(buf, ":", max_len);
    str_cat(buf, mm, max_len);
    str_cat(buf, ":", max_len);
    str_cat(buf, ss, max_len);
}

static void build_date_string(char *buf, uint32_t max_len) {
    if (!buf || max_len < 11) return;
    char y[8], m[3], d[3];
    u32_to_str(g_year, y, sizeof(y));
    fmt_two_digits(g_month, m);
    fmt_two_digits(g_day,   d);

    buf[0] = '\0';
    str_cat(buf, y, max_len);
    str_cat(buf, "-", max_len);
    str_cat(buf, m, max_len);
    str_cat(buf, "-", max_len);
    str_cat(buf, d, max_len);
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
// PS/2 keyboard and mouse access
// ---------------------------------------------------------------------

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// Keyboard: only treat data as keyboard if mouse bit (0x20) is clear.
static int keyboard_poll(uint8_t *sc) {
    uint8_t status = inb(0x64);
    if ((status & 0x01) == 0) return 0;      // no data
    if (status & 0x20) return 0;            // mouse data, not keyboard
    *sc = inb(0x60);
    return 1;
}

// Simple PS/2 controller wait helpers
static void ps2_wait_input(void) {   // wait until we can send
    while (inb(0x64) & 0x02) { }
}

static void ps2_wait_output(void) {  // wait until data available
    while ((inb(0x64) & 0x01) == 0) { }
}

static uint8_t mouse_read_ack(void) {
    ps2_wait_output();
    return inb(0x60);
}

static void mouse_write(uint8_t val) {
    ps2_wait_input();
    outb(0x64, 0xD4);   // tell controller next byte is for mouse
    ps2_wait_input();
    outb(0x60, val);
    (void)mouse_read_ack(); // ignore ACK
}

static void mouse_init(void) {
    // enable auxiliary device (mouse)
    ps2_wait_input();
    outb(0x64, 0xA8);

    // reset to defaults
    mouse_write(0xF6);
    // enable streaming
    mouse_write(0xF4);

    g_mouse_x       = (int32_t)(g_width / 2);
    g_mouse_y       = (int32_t)(g_height / 2);
    g_mouse_buttons = 0;
}

// Try to read one PS/2 mouse packet (3 bytes)
static int mouse_poll(int *dx, int *dy, uint8_t *buttons) {
    if (!dx || !dy || !buttons) return 0;

    uint8_t status = inb(0x64);
    if ((status & 0x01) == 0) return 0;       // no data
    if ((status & 0x20) == 0) return 0;       // not mouse data

    int8_t packet[3];
    packet[0] = (int8_t)inb(0x60);

    // Read remaining bytes
    ps2_wait_output();
    packet[1] = (int8_t)inb(0x60);
    ps2_wait_output();
    packet[2] = (int8_t)inb(0x60);

    *dx      = (int)packet[1];
    *dy      = -(int)packet[2]; // screen y grows downwards, mouse Y packet is opposite
    *buttons = (uint8_t)(packet[0] & 0x07); // bits 0..2 = L, R, M

    return 1;
}

// ---------------------------------------------------------------------
// Terminal (Command Block)
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

// ---------------------------------------------------------------------
// In-memory virtual filesystem for File Block + Command Block
// (single-level directories under root, simple text files)
// ---------------------------------------------------------------------

#define VFS_MAX_NODES    64
#define VFS_MAX_NAME     24
#define VFS_MAX_CONTENT 256

typedef struct {
    char    name[VFS_MAX_NAME];
    uint8_t is_dir;
    int8_t  parent;     // -1 = root
    uint32_t size;
    char    content[VFS_MAX_CONTENT];
} VNode;

static VNode    g_nodes[VFS_MAX_NODES];
static uint32_t g_node_count = 0;
static int      g_cwd        = -1;   // -1 = root

static int vfs_find_in_dir(int parent, const char *name) {
    for (uint32_t i = 0; i < g_node_count; ++i) {
        if (g_nodes[i].parent == parent && str_eq(g_nodes[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
}

// Only root + one-level directories.
// If cwd != root, we disallow mkdir to avoid complex nesting.
static void vfs_init(void) {
    g_node_count = 0;
    g_cwd        = -1;

    // Make a docs directory and a readme.txt in it
    if (g_node_count < VFS_MAX_NODES) {
        VNode *d = &g_nodes[g_node_count++];
        d->is_dir = 1;
        d->parent = -1;
        d->size   = 0;
        str_copy(d->name, "docs", VFS_MAX_NAME);
        d->content[0] = '\0';
    }

    if (g_node_count < VFS_MAX_NODES) {
        VNode *f = &g_nodes[g_node_count++];
        f->is_dir = 0;
        f->parent = -1; // at root for now
        str_copy(f->name, "readme.txt", VFS_MAX_NAME);
        const char *msg =
            "Welcome to LightOS 4.\n"
            "Use Command Block and File Block to explore.\n"
            "Demo filesystem is in RAM only.";
        str_copy(f->content, msg, VFS_MAX_CONTENT);
        f->size = str_len(f->content);
    }
}

static void vfs_get_cwd_path(char *out, uint32_t max_len) {
    if (!out || max_len == 0) return;
    if (g_cwd < 0) {
        str_copy(out, "/", max_len);
    } else {
        out[0] = '/';
        out[1] = '\0';
        str_cat(out, g_nodes[g_cwd].name, max_len);
    }
}

static void vfs_dir_list(TerminalState *t) {
    char path[64];
    vfs_get_cwd_path(path, sizeof(path));

    char line[80];
    line[0] = '\0';
    str_cat(line, "Directory listing for ", sizeof(line));
    str_cat(line, path, sizeof(line));
    term_add_line(t, line);

    // Directories
    for (uint32_t i = 0; i < g_node_count; ++i) {
        VNode *n = &g_nodes[i];
        if (!n->is_dir || n->parent != g_cwd) continue;
        char buf[64];
        buf[0] = '\0';
        str_cat(buf, "[DIR] ", sizeof(buf));
        str_cat(buf, n->name, sizeof(buf));
        term_add_line(t, buf);
    }

    // Files
    for (uint32_t i = 0; i < g_node_count; ++i) {
        VNode *n = &g_nodes[i];
        if (n->is_dir || n->parent != g_cwd) continue;
        char size_str[16];
        u32_to_str(n->size, size_str, sizeof(size_str));
        char buf[80];
        buf[0] = '\0';
        str_cat(buf, n->name, sizeof(buf));
        str_cat(buf, " (", sizeof(buf));
        str_cat(buf, size_str, sizeof(buf));
        str_cat(buf, " bytes)", sizeof(buf));
        term_add_line(t, buf);
    }
}

static void vfs_mkdir(const char *name, TerminalState *t) {
    if (!name || !*name) {
        term_add_line(t, "mkdir: missing directory name.");
        return;
    }
    if (g_cwd != -1) {
        term_add_line(t, "mkdir: nested directories not supported (stay at root).");
        return;
    }
    if (vfs_find_in_dir(g_cwd, name) >= 0) {
        term_add_line(t, "mkdir: already exists.");
        return;
    }
    if (g_node_count >= VFS_MAX_NODES) {
        term_add_line(t, "mkdir: VFS full.");
        return;
    }

    VNode *n = &g_nodes[g_node_count++];
    n->is_dir = 1;
    n->parent = g_cwd;
    n->size   = 0;
    str_copy(n->name, name, VFS_MAX_NAME);
    n->content[0] = '\0';

    term_add_line(t, "Directory created.");
}

static void vfs_create_file(const char *name, TerminalState *t) {
    if (!name || !*name) {
        term_add_line(t, "create/touch: missing filename.");
        return;
    }
    if (vfs_find_in_dir(g_cwd, name) >= 0) {
        term_add_line(t, "File already exists.");
        return;
    }
    if (g_node_count >= VFS_MAX_NODES) {
        term_add_line(t, "create: VFS full.");
        return;
    }
    VNode *n = &g_nodes[g_node_count++];
    n->is_dir = 0;
    n->parent = g_cwd;
    str_copy(n->name, name, VFS_MAX_NAME);
    n->content[0] = '\0';
    n->size = 0;

    term_add_line(t, "File created.");
}

static void vfs_cd(const char *arg, TerminalState *t) {
    if (!arg || !*arg) {
        char path[64];
        vfs_get_cwd_path(path, sizeof(path));
        term_add_line(t, path);
        return;
    }

    if (arg[0] == '/' || arg[0] == '\\') {
        g_cwd = -1;
        term_add_line(t, "Changed directory to /");
        return;
    }

    if (str_eq(arg, "..")) {
        g_cwd = -1;
        term_add_line(t, "Changed directory to /");
        return;
    }

    if (g_cwd != -1) {
        term_add_line(t, "cd: nested directories not supported (go back to / first).");
        return;
    }

    int idx = vfs_find_in_dir(-1, arg);
    if (idx < 0 || !g_nodes[idx].is_dir) {
        term_add_line(t, "cd: directory not found.");
        return;
    }

    g_cwd = idx;
    char path[64];
    vfs_get_cwd_path(path, sizeof(path));
    char msg[80];
    msg[0] = '\0';
    str_cat(msg, "Changed directory to ", sizeof(msg));
    str_cat(msg, path, sizeof(msg));
    term_add_line(t, msg);
}

static void vfs_del(const char *name, TerminalState *t) {
    if (!name || !*name) {
        term_add_line(t, "del/rm: missing name.");
        return;
    }

    int idx = vfs_find_in_dir(g_cwd, name);
    if (idx < 0) {
        term_add_line(t, "del/rm: not found.");
        return;
    }

    VNode *n = &g_nodes[idx];
    if (n->is_dir) {
        // ensure empty
        for (uint32_t i = 0; i < g_node_count; ++i) {
            if (g_nodes[i].parent == idx) {
                term_add_line(t, "rmdir: directory not empty.");
                return;
            }
        }
    }

    // remove by shifting down
    for (uint32_t i = (uint32_t)idx + 1; i < g_node_count; ++i) {
        g_nodes[i - 1] = g_nodes[i];
    }
    g_node_count--;

    term_add_line(t, n->is_dir ? "Directory removed." : "File removed.");
}

static void vfs_type(const char *name, TerminalState *t) {
    if (!name || !*name) {
        term_add_line(t, "type/cat: missing filename.");
        return;
    }

    int idx = vfs_find_in_dir(g_cwd, name);
    if (idx < 0 || g_nodes[idx].is_dir) {
        term_add_line(t, "type/cat: file not found.");
        return;
    }

    VNode *n = &g_nodes[idx];
    if (n->content[0] == '\0') {
        term_add_line(t, "(file is empty)");
        return;
    }

    // For simplicity, output as a single line (newlines are fine)
    term_add_line(t, n->content);
}

// ---------------------------------------------------------------------
// Command parsing + execution
// ---------------------------------------------------------------------

static void parse_command(const char *line,
                          char *cmd, uint32_t cmd_max,
                          char *args, uint32_t args_max) {
    if (!cmd || cmd_max == 0) return;
    if (!args || args_max == 0) return;

    cmd[0]  = '\0';
    args[0] = '\0';
    if (!line) return;

    // skip leading spaces
    while (*line == ' ' || *line == '\t') ++line;

    // command word
    uint32_t i = 0;
    while (*line && *line != ' ' && *line != '\t' && i + 1 < cmd_max) {
        cmd[i++] = *line++;
    }
    cmd[i] = '\0';

    // skip spaces before args
    while (*line == ' ' || *line == '\t') ++line;

    // rest of line is args
    uint32_t j = 0;
    while (*line && j + 1 < args_max) {
        args[j++] = *line++;
    }
    args[j] = '\0';
}

static void term_execute_command(TerminalState *t, const char *line) {
    if (!line || !*line) return;

    char cmd[16];
    char args[TERM_MAX_COLS];
    parse_command(line, cmd, sizeof(cmd), args, sizeof(args));

    if (!cmd[0]) return;

    // HELP
    if (str_eq(cmd, "help")) {
        term_add_line(t, "Supported commands:");
        term_add_line(t, "  help");
        term_add_line(t, "  cls / clear");
        term_add_line(t, "  dir / ls");
        term_add_line(t, "  mkdir <name>");
        term_add_line(t, "  cd [dir]");
        term_add_line(t, "  create <file> / touch <file>");
        term_add_line(t, "  del <file/dir> / rm <file/dir>");
        term_add_line(t, "  type <file> / cat <file>");
        term_add_line(t, "  ver / uname");
        term_add_line(t, "  time / date");
        term_add_line(t, "  echo <text>");
        return;
    }

    // CLS / CLEAR
    if (str_eq(cmd, "cls") || str_eq(cmd, "clear")) {
        term_reset(t);
        return;
    }

    // DIR / LS
    if (str_eq(cmd, "dir") || str_eq(cmd, "ls")) {
        vfs_dir_list(t);
        return;
    }

    // MKDIR
    if (str_eq(cmd, "mkdir") || str_eq(cmd, "md")) {
        vfs_mkdir(args, t);
        return;
    }

    // CD
    if (str_eq(cmd, "cd")) {
        vfs_cd(args, t);
        return;
    }

    // CREATE / TOUCH
    if (str_eq(cmd, "create") || str_eq(cmd, "touch")) {
        vfs_create_file(args, t);
        return;
    }

    // DELETE / RM
    if (str_eq(cmd, "del") || str_eq(cmd, "rm")) {
        vfs_del(args, t);
        return;
    }

    // TYPE / CAT
    if (str_eq(cmd, "type") || str_eq(cmd, "cat")) {
        vfs_type(args, t);
        return;
    }

    // VERSION
    if (str_eq(cmd, "ver") || str_eq(cmd, "uname")) {
        term_add_line(t, "LightOS 4 demo kernel (x86_64, UEFI).");
        return;
    }

    // TIME / DATE
    if (str_eq(cmd, "time") || str_eq(cmd, "date")) {
        char tbuf[16];
        char dbuf[16];
        build_time_string(tbuf, sizeof(tbuf));
        build_date_string(dbuf, sizeof(dbuf));

        char linebuf[48];
        linebuf[0] = '\0';
        str_cat(linebuf, dbuf, sizeof(linebuf));
        str_cat(linebuf, " ", sizeof(linebuf));
        str_cat(linebuf, tbuf, sizeof(linebuf));

        term_add_line(t, linebuf);
        return;
    }

    // ECHO
    if (str_eq(cmd, "echo")) {
        term_add_line(t, args);
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
// Desktop + apps drawing
// ---------------------------------------------------------------------

static void draw_mouse_cursor(void) {
    uint32_t x = (uint32_t)g_mouse_x;
    uint32_t y = (uint32_t)g_mouse_y;
    if (x >= g_width || y >= g_height) return;

    // Simple white triangle cursor
    uint32_t size = 12;
    for (uint32_t dy = 0; dy < size; ++dy) {
        for (uint32_t dx = 0; dx <= dy; ++dx) {
            uint32_t px = x + dx;
            uint32_t py = y + dy;
            if (px < g_width && py < g_height) {
                put_pixel(px, py, 0xFFFFFFu);
            }
        }
    }
}

static void draw_desktop_background(void) {
    uint32_t col = (g_theme == 0) ? 0x003366u : 0x224477u;
    fill_rect(0, 0, g_width, g_height, col);
}

static void draw_taskbar(void) {
    uint32_t bar_h = g_height / 10;
    if (bar_h < 40) bar_h = 40;
    uint32_t y = g_height - bar_h;

    uint32_t bar_col = (g_theme == 0) ? 0x202020u : 0x303030u;
    fill_rect(0, y, g_width, bar_h, bar_col);

    // Start button
    uint32_t sb_w = 80;
    uint32_t sb_h = bar_h - 12;
    uint32_t sb_x = 8;
    uint32_t sb_y = y + 6;
    uint32_t sb_col = g_start_open ? 0x505070u : 0x404050u;
    fill_rect(sb_x, sb_y, sb_w, sb_h, sb_col);
    draw_text(sb_x + 10, sb_y + (sb_h / 2) - 6, "Start", 0xFFFFFFu, 1);

    // Clock + date from RTC
    char timebuf[16];
    char datebuf[16];
    build_time_string(timebuf, sizeof(timebuf));
    build_date_string(datebuf, sizeof(datebuf));

    draw_text(g_width - 180, y + 6, timebuf, 0xFFFFFFu, 1);
    draw_text(g_width - 180, y + 22, datebuf, 0xC0C0C0u, 1);

    // simple battery block (fake 100%)
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

// geometry helper for dock icons
static void get_icon_geometry(int index,
                              uint32_t *x, uint32_t *y,
                              uint32_t *w, uint32_t *h) {
    uint32_t icon_w = g_width / 16;
    if (icon_w < 40) icon_w = 40;
    uint32_t icon_h = icon_w;
    uint32_t gap    = icon_h / 4;

    uint32_t base_x = g_width / 40;
    uint32_t base_y = g_height / 10;

    if (x) *x = base_x;
    if (y) *y = base_y + (uint32_t)index * (icon_h + gap);
    if (w) *w = icon_w;
    if (h) *h = icon_h;
}

static void draw_icons_column(int selected_icon) {
    for (int i = 0; i < 5; ++i) {
        uint32_t x, y, w, h;
        get_icon_geometry(i, &x, &y, &w, &h);

        uint32_t col = (i == selected_icon) ? 0xFFFFFFu : 0xAAAAAAu;
        draw_dock_icon(x, y, w, h, col);

        uint32_t ix = x + w / 4;
        uint32_t iy = y + h / 4;
        uint32_t iw = w / 2;
        uint32_t ih = h / 2;

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
    }
}

// Draw Command Block contents inside a window
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
        buf[0] = '\0';

        char path[32];
        vfs_get_cwd_path(path, sizeof(path));

        // Prompt  [path] >
        str_cat(buf, "[", sizeof(buf));
        str_cat(buf, path, sizeof(buf));
        str_cat(buf, "] ", sizeof(buf));
        str_cat(buf, ">", sizeof(buf));
        str_cat(buf, " ", sizeof(buf));

        uint32_t len = g_term.input_len;
        if (len > TERM_MAX_COLS - 4) len = TERM_MAX_COLS - 4;
        for (uint32_t i = 0; i < len; ++i) {
            uint32_t l = str_len(buf);
            if (l + 1 < TERM_MAX_COLS) {
                buf[l]   = g_term.input[i];
                buf[l+1] = '\0';
            }
        }
        // show an underscore cursor
        uint32_t l = str_len(buf);
        if (l + 2 < TERM_MAX_COLS) {
            buf[l]   = '_';
            buf[l+1] = '\0';
        }

        draw_text(x, y + 4, buf, 0x00FF00u, 1);
    }
}

// File Block window body
static void draw_fileblock_contents(uint32_t win_x, uint32_t win_y,
                                    uint32_t win_w, uint32_t win_h,
                                    uint32_t title_h) {
    uint32_t x = win_x + 10;
    uint32_t y = win_y + title_h + 10;

    char path[64];
    vfs_get_cwd_path(path, sizeof(path));

    char hdr[80];
    hdr[0] = '\0';
    str_cat(hdr, "File Block - ", sizeof(hdr));
    str_cat(hdr, path, sizeof(hdr));
    draw_text(x, y, hdr, 0xFFFFFFu, 1);
    y += 16;

    draw_text(x, y, "(Use Command Block to create/edit files; File Block is a viewer.)",
              0xC0C0C0u, 1);
    y += 16;

    // List directories
    for (uint32_t i = 0; i < g_node_count; ++i) {
        VNode *n = &g_nodes[i];
        if (!n->is_dir || n->parent != g_cwd) continue;
        char buf[64];
        buf[0] = '\0';
        str_cat(buf, "[DIR] ", sizeof(buf));
        str_cat(buf, n->name, sizeof(buf));
        draw_text(x, y, buf, 0xFFFFFFu, 1);
        y += 14;
        if (y + 14 >= win_y + win_h) return;
    }

    // List files
    for (uint32_t i = 0; i < g_node_count; ++i) {
        VNode *n = &g_nodes[i];
        if (n->is_dir || n->parent != g_cwd) continue;
        char size_str[16];
        u32_to_str(n->size, size_str, sizeof(size_str));
        char buf[80];
        buf[0] = '\0';
        str_cat(buf, n->name, sizeof(buf));
        str_cat(buf, "  (", sizeof(buf));
        str_cat(buf, size_str, sizeof(buf));
        str_cat(buf, " bytes)", sizeof(buf));
        draw_text(x, y, buf, 0xFFFFFFu, 1);
        y += 14;
        if (y + 14 >= win_y + win_h) return;
    }
}

// Settings window body
static void draw_settings_contents(uint32_t win_x, uint32_t win_y,
                                   uint32_t win_w, uint32_t win_h,
                                   uint32_t title_h) {
    (void)win_w; (void)win_h;
    uint32_t x = win_x + 10;
    uint32_t y = win_y + title_h + 10;

    draw_text(x, y, "LightOS 4 Settings", 0xFFFFFFu, 1);
    y += 16;

    char res[32];
    char w[16], h[16];
    u32_to_str(g_width,  w, sizeof(w));
    u32_to_str(g_height, h, sizeof(h));
    res[0] = '\0';
    str_cat(res, "Resolution: ", sizeof(res));
    str_cat(res, w, sizeof(res));
    str_cat(res, "x", sizeof(res));
    str_cat(res, h, sizeof(res));
    draw_text(x, y, res, 0xFFFFFFu, 1);
    y += 14;

    char tbuf[16];
    char dbuf[16];
    build_time_string(tbuf, sizeof(tbuf));
    build_date_string(dbuf, sizeof(dbuf));

    char line[48];
    line[0] = '\0';
    str_cat(line, "System time: ", sizeof(line));
    str_cat(line, dbuf, sizeof(line));
    str_cat(line, " ", sizeof(line));
    str_cat(line, tbuf, sizeof(line));
    draw_text(x, y, line, 0xFFFFFFu, 1);
    y += 14;

    const char *theme_str = (g_theme == 0) ? "Dark Blue" : "Light Blue";
    char theme_line[48];
    theme_line[0] = '\0';
    str_cat(theme_line, "Theme: ", sizeof(theme_line));
    str_cat(theme_line, theme_str, sizeof(theme_line));
    draw_text(x, y, theme_line, 0xFFFFFFu, 1);
    y += 16;

    draw_text(x, y, "Press 'T' to toggle theme.", 0xC0C0C0u, 1);
}

// Simple offline browser contents
static void draw_browser_contents(uint32_t win_x, uint32_t win_y,
                                  uint32_t win_w, uint32_t win_h,
                                  uint32_t title_h) {
    uint32_t x = win_x + 10;
    uint32_t y = win_y + title_h + 10;

    const char *url = "lightos://home";
    if (g_browser_page == 1) url = "lightos://docs";
    else if (g_browser_page == 2) url = "lightos://about";

    char url_line[80];
    url_line[0] = '\0';
    str_cat(url_line, "URL: ", sizeof(url_line));
    str_cat(url_line, url, sizeof(url_line));
    draw_text(x, y, url_line, 0xFFFFFFu, 1);
    y += 16;

    draw_text(x, y, "[1] Home   [2] Docs   [3] About", 0xC0C0C0u, 1);
    y += 18;

    switch (g_browser_page) {
        case 0:
            draw_text(x, y,
                      "Welcome to LightOS Web (offline demo).\n"
                      "There is no real network stack yet, but this\n"
                      "browser can show static pages.\n",
                      0xFFFFFFu, 1);
            break;
        case 1:
            draw_text(x, y,
                      "Docs page:\n"
                      " - Command Block supports basic commands.\n"
                      " - File Block shows a RAM-based filesystem.\n"
                      " - Settings lets you change theme.\n",
                      0xFFFFFFu, 1);
            break;
        case 2:
            draw_text(x, y,
                      "About:\n"
                      "LightOS 4 demo kernel running under UEFI.\n"
                      "Framebuffer UI, keyboard + mouse input,\n"
                      "simple filesystem, and toy browser.\n",
                      0xFFFFFFu, 1);
            break;
        default:
            break;
    }

    (void)win_w;
    (void)win_h;
}

// Start menu drawing
static const char *g_start_items[5] = {
    "Settings",
    "File Block",
    "Command Block",
    "Browser",
    "Extra"
};

static void draw_start_menu(void) {
    if (!g_start_open) return;

    uint32_t bar_h = g_height / 10;
    if (bar_h < 40) bar_h = 40;

    uint32_t menu_w = g_width / 4;
    if (menu_w < 200) menu_w = 200;
    uint32_t menu_h = bar_h * 2 + 40;
    uint32_t x      = 8;
    uint32_t y      = g_height - bar_h - menu_h - 4;

    fill_rect(x, y, menu_w, menu_h, 0x303040u);
    // border
    for (uint32_t i = 0; i < menu_w; ++i) {
        put_pixel(x + i, y, 0x000000u);
        put_pixel(x + i, y + menu_h - 1, 0x000000u);
    }
    for (uint32_t j = 0; j < menu_h; ++j) {
        put_pixel(x, y + j, 0x000000u);
        put_pixel(x + menu_w - 1, y + j, 0x000000u);
    }

    uint32_t item_h = 24;
    uint32_t iy     = y + 10;

    for (int i = 0; i < 5; ++i) {
        uint32_t bg = (i == g_start_selection) ? 0x505070u : 0x404050u;
        fill_rect(x + 4, iy, menu_w - 8, item_h, bg);
        draw_text(x + 12, iy + 6, g_start_items[i], 0xFFFFFFu, 1);
        iy += item_h + 4;
    }
}

// Window frame + content
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
            draw_settings_contents(win_x, win_y, win_w, win_h, title_h);
            break;
        case 1:
            draw_fileblock_contents(win_x, win_y, win_w, win_h, title_h);
            break;
        case 2:
            draw_terminal_contents(win_x, win_y, win_w, win_h, title_h);
            break;
        case 3:
            draw_browser_contents(win_x, win_y, win_w, win_h, title_h);
            break;
        case 4:
            draw_text(win_x + 10, win_y + title_h + 10,
                      "Extra app placeholder.", 0xFFFFFFu, 1);
            break;
        default:
            break;
    }
}

static void draw_desktop(int selected_icon, int open_app) {
    draw_desktop_background();
    draw_taskbar();
    draw_icons_column(selected_icon);

    if (g_start_open && open_app == -1) {
        draw_start_menu();
    }

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

    draw_mouse_cursor();
}

// ---------------------------------------------------------------------
// Geometry helpers for mouse hit testing
// ---------------------------------------------------------------------

static void get_taskbar_metrics(uint32_t *y, uint32_t *h) {
    uint32_t bar_h = g_height / 10;
    if (bar_h < 40) bar_h = 40;
    if (y) *y = g_height - bar_h;
    if (h) *h = bar_h;
}

static void get_start_button_rect(uint32_t *x, uint32_t *y,
                                  uint32_t *w, uint32_t *h) {
    uint32_t bar_y, bar_h;
    get_taskbar_metrics(&bar_y, &bar_h);
    uint32_t sb_w = 80;
    uint32_t sb_h = bar_h - 12;
    uint32_t sb_x = 8;
    uint32_t sb_y = bar_y + 6;
    if (x) *x = sb_x;
    if (y) *y = sb_y;
    if (w) *w = sb_w;
    if (h) *h = sb_h;
}

static void get_window_rect(uint32_t *x, uint32_t *y,
                            uint32_t *w, uint32_t *h) {
    uint32_t win_w = g_width * 3 / 5;
    uint32_t win_h = g_height * 3 / 5;
    uint32_t win_x = (g_width  - win_w) / 2;
    uint32_t win_y = (g_height - win_h) / 2 - g_height / 20;
    if (x) *x = win_x;
    if (y) *y = win_y;
    if (w) *w = win_w;
    if (h) *h = win_h;
}

static void get_close_button_rect(uint32_t *x, uint32_t *y,
                                  uint32_t *w, uint32_t *h) {
    uint32_t win_x, win_y, win_w, win_h;
    (void)win_h;
    get_window_rect(&win_x, &win_y, &win_w, &win_h);
    uint32_t title_h = 26;
    uint32_t cb_w = 18;
    uint32_t cb_x = win_x + win_w - cb_w - 6;
    uint32_t cb_y = win_y + 4;
    if (x) *x = cb_x;
    if (y) *y = cb_y;
    if (w) *w = cb_w;
    if (h) *h = title_h - 8;
}

// Start menu item hit test
static int hit_test_start_item(uint32_t mx, uint32_t my) {
    if (!g_start_open) return -1;

    uint32_t bar_h = g_height / 10;
    if (bar_h < 40) bar_h = 40;

    uint32_t menu_w = g_width / 4;
    if (menu_w < 200) menu_w = 200;
    uint32_t menu_h = bar_h * 2 + 40;
    uint32_t x      = 8;
    uint32_t y      = g_height - bar_h - menu_h - 4;

    if (mx < x || mx >= x + menu_w || my < y || my >= y + menu_h) {
        return -1;
    }

    uint32_t item_h = 24;
    uint32_t iy     = y + 10;

    for (int i = 0; i < 5; ++i) {
        uint32_t top = iy;
        uint32_t bottom = iy + item_h;
        if (my >= top && my < bottom) {
            return i;
        }
        iy += item_h + 4;
    }
    return -1;
}

// Dock icon hit test
static int hit_test_icon(uint32_t mx, uint32_t my) {
    for (int i = 0; i < 5; ++i) {
        uint32_t x, y, w, h;
        get_icon_geometry(i, &x, &y, &w, &h);
        if (mx >= x && mx < x + w && my >= y && my < y + h) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------
// Keyboard handling (desktop + apps)
// ---------------------------------------------------------------------

static void open_app_index(int idx) {
    g_open_app = idx;
    if (idx == 2) {
        term_reset(&g_term);
    } else if (idx == 3) {
        g_browser_page = 0;
    }
}

static void settings_handle_scancode(uint8_t sc) {
    if (sc & 0x80) return;
    if (sc == 0x01) { // Esc
        g_open_app = -1;
        return;
    }
    // 't' key toggles theme
    if (sc == 0x14) { // 't'
        g_theme = (g_theme == 0) ? 1 : 0;
    }
}

static void browser_handle_scancode(uint8_t sc) {
    if (sc & 0x80) return;
    if (sc == 0x01) { // Esc
        g_open_app = -1;
        return;
    }
    if (sc == 0x02) g_browser_page = 0; // '1'
    if (sc == 0x03) g_browser_page = 1; // '2'
    if (sc == 0x04) g_browser_page = 2; // '3'
}

static void startmenu_handle_scancode(uint8_t sc) {
    if (sc & 0x80) return;
    if (sc == 0x01) { // Esc
        g_start_open = 0;
        return;
    }
    if (sc == 0x48) { // Up
        if (g_start_selection > 0) g_start_selection--;
        return;
    }
    if (sc == 0x50) { // Down
        if (g_start_selection < 4) g_start_selection++;
        return;
    }
    if (sc == 0x1C) { // Enter
        open_app_index(g_start_selection);
        g_start_open = 0;
        return;
    }
}

// Desktop navigation when no app is active or for generic keys
static void handle_nav_scancode(uint8_t sc,
                                int *selected_icon,
                                int *open_app) {
    if (sc == 0xE0) return;      // ignore extended
    if (sc & 0x80) return;       // ignore releases

    // If specific apps are open, route keys there
    if (*open_app == 0) {
        settings_handle_scancode(sc);
        return;
    }
    if (*open_app == 1) {
        if (sc == 0x01) *open_app = -1; // Esc closes
        return;
    }
    if (*open_app == 3) {
        browser_handle_scancode(sc);
        return;
    }

    // Start menu navigation when open and no window focused
    if (*open_app == -1 && g_start_open) {
        startmenu_handle_scancode(sc);
        return;
    }

    // Toggle start menu from desktop with 's' key
    if (*open_app == -1 && sc == 0x1F) { // 's'
        g_start_open = !g_start_open;
        return;
    }

    // Arrow keys for dock navigation
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
            g_start_open = 0;
            *open_app = *selected_icon;
            if (*open_app == 2) term_reset(&g_term);
            if (*open_app == 3) g_browser_page = 0;
        }
        return;
    }

    if (sc == 0x01) {            // Esc
        *open_app = -1;
        return;
    }
}

// ---------------------------------------------------------------------
// Mouse click handling
// ---------------------------------------------------------------------

static void handle_mouse_click(uint32_t mx, uint32_t my) {
    // Start button
    {
        uint32_t x, y, w, h;
        get_start_button_rect(&x, &y, &w, &h);
        if (mx >= x && mx < x + w && my >= y && my < y + h) {
            g_start_open = !g_start_open;
            return;
        }
    }

    // Start menu item
    if (g_start_open && g_open_app == -1) {
        int item = hit_test_start_item(mx, my);
        if (item >= 0 && item < 5) {
            g_start_selection = item;
            open_app_index(item);
            g_start_open = 0;
            return;
        }
    }

    // Window close button
    if (g_open_app >= 0) {
        uint32_t x, y, w, h;
        get_close_button_rect(&x, &y, &w, &h);
        if (mx >= x && mx < x + w && my >= y && my < y + h) {
            g_open_app = -1;
            return;
        }
    }

    // Dock icons
    {
        int icon = hit_test_icon(mx, my);
        if (icon >= 0) {
            g_selected_icon = icon;
            open_app_index(icon);
            g_start_open = 0;
            return;
        }
    }

    // Clicking empty space closes start menu
    g_start_open = 0;
}

// ---------------------------------------------------------------------
// Kernel entry + main loop
// ---------------------------------------------------------------------

__attribute__((noreturn))
void kernel_main(BootInfo *bi) {
    g_fb     = (uint32_t*)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch ? bi->framebuffer_pitch
                                     : bi->framebuffer_width;

    g_year   = bi->year;
    g_month  = bi->month;
    g_day    = bi->day;
    g_hour   = bi->hour;
    g_minute = bi->minute;
    g_second = bi->second;

    vfs_init();
    mouse_init();

    run_boot_splash();

    g_selected_icon = 2;   // highlight Command Block
    g_open_app      = -1;  // none open yet
    g_start_open    = 0;
    g_start_selection = 2;

    term_reset(&g_term);
    draw_desktop(g_selected_icon, g_open_app);

    for (;;) {
        int need_redraw = 0;

        // Keyboard
        {
            uint8_t sc;
            if (keyboard_poll(&sc)) {
                int prev_sel  = g_selected_icon;
                int prev_open = g_open_app;
                int prev_start = g_start_open;

                if (g_open_app == 2) {
                    term_handle_scancode(sc, &g_term,
                                         &g_selected_icon,
                                         &g_open_app);
                } else {
                    handle_nav_scancode(sc, &g_selected_icon, &g_open_app);
                }

                if (g_selected_icon != prev_sel ||
                    g_open_app      != prev_open ||
                    g_start_open    != prev_start ||
                    g_open_app == 2) {
                    need_redraw = 1;
                }
            }
        }

        // Mouse
        {
            int dx = 0, dy = 0;
            uint8_t buttons = 0;
            if (mouse_poll(&dx, &dy, &buttons)) {
                int32_t old_x = g_mouse_x;
                int32_t old_y = g_mouse_y;
                uint8_t old_buttons = g_mouse_buttons;

                int32_t nx = g_mouse_x + dx;
                int32_t ny = g_mouse_y + dy;
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx >= (int32_t)g_width)  nx = (int32_t)g_width - 1;
                if (ny >= (int32_t)g_height) ny = (int32_t)g_height - 1;

                g_mouse_x       = nx;
                g_mouse_y       = ny;
                g_mouse_buttons = buttons;

                if (nx != old_x || ny != old_y) {
                    need_redraw = 1;
                }

                // Left-click edge
                if ((buttons & 0x01) && !(old_buttons & 0x01)) {
                    handle_mouse_click((uint32_t)nx, (uint32_t)ny);
                    need_redraw = 1;
                }
            }
        }

        if (need_redraw) {
            draw_desktop(g_selected_icon, g_open_app);
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
