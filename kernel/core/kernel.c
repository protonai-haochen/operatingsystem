// kernel/core/kernel.c
// LightOS 4 - UEFI framebuffer kernel
// Desktop + Command Block + File Block + Browser + Settings + Start menu
// NOTE: UI is "mouse-ready", but physical mouse + real networking still need drivers.

#include <stdint.h>
#include <stddef.h>
#include "boot.h"

// ---------------------------------------------------------------------
// Global framebuffer + time
// ---------------------------------------------------------------------

static uint32_t *g_fb    = 0;
static uint32_t  g_width = 0;
static uint32_t  g_height = 0;
static uint32_t  g_pitch  = 0;  // pixels per row

static uint16_t g_year  = 2026;
static uint8_t  g_month = 1;
static uint8_t  g_day   = 1;
static uint8_t  g_hour  = 0;
static uint8_t  g_minute = 0;
static uint8_t  g_second = 0;

// ---------------------------------------------------------------------
// Basic pixel ops
// ---------------------------------------------------------------------

static void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_fb) return;
    if (x >= g_width || y >= g_height) return;
    g_fb[(uint64_t)y * g_pitch + x] = color;
}

static void fill_rect(uint32_t x, uint32_t y,
                      uint32_t w, uint32_t h,
                      uint32_t color) {
    if (!g_fb) return;
    if (x >= g_width || y >= g_height) return;
    if (x + w > g_width)  w = g_width  - x;
    if (y + h > g_height) h = g_height - y;
    for (uint32_t j = 0; j < h; ++j) {
        uint32_t *row = &g_fb[(uint64_t)(y + j) * g_pitch + x];
        for (uint32_t i = 0; i < w; ++i) {
            row[i] = color;
        }
    }
}

static void draw_rect_border(uint32_t x, uint32_t y,
                             uint32_t w, uint32_t h,
                             uint32_t color) {
    if (!g_fb) return;
    if (w < 2 || h < 2) return;
    for (uint32_t i = 0; i < w; ++i) {
        put_pixel(x + i, y,         color);
        put_pixel(x + i, y + h - 1, color);
    }
    for (uint32_t j = 0; j < h; ++j) {
        put_pixel(x,         y + j, color);
        put_pixel(x + w - 1, y + j, color);
    }
}

// ---------------------------------------------------------------------
// 8x8 Font (minimal; uppercase letters + digits + some punctuation)
// ---------------------------------------------------------------------

typedef struct {
    char    c;
    uint8_t rows[8];
} Glyph8;

static const Glyph8 FONT8[] = {
    // Digits
    { '0', { 0x3C,0x42,0x46,0x4A,0x52,0x62,0x3C,0x00 } },
    { '1', { 0x08,0x18,0x28,0x08,0x08,0x08,0x3E,0x00 } },
    { '2', { 0x3C,0x42,0x02,0x1C,0x20,0x40,0x7E,0x00 } },
    { '3', { 0x3C,0x42,0x02,0x1C,0x02,0x42,0x3C,0x00 } },
    { '4', { 0x04,0x0C,0x14,0x24,0x44,0x7E,0x04,0x00 } },
    { '5', { 0x7E,0x40,0x7C,0x02,0x02,0x42,0x3C,0x00 } },
    { '6', { 0x1C,0x20,0x40,0x7C,0x42,0x42,0x3C,0x00 } },
    { '7', { 0x7E,0x02,0x04,0x08,0x10,0x20,0x20,0x00 } },
    { '8', { 0x3C,0x42,0x42,0x3C,0x42,0x42,0x3C,0x00 } },
    { '9', { 0x3C,0x42,0x42,0x3E,0x02,0x04,0x38,0x00 } },

    // Uppercase letters
    { 'A', { 0x10,0x28,0x44,0x44,0x7C,0x44,0x44,0x00 } },
    { 'B', { 0x78,0x44,0x44,0x78,0x44,0x44,0x78,0x00 } },
    { 'C', { 0x3C,0x42,0x40,0x40,0x40,0x42,0x3C,0x00 } },
    { 'D', { 0x78,0x44,0x42,0x42,0x42,0x44,0x78,0x00 } },
    { 'E', { 0x7E,0x40,0x40,0x7C,0x40,0x40,0x7E,0x00 } },
    { 'F', { 0x7E,0x40,0x40,0x7C,0x40,0x40,0x40,0x00 } },
    { 'G', { 0x3C,0x42,0x40,0x4E,0x42,0x42,0x3C,0x00 } },
    { 'H', { 0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x00 } },
    { 'I', { 0x3E,0x08,0x08,0x08,0x08,0x08,0x3E,0x00 } },
    { 'J', { 0x0E,0x04,0x04,0x04,0x44,0x44,0x38,0x00 } },
    { 'K', { 0x42,0x44,0x48,0x70,0x48,0x44,0x42,0x00 } },
    { 'L', { 0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00 } },
    { 'M', { 0x42,0x66,0x5A,0x5A,0x42,0x42,0x42,0x00 } },
    { 'N', { 0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x00 } },
    { 'O', { 0x3C,0x42,0x42,0x42,0x42,0x42,0x3C,0x00 } },
    { 'P', { 0x7C,0x42,0x42,0x7C,0x40,0x40,0x40,0x00 } },
    { 'Q', { 0x3C,0x42,0x42,0x42,0x4A,0x44,0x3A,0x00 } },
    { 'R', { 0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0x00 } },
    { 'S', { 0x3C,0x40,0x40,0x3C,0x02,0x02,0x3C,0x00 } },
    { 'T', { 0x7F,0x49,0x08,0x08,0x08,0x08,0x1C,0x00 } },
    { 'U', { 0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00 } },
    { 'V', { 0x42,0x42,0x42,0x24,0x24,0x18,0x18,0x00 } },
    { 'W', { 0x42,0x42,0x5A,0x5A,0x5A,0x66,0x42,0x00 } },
    { 'X', { 0x42,0x24,0x18,0x18,0x18,0x24,0x42,0x00 } },
    { 'Y', { 0x42,0x24,0x18,0x18,0x18,0x18,0x18,0x00 } },
    { 'Z', { 0x7E,0x02,0x04,0x08,0x10,0x20,0x7E,0x00 } },

    // Basic punctuation and symbols used by the shell
    { ' ', { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
    { '>', { 0x00,0x40,0x20,0x10,0x20,0x40,0x00,0x00 } },
    { '<', { 0x00,0x02,0x04,0x08,0x04,0x02,0x00,0x00 } },
    { ':', { 0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00 } },
    { '.', { 0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00 } },
    { ',', { 0x00,0x00,0x00,0x00,0x18,0x18,0x10,0x20 } },
    { '/', { 0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00 } },
    { '\\',{ 0x40,0x20,0x10,0x08,0x04,0x02,0x00,0x00 } },
    { '-', { 0x00,0x00,0x00,0x3C,0x00,0x00,0x00,0x00 } },
    { '_', { 0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00 } },
    { '=', { 0x00,0x00,0x3C,0x00,0x3C,0x00,0x00,0x00 } },
    { '[', { 0x1E,0x10,0x10,0x10,0x10,0x10,0x1E,0x00 } },
    { ']', { 0x78,0x08,0x08,0x08,0x08,0x08,0x78,0x00 } },
    { '(', { 0x0C,0x10,0x20,0x20,0x20,0x10,0x0C,0x00 } },
    { ')', { 0x30,0x08,0x04,0x04,0x04,0x08,0x30,0x00 } },
    { '?', { 0x3C,0x42,0x02,0x0C,0x10,0x00,0x10,0x00 } },
    { '!', { 0x08,0x08,0x08,0x08,0x08,0x00,0x08,0x00 } },
    { '|', { 0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x00 } },
};

static const uint8_t *font_lookup(char c) {
    // Lowercase -> uppercase to reuse glyphs
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (unsigned i = 0; i < sizeof(FONT8)/sizeof(FONT8[0]); ++i) {
        if (FONT8[i].c == c) return FONT8[i].rows;
    }
    // Fallback: '?'
    for (unsigned i = 0; i < sizeof(FONT8)/sizeof(FONT8[0]); ++i) {
        if (FONT8[i].c == '?') return FONT8[i].rows;
    }
    return NULL;
}

static void draw_char(uint32_t x, uint32_t y, char c,
                      uint32_t color, uint32_t scale) {
    const uint8_t *rows = font_lookup(c);
    if (!rows) return;
    for (uint32_t row = 0; row < 8; ++row) {
        uint8_t bits = rows[row];
        for (uint32_t col = 0; col < 8; ++col) {
            if (bits & (1u << (7 - col))) {
                for (uint32_t yy = 0; yy < scale; ++yy) {
                    for (uint32_t xx = 0; xx < scale; ++xx) {
                        put_pixel(x + col*scale + xx,
                                  y + row*scale + yy,
                                  color);
                    }
                }
            }
        }
    }
}

static void draw_text(uint32_t x, uint32_t y,
                      const char *s,
                      uint32_t color,
                      uint32_t scale) {
    if (!s) return;
    uint32_t cx = x;
    while (*s) {
        if (*s == '\n') {
            y += 8 * scale + 2;
            cx = x;
        } else {
            draw_char(cx, y, *s, color, scale);
            cx += 8 * scale;
        }
        ++s;
    }
}

// ---------------------------------------------------------------------
// Tiny string helpers
// ---------------------------------------------------------------------

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
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

static void str_cat(char *dst, const char *src, uint32_t max_len) {
    uint32_t len = str_len(dst);
    uint32_t i = 0;
    while (len + 1 < max_len && src && src[i]) {
        dst[len++] = src[i++];
    }
    dst[len] = '\0';
}

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (*a != *b) return 0;
        ++a; ++b;
    }
    return (*a == '\0' && *b == '\0');
}

// Skip leading spaces
static const char *skip_spaces(const char *p) {
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

// Extract next word; returns pointer to char after word
static const char *next_word(const char *p, char *out, uint32_t max_len) {
    p = skip_spaces(p);
    uint32_t i = 0;
    while (*p && *p != ' ' && *p != '\t') {
        if (i + 1 < max_len) {
            out[i++] = *p;
        }
        ++p;
    }
    out[i] = '\0';
    return p;
}

// ---------------------------------------------------------------------
// Time formatting
// ---------------------------------------------------------------------

static void format_time(char *buf, uint32_t max) {
    if (max < 9) { if (max) buf[0] = '\0'; return; }
    buf[0] = (char)('0' + (g_hour / 10));
    buf[1] = (char)('0' + (g_hour % 10));
    buf[2] = ':';
    buf[3] = (char)('0' + (g_minute / 10));
    buf[4] = (char)('0' + (g_minute % 10));
    buf[5] = ':';
    buf[6] = (char)('0' + (g_second / 10));
    buf[7] = (char)('0' + (g_second % 10));
    buf[8] = '\0';
}

static void format_date(char *buf, uint32_t max) {
    // YYYY-MM-DD
    if (max < 11) { if (max) buf[0] = '\0'; return; }
    buf[0] = (char)('0' + ((g_year / 1000) % 10));
    buf[1] = (char)('0' + ((g_year / 100)  % 10));
    buf[2] = (char)('0' + ((g_year / 10)   % 10));
    buf[3] = (char)('0' + ( g_year         % 10));
    buf[4] = '-';
    buf[5] = (char)('0' + (g_month / 10));
    buf[6] = (char)('0' + (g_month % 10));
    buf[7] = '-';
    buf[8] = (char)('0' + (g_day / 10));
    buf[9] = (char)('0' + (g_day % 10));
    buf[10] = '\0';
}

// ---------------------------------------------------------------------
// Boot splash (no trig / libm)
// ---------------------------------------------------------------------

static void run_boot_splash(void) {
    // Simple dark screen with LightOS logo and spinner
    fill_rect(0, 0, g_width, g_height, 0x001020u);
    const char *name = "LightOS 4";
    uint32_t name_w = 9 * 8; // approx, assuming scale 2
    uint32_t x = (g_width  - name_w * 2) / 2;
    uint32_t y = g_height / 3;
    draw_text(x, y, name, 0xFFFFFFu, 2);

    // Spinner under the name, not overlapping
    uint32_t cx = g_width / 2;
    uint32_t cy = y + 80;
    int32_t  r  = 16;

    // 8 precomputed offsets around a circle-ish shape
    static const int8_t off_x[8] = {  0,  6, 10,  6,  0, -6,-10, -6 };
    static const int8_t off_y[8] = { -10,-6,  0,  6, 10,  6,  0, -6 };

    for (int step = 0; step < 64; ++step) {
        // clear ring area
        for (int32_t dy = -r-2; dy <= r+2; ++dy) {
            for (int32_t dx = -r-2; dx <= r+2; ++dx) {
                int32_t px = (int32_t)cx + dx;
                int32_t py = (int32_t)cy + dy;
                if (px >= 0 && py >= 0 &&
                    (uint32_t)px < g_width &&
                    (uint32_t)py < g_height) {
                    g_fb[(uint64_t)py * g_pitch + (uint32_t)px] = 0x001020u;
                }
            }
        }
        // draw ring
        for (int32_t dy = -r; dy <= r; ++dy) {
            for (int32_t dx = -r; dx <= r; ++dx) {
                int32_t d2 = dx*dx + dy*dy;
                int32_t r1 = (r-1)*(r-1);
                int32_t r2 = (r+1)*(r+1);
                if (d2 >= r1 && d2 <= r2) {
                    int32_t px = (int32_t)cx + dx;
                    int32_t py = (int32_t)cy + dy;
                    if (px >= 0 && py >= 0 &&
                        (uint32_t)px < g_width &&
                        (uint32_t)py < g_height) {
                        g_fb[(uint64_t)py * g_pitch + (uint32_t)px] = 0x5555FFu;
                    }
                }
            }
        }
        // draw one "chunk" lit using lookup offsets
        int idx = step & 7; // step % 8
        int32_t hx = (int32_t)cx + off_x[idx];
        int32_t hy = (int32_t)cy + off_y[idx];
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                int32_t px = hx + dx;
                int32_t py = hy + dy;
                if (px >= 0 && py >= 0 &&
                    (uint32_t)px < g_width &&
                    (uint32_t)py < g_height) {
                    g_fb[(uint64_t)py * g_pitch + (uint32_t)px] = 0xFFFFFFu;
                }
            }
        }
        // crude delay
        for (volatile uint32_t w = 0; w < 2000000; ++w) { }
    }
}

// ---------------------------------------------------------------------
// I/O ports for keyboard (and possible PS/2 mouse later)
// ---------------------------------------------------------------------

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static int keyboard_poll(uint8_t *scancode) {
    if (!scancode) return 0;
    // Status port 0x64, data port 0x60
    if (!(inb(0x64) & 0x01)) return 0;
    *scancode = inb(0x60);
    return 1;
}

// ---------------------------------------------------------------------
// Mouse state (UI-level). Hardware driver still TODO.
// ---------------------------------------------------------------------

typedef struct {
    int32_t x;
    int32_t y;
    uint8_t left_down;
    uint8_t right_down;
} MouseState;

static MouseState g_mouse = { 40, 40, 0, 0 };

// Simple arrow cursor
static void draw_mouse_cursor(void) {
    const uint32_t col_fg = 0xFFFFFFu;
    const uint32_t col_bd = 0x000000u;
    int32_t base_x = g_mouse.x;
    int32_t base_y = g_mouse.y;
    int32_t h = 16;

    for (int32_t row = 0; row < h; ++row) {
        for (int32_t col = 0; col <= row; ++col) {
            int32_t x = base_x + col;
            int32_t y = base_y + row;
            if (x < 0 || y < 0 ||
                (uint32_t)x >= g_width ||
                (uint32_t)y >= g_height) continue;
            uint32_t color = (col == 0 || row == 0 || col == row) ? col_bd : col_fg;
            g_fb[(uint64_t)y * g_pitch + (uint32_t)x] = color;
        }
    }
}

// ---------------------------------------------------------------------
// Terminal + VFS
// ---------------------------------------------------------------------

#define TERM_MAX_LINES 32
#define TERM_MAX_COLS  80

typedef struct {
    char     lines[TERM_MAX_LINES][TERM_MAX_COLS];
    uint32_t line_count;
    char     input[TERM_MAX_COLS];
    uint32_t input_len;
} TerminalState;

static TerminalState g_term;

// Simple "filesystem" in RAM
#define VFS_MAX_NODES   128
#define VFS_NAME_LEN    32
#define VFS_CONTENT_LEN 512

typedef enum {
    VFS_DIR,
    VFS_FILE
} VfsType;

typedef struct {
    VfsType type;
    int     parent;                   // index of parent, -1 for root
    char    name[VFS_NAME_LEN];       // name only, no path separators
    char    content[VFS_CONTENT_LEN]; // used only for files
} VfsNode;

static VfsNode g_vfs[VFS_MAX_NODES];
static int     g_vfs_count = 0;
static int     g_cwd       = 0;       // index of current directory (0 = root)

// Add node helpers
static int vfs_add_node(VfsType type, int parent, const char *name) {
    if (g_vfs_count >= VFS_MAX_NODES) return -1;
    int idx = g_vfs_count++;
    g_vfs[idx].type   = type;
    g_vfs[idx].parent = parent;
    str_copy(g_vfs[idx].name, name ? name : "", VFS_NAME_LEN);
    if (type == VFS_FILE) {
        g_vfs[idx].content[0] = '\0';
    }
    return idx;
}

static int vfs_find_child(int parent, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < g_vfs_count; ++i) {
        if (g_vfs[i].parent == parent &&
            str_eq(g_vfs[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int vfs_is_empty_dir(int idx) {
    if (idx < 0 || idx >= g_vfs_count) return 0;
    if (g_vfs[idx].type != VFS_DIR) return 0;
    for (int i = 0; i < g_vfs_count; ++i) {
        if (g_vfs[i].parent == idx) return 0;
    }
    return 1;
}

static void vfs_delete_node(int idx) {
    if (idx <= 0 || idx >= g_vfs_count) return; // don't delete root
    // compact array
    for (int i = idx + 1; i < g_vfs_count; ++i) {
        g_vfs[i - 1] = g_vfs[i];
    }
    g_vfs_count--;
    // fix parent indices
    for (int i = 0; i < g_vfs_count; ++i) {
        if (g_vfs[i].parent == idx) {
            // orphaned children; move to root
            g_vfs[i].parent = 0;
        } else if (g_vfs[i].parent > idx) {
            g_vfs[i].parent--;
        }
    }
    if (g_cwd == idx) g_cwd = 0;
    if (g_cwd > idx)  g_cwd--;
}

static void vfs_init(void) {
    g_vfs_count = 0;
    int root = vfs_add_node(VFS_DIR, -1, "");
    (void)root; // should be 0

    int docs = vfs_add_node(VFS_DIR, 0, "docs");
    int etc  = vfs_add_node(VFS_DIR, 0, "etc");

    int readme = vfs_add_node(VFS_FILE, docs, "readme.txt");
    if (readme >= 0) {
        str_copy(g_vfs[readme].content,
                 "Welcome to LightOS 4.\n"
                 "This is a toy RAM filesystem.\n"
                 "Use 'dir', 'cd', 'mkdir', 'touch', 'type', etc.\n",
                 VFS_CONTENT_LEN);
    }

    int conf = vfs_add_node(VFS_FILE, etc, "system.conf");
    if (conf >= 0) {
        str_copy(g_vfs[conf].content,
                 "# LightOS 4 config (demo)\n"
                 "theme=light\n",
                 VFS_CONTENT_LEN);
    }

    g_cwd = 0;
}

// Build string like "C:\docs"
static void vfs_build_path(char *buf, uint32_t max_len, int node_index) {
    char tmp[128];
    tmp[0] = '\0';

    // climb parents
    int stack[16];
    int depth = 0;
    int cur = node_index;
    while (cur > 0 && depth < 16) {
        stack[depth++] = cur;
        cur = g_vfs[cur].parent;
    }

    // root
    str_copy(tmp, "C:\\", sizeof(tmp));

    for (int i = depth - 1; i >= 0; --i) {
        str_cat(tmp, g_vfs[stack[i]].name, sizeof(tmp));
        if (i > 0) str_cat(tmp, "\\", sizeof(tmp));
    }

    str_copy(buf, tmp, max_len);
}

// ---------------------------------------------------------------------
// Terminal helpers
// ---------------------------------------------------------------------

static void term_add_line(TerminalState *t, const char *text);

static void vfs_list_dir_to_terminal(TerminalState *t, int dir_index) {
    char line[TERM_MAX_COLS];
    char path[64];
    vfs_build_path(path, sizeof(path), dir_index);

    str_copy(line, " Directory of ", TERM_MAX_COLS);
    str_cat(line, path, TERM_MAX_COLS);
    term_add_line(t, line);
    term_add_line(t, "");

    for (int i = 0; i < g_vfs_count; ++i) {
        if (g_vfs[i].parent != dir_index) continue;

        char entry[TERM_MAX_COLS];
        if (g_vfs[i].type == VFS_DIR) {
            str_copy(entry, "<DIR>  ", TERM_MAX_COLS);
        } else {
            str_copy(entry, "       ", TERM_MAX_COLS);
        }
        str_cat(entry, g_vfs[i].name, TERM_MAX_COLS);
        term_add_line(t, entry);
    }
}

static void term_reset(TerminalState *t) {
    if (!t) return;
    t->line_count = 0;
    t->input_len  = 0;
    t->input[0]   = '\0';

    term_add_line(t, "LightOS 4 Command Block");
    term_add_line(t, "Type 'help' for commands.");
    term_add_line(t, "");
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

// Find node by path relative to g_cwd (simple, one-component names)
static int vfs_resolve_simple(const char *name, int expect_dir, int *out_parent) {
    if (!name || !*name) {
        if (out_parent) *out_parent = g_cwd;
        return g_cwd;
    }

    // Handle "C:\something" (strip drive)
    if ((name[0] == 'C' || name[0] == 'c') && name[1] == ':' &&
        (name[2] == '\\' || name[2] == '/')) {
        name += 3;
        g_cwd = 0;
    }

    if (str_eq(name, "/") || str_eq(name, "\\")) {
        if (out_parent) *out_parent = 0;
        return 0;
    }
    if (str_eq(name, ".")) {
        if (out_parent) *out_parent = g_cwd;
        return g_cwd;
    }
    if (str_eq(name, "..")) {
        int parent = g_vfs[g_cwd].parent;
        if (parent < 0) parent = 0;
        if (out_parent) *out_parent = parent;
        return parent;
    }

    int parent = g_cwd;
    int child  = vfs_find_child(parent, name);
    if (child < 0) {
        if (out_parent) *out_parent = parent;
        return -1;
    }
    if (expect_dir && g_vfs[child].type != VFS_DIR) {
        if (out_parent) *out_parent = parent;
        return -1;
    }
    if (out_parent) *out_parent = parent;
    return child;
}

static void term_print_prompt_path(char *buf, uint32_t max_len) {
    // Build Windows-style prompt like: C:\docs>
    char path[64];
    vfs_build_path(path, sizeof(path), g_cwd);
    str_copy(buf, path, max_len);
    str_cat(buf, ">", max_len);
}

// ---------------------------------------------------------------------
// Command execution
// ---------------------------------------------------------------------

static void term_execute_command(TerminalState *t, const char *cmd) {
    if (!cmd || !*cmd) return;

    // Skip leading whitespace
    cmd = skip_spaces(cmd);
    if (!*cmd) return;

    char word[32];
    const char *rest = next_word(cmd, word, sizeof(word));

    // ---------------- help ----------------
    if (str_eq(word, "help") || str_eq(word, "?")) {
        term_add_line(t, "Commands (Windows + Linux style):");
        term_add_line(t, "  cls / clear");
        term_add_line(t, "  dir / ls");
        term_add_line(t, "  cd / chdir [dir]");
        term_add_line(t, "  mkdir / md <name>");
        term_add_line(t, "  rmdir / rd <name>");
        term_add_line(t, "  touch / create / mkfile <name>");
        term_add_line(t, "  del / erase / rm <name>");
        term_add_line(t, "  type / cat <file>");
        term_add_line(t, "  copy / cp <src> <dst>");
        term_add_line(t, "  move / mv <src> <dst>");
        term_add_line(t, "  pwd");
        term_add_line(t, "  ver / uname");
        term_add_line(t, "  time / date");
        term_add_line(t, "  echo <text>");
        return;
    }

    // ---------------- cls / clear ----------------
    if (str_eq(word, "cls") || str_eq(word, "clear")) {
        term_reset(t);
        return;
    }

    // ---------------- dir / ls ----------------
    if (str_eq(word, "dir") || str_eq(word, "ls")) {
        vfs_list_dir_to_terminal(t, g_cwd);
        return;
    }

    // ---------------- cd / chdir ----------------
    if (str_eq(word, "cd") || str_eq(word, "chdir")) {
        char arg[64];
        rest = next_word(rest, arg, sizeof(arg));
        if (!arg[0]) {
            // print current directory
            char path[64];
            vfs_build_path(path, sizeof(path), g_cwd);
            term_add_line(t, path);
            return;
        }
        int parent = 0;
        int newdir = vfs_resolve_simple(arg, /*expect_dir=*/1, &parent);
        if (newdir < 0) {
            char msg[TERM_MAX_COLS];
            str_copy(msg, "The system cannot find the path specified: ", TERM_MAX_COLS);
            str_cat(msg, arg, TERM_MAX_COLS);
            term_add_line(t, msg);
            return;
        }
        g_cwd = newdir;
        return;
    }

    // ---------------- mkdir / md ----------------
    if (str_eq(word, "mkdir") || str_eq(word, "md")) {
        char name[64];
        rest = next_word(rest, name, sizeof(name));
        if (!name[0]) {
            term_add_line(t, "mkdir: missing directory name.");
            return;
        }
        if (vfs_find_child(g_cwd, name) >= 0) {
            term_add_line(t, "mkdir: directory or file already exists.");
            return;
        }
        if (vfs_add_node(VFS_DIR, g_cwd, name) < 0) {
            term_add_line(t, "mkdir: no space left in VFS.");
        }
        return;
    }

    // ---------------- rmdir / rd ----------------
    if (str_eq(word, "rmdir") || str_eq(word, "rd")) {
        char name[64];
        rest = next_word(rest, name, sizeof(name));
        if (!name[0]) {
            term_add_line(t, "rmdir: missing directory name.");
            return;
        }
        int idx = vfs_find_child(g_cwd, name);
        if (idx < 0 || g_vfs[idx].type != VFS_DIR) {
            term_add_line(t, "rmdir: not a directory or not found.");
            return;
        }
        if (!vfs_is_empty_dir(idx)) {
            term_add_line(t, "rmdir: directory not empty.");
            return;
        }
        vfs_delete_node(idx);
        return;
    }

    // ---------------- touch / create / mkfile ----------------
    if (str_eq(word, "touch") || str_eq(word, "create") || str_eq(word, "mkfile")) {
        char name[64];
        rest = next_word(rest, name, sizeof(name));
        if (!name[0]) {
            term_add_line(t, "touch: missing file name.");
            return;
        }
        int idx = vfs_find_child(g_cwd, name);
        if (idx >= 0) {
            if (g_vfs[idx].type == VFS_DIR) {
                term_add_line(t, "touch: already exists as directory.");
                return;
            }
            // Existing file; we won't change content
            return;
        }
        idx = vfs_add_node(VFS_FILE, g_cwd, name);
        if (idx < 0) {
            term_add_line(t, "touch: no space left in VFS.");
            return;
        }
        g_vfs[idx].content[0] = '\0';
        return;
    }

    // ---------------- del / erase / rm ----------------
    if (str_eq(word, "del") || str_eq(word, "erase") || str_eq(word, "rm")) {
        char name[64];
        rest = next_word(rest, name, sizeof(name));
        if (!name[0]) {
            term_add_line(t, "del: missing file name.");
            return;
        }
        int idx = vfs_find_child(g_cwd, name);
        if (idx < 0 || g_vfs[idx].type != VFS_FILE) {
            term_add_line(t, "del: file not found.");
            return;
        }
        vfs_delete_node(idx);
        return;
    }

    // ---------------- type / cat ----------------
    if (str_eq(word, "type") || str_eq(word, "cat")) {
        char name[64];
        rest = next_word(rest, name, sizeof(name));
        if (!name[0]) {
            term_add_line(t, "type: missing file name.");
            return;
        }
        int idx = vfs_find_child(g_cwd, name);
        if (idx < 0 || g_vfs[idx].type != VFS_FILE) {
            term_add_line(t, "type: file not found.");
            return;
        }
        if (!g_vfs[idx].content[0]) {
            term_add_line(t, "(empty file)");
        } else {
            // Split on '\n' to multiple lines
            char buf[VFS_CONTENT_LEN];
            str_copy(buf, g_vfs[idx].content, sizeof(buf));
            char *p = buf;
            while (*p) {
                char *line = p;
                while (*p && *p != '\n') ++p;
                char saved = *p;
                *p = '\0';
                term_add_line(t, line);
                if (saved == '\n') {
                    *p = saved;
                    ++p;
                }
            }
        }
        return;
    }

    // ---------------- copy / cp ----------------
    if (str_eq(word, "copy") || str_eq(word, "cp")) {
        char src[64], dst[64];
        rest = next_word(rest, src, sizeof(src));
        rest = next_word(rest, dst, sizeof(dst));
        if (!src[0] || !dst[0]) {
            term_add_line(t, "copy: usage: copy <src> <dst>");
            return;
        }
        int sidx = vfs_find_child(g_cwd, src);
        if (sidx < 0 || g_vfs[sidx].type != VFS_FILE) {
            term_add_line(t, "copy: src file not found.");
            return;
        }
        int didx = vfs_find_child(g_cwd, dst);
        if (didx >= 0 && g_vfs[didx].type == VFS_DIR) {
            term_add_line(t, "copy: dst is a directory (not supported).");
            return;
        }
        if (didx < 0) {
            didx = vfs_add_node(VFS_FILE, g_cwd, dst);
            if (didx < 0) {
                term_add_line(t, "copy: no space left in VFS.");
                return;
            }
        }
        str_copy(g_vfs[didx].content, g_vfs[sidx].content, VFS_CONTENT_LEN);
        return;
    }

    // ---------------- move / mv ----------------
    if (str_eq(word, "move") || str_eq(word, "mv")) {
        char src[64], dst[64];
        rest = next_word(rest, src, sizeof(src));
        rest = next_word(rest, dst, sizeof(dst));
        if (!src[0] || !dst[0]) {
            term_add_line(t, "move: usage: move <src> <dst>");
            return;
        }
        int sidx = vfs_find_child(g_cwd, src);
        if (sidx < 0) {
            term_add_line(t, "move: src not found.");
            return;
        }
        // Only rename, no path changes yet
        str_copy(g_vfs[sidx].name, dst, VFS_NAME_LEN);
        return;
    }

    // ---------------- pwd ----------------
    if (str_eq(word, "pwd")) {
        char path[64];
        vfs_build_path(path, sizeof(path), g_cwd);
        term_add_line(t, path);
        return;
    }

    // ---------------- ver / uname ----------------
    if (str_eq(word, "ver") || str_eq(word, "uname")) {
        term_add_line(t, "LightOS 4 demo kernel (x86_64, UEFI framebuffer).");
        return;
    }

    // ---------------- time / date ----------------
    if (str_eq(word, "time") || str_eq(word, "date")) {
        char buf[32];
        char tbuf[16];
        char dbuf[16];
        format_time(tbuf, sizeof(tbuf));
        format_date(dbuf, sizeof(dbuf));
        str_copy(buf, dbuf, sizeof(buf));
        str_cat(buf, " ", sizeof(buf));
        str_cat(buf, tbuf, sizeof(buf));
        term_add_line(t, buf);
        return;
    }

    // ---------------- echo ----------------
    if (str_eq(word, "echo")) {
        rest = skip_spaces(rest);
        term_add_line(t, rest);
        return;
    }

    // Fallback
    {
        char msg[TERM_MAX_COLS];
        str_copy(msg, "Unknown command: ", TERM_MAX_COLS);
        str_cat(msg, word, TERM_MAX_COLS);
        str_cat(msg, " (type 'help')", TERM_MAX_COLS);
        term_add_line(t, msg);
    }
}

// ---------------------------------------------------------------------
// Keyboard input → terminal (with Shift + punctuation)
// ---------------------------------------------------------------------

static int g_shift_down = 0;

static void term_handle_scancode(uint8_t sc, TerminalState *t,
                                 int *selected_icon, int *open_app) {
    (void)selected_icon; // unused while inside Command Block

    // Handle Shift explicitly (both press & release)
    if (sc == 0x2A || sc == 0x36) { // LShift / RShift press
        g_shift_down = 1;
        return;
    }
    if (sc == 0xAA || sc == 0xB6) { // LShift / RShift release
        g_shift_down = 0;
        return;
    }

    // Ignore other key releases
    if (sc & 0x80) return;

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
        // Echo prompt + command into buffer lines
        char prompt[TERM_MAX_COLS];
        term_print_prompt_path(prompt, sizeof(prompt));
        char line[TERM_MAX_COLS];
        str_copy(line, prompt, sizeof(line));
        str_cat(line, " ", sizeof(line));
        str_cat(line, t->input, sizeof(line));
        term_add_line(t, line);

        term_execute_command(t, t->input);
        t->input_len = 0;
        t->input[0]  = '\0';
        return;
    }

    // basic scancode → ASCII (with simple shift)
    char c = 0;
    switch (sc) {
        // Number row
        case 0x02: c = g_shift_down ? '!' : '1'; break;
        case 0x03: c = g_shift_down ? '@' : '2'; break;
        case 0x04: c = g_shift_down ? '#' : '3'; break;
        case 0x05: c = g_shift_down ? '$' : '4'; break;
        case 0x06: c = g_shift_down ? '%' : '5'; break;
        case 0x07: c = g_shift_down ? '^' : '6'; break;
        case 0x08: c = g_shift_down ? '&' : '7'; break;
        case 0x09: c = g_shift_down ? '*' : '8'; break;
        case 0x0A: c = g_shift_down ? '(' : '9'; break;
        case 0x0B: c = g_shift_down ? ')' : '0'; break;
        case 0x0C: c = g_shift_down ? '_' : '-'; break;
        case 0x0D: c = g_shift_down ? '+' : '='; break;

        // QWERTY rows
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

        case 0x1A: c = g_shift_down ? '{' : '['; break;
        case 0x1B: c = g_shift_down ? '}' : ']'; break;

        case 0x1E: c = 'a'; break;
        case 0x1F: c = 's'; break;
        case 0x20: c = 'd'; break;
        case 0x21: c = 'f'; break;
        case 0x22: c = 'g'; break;
        case 0x23: c = 'h'; break;
        case 0x24: c = 'j'; break;
        case 0x25: c = 'k'; break;
        case 0x26: c = 'l'; break;
        case 0x27: c = g_shift_down ? ':' : ';'; break;
        case 0x28: c = g_shift_down ? '"' : '\''; break;
        case 0x2B: c = g_shift_down ? '|' : '\\'; break;

        case 0x2C: c = 'z'; break;
        case 0x2D: c = 'x'; break;
        case 0x2E: c = 'c'; break;
        case 0x2F: c = 'v'; break;
        case 0x30: c = 'b'; break;
        case 0x31: c = 'n'; break;
        case 0x32: c = 'm'; break;
        case 0x33: c = g_shift_down ? '<' : ','; break;
        case 0x34: c = g_shift_down ? '>' : '.'; break;
        case 0x35: c = g_shift_down ? '?' : '/'; break;

        case 0x39: c = ' '; break; // space

        default: break;
    }

    if (c && t->input_len < TERM_MAX_COLS - 1) {
        t->input[t->input_len++] = c;
        t->input[t->input_len]   = '\0';
    }
}

// ---------------------------------------------------------------------
// Desktop & apps UI
// ---------------------------------------------------------------------

// Start menu state (simple toggle)
static int g_start_open = 0;

// Draw ChromeOS-like flat background
static void draw_desktop_background(void) {
    // simple gradient-ish: dark blue at top, lighter towards bottom
    for (uint32_t y = 0; y < g_height; ++y) {
        uint8_t shade = (uint8_t)(0x20 + (y * 80 / (g_height ? g_height : 1)));
        uint32_t col = (0x00 << 16) | (shade << 8) | 0x80;
        for (uint32_t x = 0; x < g_width; ++x) {
            g_fb[(uint64_t)y * g_pitch + x] = col;
        }
    }
}

// Taskbar with clock + battery + fake wifi
static void draw_taskbar(void) {
    uint32_t bar_h = g_height / 12;
    if (bar_h < 40) bar_h = 40;
    uint32_t y = g_height - bar_h;

    fill_rect(0, y, g_width, bar_h, 0x202428u);

    // "Start" / launcher button (left)
    uint32_t sx = 8;
    uint32_t sy = y + 6;
    uint32_t sw = 80;
    uint32_t sh = bar_h - 12;
    fill_rect(sx, sy, sw, sh, 0x303840u);
    draw_rect_border(sx, sy, sw, sh, 0x505860u);
    draw_text(sx + 8, sy + (sh / 2) - 6, "Start", 0xFFFFFFu, 1);

    // Right side: time/date + battery + wifi
    char tbuf[16], dbuf[16];
    format_time(tbuf, sizeof(tbuf));
    format_date(dbuf, sizeof(dbuf));

    uint32_t tx = g_width - 220;
    draw_text(tx, y + 6, tbuf, 0xFFFFFFu, 1);
    draw_text(tx, y + 22, dbuf, 0xC0C0C0u, 1);

    // battery icon
    uint32_t bx = g_width - 80;
    uint32_t by = y + 8;
    uint32_t bw = 32;
    uint32_t bh = 16;
    draw_rect_border(bx, by, bw, bh, 0xFFFFFFu);
    fill_rect(bx + 3, by + 3, bw - 6, bh - 6, 0x80FF80u);
    // battery "nub"
    fill_rect(bx + bw, by + 4, 3, bh - 8, 0xFFFFFFu);

    // fake wifi bars
    uint32_t wx = g_width - 120;
    uint32_t wy = y + 8;
    fill_rect(wx +  0, wy + 10, 4, 6, 0xFFFFFFu);
    fill_rect(wx +  6, wy +  6, 4, 10, 0xFFFFFFu);
    fill_rect(wx + 12, wy +  2, 4, 14, 0xFFFFFFu);
}

// Dock icons (left side vertical like ChromeOS shelf)
static void draw_icons_column(int selected_icon) {
    uint32_t icon_w = g_width / 16;
    if (icon_w < 40) icon_w = 40;
    uint32_t icon_h = icon_w;
    uint32_t gap    = icon_h / 4;

    uint32_t x = g_width / 40;
    uint32_t y = g_height / 7;

    for (int i = 0; i < 5; ++i) {
        uint32_t col = (i == selected_icon) ? 0xFFFFFFu : 0xAAAAAAu;
        fill_rect(x, y, icon_w, icon_h, 0x252C32u);
        draw_rect_border(x, y, icon_w, icon_h, col);

        uint32_t ix = x + icon_w / 6;
        uint32_t iy = y + icon_h / 6;
        uint32_t iw = icon_w * 2 / 3;
        uint32_t ih = icon_h * 2 / 3;

        switch (i) {
            case 0: // Settings (gear-ish)
                fill_rect(ix, iy, iw, ih, 0x404850u);
                draw_rect_border(ix, iy, iw, ih, 0xCCCCCCu);
                draw_text(ix + 4, iy + ih/2 - 4, "S", 0xFFFFFFu, 1);
                break;
            case 1: // File Block
                fill_rect(ix, iy, iw, ih, 0xF0F0F0u);
                draw_rect_border(ix, iy, iw, ih, 0xC0C0C0u);
                draw_text(ix + 4, iy + 4, "Fs", 0x000000u, 1);
                break;
            case 2: // Command Block
                fill_rect(ix, iy, iw, ih, 0x000000u);
                draw_text(ix + 4, iy + ih/2 - 4, "C_", 0x00FF00u, 1);
                break;
            case 3: // Browser
                fill_rect(ix, iy, iw, ih, 0xE0F2FFu);
                draw_rect_border(ix, iy, iw, ih, 0x66AAFFu);
                draw_text(ix + 4, iy + ih/2 - 4, "Web", 0x004080u, 1);
                break;
            case 4: // Extra
                fill_rect(ix, iy, iw, ih, 0xFFFFFFu);
                draw_rect_border(ix, iy, iw, ih, 0xC0C0C0u);
                draw_text(ix + 4, iy + ih/2 - 4, "App", 0x000000u, 1);
                break;
        }

        y += icon_h + gap;
    }
}

// ---------------- Start menu ----------------

static void draw_start_menu(void) {
    if (!g_start_open) return;
    uint32_t bar_h = g_height / 12;
    if (bar_h < 40) bar_h = 40;
    uint32_t h = g_height / 2;
    uint32_t w = g_width / 3;
    uint32_t x = 8;
    uint32_t y = g_height - bar_h - h - 8;

    fill_rect(x, y, w, h, 0x252C32u);
    draw_rect_border(x, y, w, h, 0xFFFFFFu);

    draw_text(x + 8, y + 8, "Start", 0xFFFFFFu, 1);
    draw_text(x + 8, y + 24, "Apps:", 0xC0C0C0u, 1);

    uint32_t ax = x + 16;
    uint32_t ay = y + 40;
    draw_text(ax, ay,   "Settings", 0xFFFFFFu, 1);
    draw_text(ax, ay+16,"File Block",0xFFFFFFu,1);
    draw_text(ax, ay+32,"Command Block",0xFFFFFFu,1);
    draw_text(ax, ay+48,"Browser",0xFFFFFFu,1);
}

// ---------------- Command Block drawing ----------------

static void draw_terminal_contents(uint32_t win_x, uint32_t win_y,
                                   uint32_t win_w, uint32_t win_h,
                                   uint32_t title_h) {
    uint32_t x = win_x + 10;
    uint32_t y = win_y + title_h + 10;

    // background
    fill_rect(win_x, win_y + title_h,
              win_w, win_h - title_h, 0x000000u);

    for (uint32_t i = 0; i < g_term.line_count; ++i) {
        draw_text(x, y, g_term.lines[i], 0xFFFFFFu, 1);
        y += 12;
        if (y + 16 >= win_y + win_h) break;
    }

    if (y + 16 < win_y + win_h) {
        char prompt[TERM_MAX_COLS];
        term_print_prompt_path(prompt, sizeof(prompt));

        char buf[TERM_MAX_COLS];
        str_copy(buf, prompt, sizeof(buf));
        str_cat(buf, " ", sizeof(buf));
        uint32_t base_len = str_len(buf);

        uint32_t len = g_term.input_len;
        if (base_len + len + 2 >= TERM_MAX_COLS) {
            len = TERM_MAX_COLS - base_len - 2;
        }
        for (uint32_t i = 0; i < len; ++i) {
            buf[base_len + i] = g_term.input[i];
        }
        // underline cursor
        buf[base_len + len] = '_';
        buf[base_len + len + 1] = '\0';

        draw_text(x, y + 4, buf, 0x00FF00u, 1);
    }
}

// ---------------- Settings drawing ----------------

static void draw_settings_contents(uint32_t win_x, uint32_t win_y,
                                   uint32_t win_w, uint32_t win_h,
                                   uint32_t title_h) {
    uint32_t x = win_x + 10;
    uint32_t y = win_y + title_h + 10;

    fill_rect(win_x, win_y + title_h,
              win_w, win_h - title_h, 0xF0F0F0u);

    char buf[64];
    draw_text(x, y, "Settings", 0x000000u, 2);
    y += 24;

    draw_text(x, y, "System", 0x202020u, 1);
    y += 16;
    // Resolution
    buf[0] = '\0';
    str_copy(buf, "Resolution: ", sizeof(buf));
    char tmp[16];

    // very simple decimal formatting
    uint32_t w = g_width;
    uint32_t h = g_height;
    // width
    int pos = 0;
    uint32_t div = 1000;
    int started = 0;
    while (div > 0 && pos < 4) {
        char digit = (char)('0' + (w / div) % 10);
        if (digit != '0' || started || div == 1) {
            tmp[pos++] = digit;
            started = 1;
        }
        div /= 10;
    }
    tmp[pos++] = 'x';
    // height
    div = 1000;
    started = 0;
    while (div > 0 && pos < 9) {
        char digit = (char)('0' + (h / div) % 10);
        if (digit != '0' || started || div == 1) {
            tmp[pos++] = digit;
            started = 1;
        }
        div /= 10;
    }
    tmp[pos] = '\0';

    str_cat(buf, tmp, sizeof(buf));
    draw_text(x + 4, y, buf, 0x000000u, 1);
    y += 14;

    // Time / date
    char tbuf[16], dbuf[16];
    format_time(tbuf, sizeof(tbuf));
    format_date(dbuf, sizeof(dbuf));
    buf[0] = '\0';
    str_copy(buf, "Boot time: ", sizeof(buf));
    str_cat(buf, dbuf, sizeof(buf));
    str_cat(buf, " ", sizeof(buf));
    str_cat(buf, tbuf, sizeof(buf));
    draw_text(x + 4, y, buf, 0x000000u, 1);
    y += 14;

    draw_text(x, y + 4, "LightOS 4 (demo kernel)", 0x000000u, 1);
}

// ---------------- VFS-driven File Block drawing ----------------

static void draw_fileblock_contents(uint32_t win_x, uint32_t win_y,
                                    uint32_t win_w, uint32_t win_h,
                                    uint32_t title_h) {
    uint32_t x = win_x + 8;
    uint32_t y = win_y + title_h + 8;

    fill_rect(win_x, win_y + title_h,
              win_w, win_h - title_h, 0xFFFFFFu);

    // path bar
    char path[64];
    vfs_build_path(path, sizeof(path), g_cwd);
    draw_text(x, y, path, 0x000000u, 1);
    y += 18;

    // column headers
    draw_text(x, y, "Name", 0x404040u, 1);
    y += 12;

    // entries
    for (int i = 0; i < g_vfs_count; ++i) {
        if (g_vfs[i].parent != g_cwd) continue;
        uint32_t row_y = y;
        // Icon
        uint32_t ix = x;
        uint32_t iy = row_y;
        if (g_vfs[i].type == VFS_DIR) {
            fill_rect(ix, iy, 10, 10, 0xFFE79Cu); // folder color
            draw_rect_border(ix, iy, 10, 10, 0xC08000u);
        } else {
            fill_rect(ix, iy, 10, 10, 0xE0E0FFu); // file color
            draw_rect_border(ix, iy, 10, 10, 0x8080C0u);
        }
        char line[TERM_MAX_COLS];
        str_copy(line, "  ", sizeof(line));
        str_cat(line, g_vfs[i].name, sizeof(line));
        draw_text(x + 14, row_y, line, 0x000000u, 1);
        y += 14;
        if (y + 14 >= win_y + win_h) break;
    }

    if (g_cwd == 0) {
        draw_text(x, win_y + win_h - 18,
                  "Use Command Block (dir/cd/mkdir/touch) to manage files.",
                  0x808080u, 1);
    }
}

// ---------------- Browser drawing ----------------

// Very simple "tab" structure
typedef struct {
    char title[32];
    char url[128];
    char content[512];
} BrowserTab;

static BrowserTab g_tabs[3];
static int        g_active_tab = 0;

// Stub network call. Replace with real HTTP later.
static int net_http_get(const char *url, char *buf, uint32_t max_len) {
    (void)url;
    const char *demo =
        "<html><body>"
        "<h1>LightOS Browser</h1>"
        "<p>No network stack yet. This is demo HTML.</p>"
        "<p>Implement NIC + TCP/IP and replace net_http_get().</p>"
        "</body></html>";
    str_copy(buf, demo, max_len);
    return 0; // success (demo)
}

static void browser_init(void) {
    str_copy(g_tabs[0].title, "Home", sizeof(g_tabs[0].title));
    str_copy(g_tabs[0].url,   "https://lightos.local/home", sizeof(g_tabs[0].url));
    str_copy(g_tabs[0].content,
             "Welcome to LightOS Browser.\n"
             "This is a static demo tab.\n",
             sizeof(g_tabs[0].content));

    str_copy(g_tabs[1].title, "Docs", sizeof(g_tabs[1].title));
    str_copy(g_tabs[1].url,   "https://lightos.local/docs", sizeof(g_tabs[1].url));
    str_copy(g_tabs[1].content,
             "Documentation is not available yet.\n",
             sizeof(g_tabs[1].content));

    str_copy(g_tabs[2].title, "Network", sizeof(g_tabs[2].title));
    str_copy(g_tabs[2].url,   "https://example.com/", sizeof(g_tabs[2].url));
    char buf[512];
    net_http_get(g_tabs[2].url, buf, sizeof(buf));
    str_copy(g_tabs[2].content, buf, sizeof(g_tabs[2].content));
}

static void draw_browser_contents(uint32_t win_x, uint32_t win_y,
                                  uint32_t win_w, uint32_t win_h,
                                  uint32_t title_h) {
    uint32_t x = win_x;
    uint32_t y = win_y + title_h;

    fill_rect(win_x, y, win_w, win_h - title_h, 0xF5F5F5u);

    // Tab bar
    uint32_t tab_h = 20;
    uint32_t tab_w = win_w / 3;
    for (int i = 0; i < 3; ++i) {
        uint32_t tx = x + i * tab_w;
        uint32_t col_bg = (i == g_active_tab) ? 0xFFFFFFu : 0xD0D0D0u;
        uint32_t col_bd = 0x808080u;
        fill_rect(tx, y, tab_w, tab_h, col_bg);
        draw_rect_border(tx, y, tab_w, tab_h, col_bd);
        draw_text(tx + 6, y + 4, g_tabs[i].title, 0x000000u, 1);
    }
    y += tab_h + 4;

    // Address bar
    uint32_t addr_h = 16;
    uint32_t addr_x = win_x + 10;
    uint32_t addr_w = win_w - 20;
    fill_rect(addr_x, y, addr_w, addr_h, 0xFFFFFFu);
    draw_rect_border(addr_x, y, addr_w, addr_h, 0xA0A0A0u);
    draw_text(addr_x + 4, y + 2, g_tabs[g_active_tab].url, 0x000000u, 1);
    y += addr_h + 6;

    // Content area (show text content)
    char buf[512];
    str_copy(buf, g_tabs[g_active_tab].content, sizeof(buf));

    uint32_t cx = win_x + 10;
    uint32_t cy = y;

    char *p = buf;
    while (*p && cy + 12 < win_y + win_h) {
        char *line = p;
        while (*p && *p != '\n') ++p;
        char saved = *p;
        *p = '\0';
        draw_text(cx, cy, line, 0x000000u, 1);
        if (saved == '\n') {
            *p = saved;
            ++p;
        }
        cy += 12;
    }

    draw_text(win_x + 10, win_y + win_h - 16,
              "NOTE: Real internet requires NIC + TCP/IP driver.",
              0x808080u, 1);
}

// ---------------- Window wrapper ----------------

static void draw_window(uint32_t win_x, uint32_t win_y,
                        uint32_t win_w, uint32_t win_h,
                        const char *title, int open_app) {
    uint32_t title_h = 24;

    // clear interior
    fill_rect(win_x, win_y, win_w, win_h, 0x202020u);
    draw_rect_border(win_x, win_y, win_w, win_h, 0x000000u);

    // title bar
    fill_rect(win_x, win_y, win_w, title_h, 0x303840u);
    draw_text(win_x + 8, win_y + 6, title, 0xFFFFFFu, 1);

    // Close button (red square, top-right)
    uint32_t bx = win_x + win_w - 20;
    uint32_t by = win_y + 4;
    fill_rect(bx, by, 14, 14, 0xAA0000u);

    // App content
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
        default:
            fill_rect(win_x, win_y + title_h,
                      win_w, win_h - title_h, 0x404040u);
            draw_text(win_x + 10, win_y + title_h + 10,
                      "Extra app placeholder.", 0xFFFFFFu, 1);
            break;
    }
}

static void draw_desktop(int selected_icon, int open_app) {
    draw_desktop_background();
    draw_taskbar();
    draw_icons_column(selected_icon);
    draw_start_menu();

    if (open_app >= 0) {
        uint32_t win_w = g_width * 3 / 5;
        uint32_t win_h = g_height * 3 / 5;
        uint32_t win_x = (g_width  - win_w) / 2;
        uint32_t win_y = (g_height - win_h) / 2 - g_height / 20;
        if (win_y < 10) win_y = 10;

        const char *title = "App";
        switch (open_app) {
            case 0: title = "Settings";       break;
            case 1: title = "File Block";     break;
            case 2: title = "Command Block";  break;
            case 3: title = "Browser";        break;
            case 4: title = "Extra";          break;
        }

        draw_window(win_x, win_y, win_w, win_h, title, open_app);
    }

    // Draw mouse cursor last (on top)
    draw_mouse_cursor();
}

// ---------------------------------------------------------------------
// Keyboard nav (desktop side) – ARROW KEYS now move the cursor
// ---------------------------------------------------------------------

static void handle_nav_scancode(uint8_t sc,
                                int *selected_icon,
                                int *open_app) {
    if (sc & 0x80) return; // ignore releases (Shift handled in terminal)

    int32_t dx = 0;
    int32_t dy = 0;

    // Arrow keys: change selection + move cursor
    if (sc == 0x48) {            // Up arrow
        if (*selected_icon > 0) (*selected_icon)--;
        dy = -8;
    } else if (sc == 0x50) {     // Down arrow
        if (*selected_icon < 4) (*selected_icon)++;
        dy = 8;
    } else if (sc == 0x4B) {     // Left arrow
        dx = -8;
    } else if (sc == 0x4D) {     // Right arrow
        dx = 8;
    } else if (sc == 0x1F) {     // 's' -> toggle Start
        g_start_open = !g_start_open;
        return;
    } else if (sc == 0x1C) {     // Enter: open selected app
        if (*selected_icon >= 0 && *selected_icon <= 4) {
            *open_app = *selected_icon;
            if (*open_app == 2) term_reset(&g_term);
        }
        return;
    } else if (sc == 0x01) {     // Esc: close app
        *open_app = -1;
        return;
    }

    if (dx != 0 || dy != 0) {
        g_mouse.x += dx;
        g_mouse.y += dy;
        if (g_mouse.x < 0) g_mouse.x = 0;
        if (g_mouse.y < 0) g_mouse.y = 0;
        if ((uint32_t)g_mouse.x >= g_width)  g_mouse.x = (int32_t)g_width - 1;
        if ((uint32_t)g_mouse.y >= g_height) g_mouse.y = (int32_t)g_height - 1;
    }
}

// ---------------------------------------------------------------------
// Kernel entry
// ---------------------------------------------------------------------

void kernel_main(BootInfo *bi) {
    g_fb     = (uint32_t*)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch ? bi->framebuffer_pitch : bi->framebuffer_width;

    g_year   = bi->year;
    g_month  = bi->month;
    g_day    = bi->day;
    g_hour   = bi->hour;
    g_minute = bi->minute;
    g_second = bi->second;

    vfs_init();
    browser_init();

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
                open_app == 2 ||
                g_start_open) {
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
