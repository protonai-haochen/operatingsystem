// LightOS 4 kernel
// Simple UEFI framebuffer desktop + Command Block shell.
// Uses BootInfo from boot.h and PS/2 keyboard polling.

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
// Low-level I/O / keyboard
// ---------------------------------------------------------------------

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

// Simple polling PS/2 keyboard
static int keyboard_poll(uint8_t *scancode) {
    if (inb(0x64) & 0x01) {
        *scancode = inb(0x60);
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------
// Basic drawing helpers
// ---------------------------------------------------------------------

static inline void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= g_width || y >= g_height) return;
    uint32_t *row = (uint32_t*)((uint8_t*)g_fb + y * g_pitch * 4);
    row[x] = color;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t yy = 0; yy < h; ++yy) {
        if (y + yy >= g_height) break;
        uint32_t *row = (uint32_t*)((uint8_t*)g_fb + (y + yy) * g_pitch * 4);
        for (uint32_t xx = 0; xx < w && x + xx < g_width; ++xx) {
            row[x + xx] = color;
        }
    }
}

// 8x8 bitmap font (ASCII 32..127) – same as before (not shown fully here
// in this comment; keep your existing font table).
// ---------------------------------------------------------------------
//  ... (keep your existing font bitmap data / font table here)
// ---------------------------------------------------------------------

extern const uint8_t g_font[96][8];  // from your existing font.c / data

static void draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg, uint32_t scale) {
    if (c < 32 || c > 127) return;
    const uint8_t *glyph = g_font[(uint8_t)c - 32];

    for (uint32_t cy = 0; cy < 8; ++cy) {
        uint8_t row = glyph[cy];
        for (uint32_t cx = 0; cx < 8; ++cx) {
            uint32_t color = (row & (1u << (7 - cx))) ? fg : bg;
            if (color == 0xFFFFFFFF) continue; // treat white as transparent bg
            for (uint32_t sy = 0; sy < scale; ++sy) {
                for (uint32_t sx = 0; sx < scale; ++sx) {
                    put_pixel(x + cx * scale + sx, y + cy * scale + sy, color);
                }
            }
        }
    }
}

static void draw_text(uint32_t x, uint32_t y, const char *s, uint32_t fg, uint32_t scale) {
    uint32_t cursor_x = x;
    while (*s) {
        if (*s == '\n') {
            y += 8 * scale + 2;
            cursor_x = x;
            ++s;
            continue;
        }
        draw_char(cursor_x, y, *s, fg, 0xFFFFFFFF, scale);
        cursor_x += 8 * scale;
        ++s;
    }
}

// ---------------------------------------------------------------------
// Simple string helpers
// ---------------------------------------------------------------------

static uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) {
        ++a; ++b;
    }
    return (*a == 0 && *b == 0);
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

// ---------------------------------------------------------------------
// Very small virtual filesystem for File Block + Command Block
// ---------------------------------------------------------------------

typedef enum {
    VNODE_DIR,
    VNODE_FILE
} VNodeType;

typedef struct VNode VNode;
struct VNode {
    const char *name;
    VNodeType   type;
    const char *ext;       // file extension for files (e.g. "txt", "mp3"), NULL for dirs
    const char *content;   // text content for .txt files, NULL for binary/other
    const VNode *parent;   // parent directory (NULL for root)
    const VNode *children; // child array for directories
    uint32_t     child_count;
};

static const VNode fs_root;
static const VNode fs_home;
static const VNode fs_system;

static const VNode fs_root_children[];
static const VNode fs_home_children[];
static const VNode fs_system_children[];

// Directories
static const VNode fs_root = {
    "", VNODE_DIR, 0, 0, 0,
    fs_root_children, 3
};

static const VNode fs_home = {
    "home", VNODE_DIR, 0, 0, &fs_root,
    fs_home_children, 3
};

static const VNode fs_system = {
    "system", VNODE_DIR, 0, 0, &fs_root,
    fs_system_children, 2
};

// Root children
static const VNode fs_root_children[] = {
    { "home",   VNODE_DIR, 0, 0, &fs_root, fs_home_children,   3 },
    { "system", VNODE_DIR, 0, 0, &fs_root, fs_system_children, 2 },
    { "readme.txt", VNODE_FILE, "txt",
      "Welcome to LightOS 4!\\n"
      "This is a demo virtual filesystem.\\n"
      "Use 'dir' or 'ls' to list entries.\\n",
      &fs_root, 0, 0
    }
};

// /home children
static const VNode fs_home_children[] = {
    { "notes.txt", VNODE_FILE, "txt",
      "This is notes.txt in C:/home.\\n"
      "Sample commands: help, dir/ls, cd, cat/type, echo, start.\\n",
      &fs_home, 0, 0
    },
    { "song.mp3", VNODE_FILE, "mp3",
      0,
      &fs_home, 0, 0
    },
    { "video.mp4", VNODE_FILE, "mp4",
      0,
      &fs_home, 0, 0
    }
};

// /system children
static const VNode fs_system_children[] = {
    { "cmd.exe",       VNODE_FILE, "exe", 0, &fs_system, 0, 0 },
    { "lightweb.exe",  VNODE_FILE, "exe", 0, &fs_system, 0, 0 }
};

static const VNode *g_fs_root    = &fs_root;
static const VNode *g_cwd        = &fs_root;  // current working directory for Command Block
static const VNode *g_file_dir   = &fs_root;  // directory currently shown in File Block
static uint32_t     g_file_index = 0;         // selected entry in File Block

// Case-insensitive compare for ASCII letters, up to n characters
static int str_ncasecmp_simple(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
        char ca = a[i];
        char cb = b[i];
        if (!ca || !cb) return ca == cb;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
    }
    return 1;
}

// Match a child name against a query, allowing omission of the extension.
// For example, "notes" matches "notes.txt".
static int fs_name_matches(const VNode *node, const char *query) {
    if (!node || !query) return 0;

    // Exact match
    if (str_eq(node->name, query)) return 1;

    // Compare without extension, case-insensitive
    uint32_t base_len = 0;
    while (node->name[base_len] && node->name[base_len] != '.') {
        base_len++;
    }
    uint32_t qlen = str_len(query);
    if (qlen != base_len) return 0;
    return str_ncasecmp_simple(node->name, query, base_len);
}

static const VNode *fs_find_child(const VNode *dir, const char *name) {
    if (!dir || dir->type != VNODE_DIR || !name) return 0;
    for (uint32_t i = 0; i < dir->child_count; ++i) {
        const VNode *child = &dir->children[i];
        if (fs_name_matches(child, name)) return child;
    }
    return 0;
}

// Build a simple C: style path like "C:/home/"
static void fs_build_path(const VNode *node, char *buf, uint32_t buf_size) {
    if (!buf || buf_size == 0) return;
    buf[0] = '\0';
    if (!node) return;

    // Collect ancestors
    const VNode *stack[8];
    uint32_t depth = 0;
    const VNode *cur = node;
    while (cur && depth < 8) {
        stack[depth++] = cur;
        cur = cur->parent;
    }

    uint32_t pos = 0;
    const char *prefix = "C:/";
    for (uint32_t i = 0; prefix[i] && pos < buf_size - 1; ++i) {
        buf[pos++] = prefix[i];
    }

    // Walk from root child downwards, skipping root itself (which has empty name)
    for (int i = (int)depth - 2; i >= 0 && pos < (int)buf_size - 1; --i) {
        const char *name = stack[i]->name;
        if (name && name[0]) {
            for (uint32_t j = 0; name[j] && pos < buf_size - 1; ++j) {
                buf[pos++] = name[j];
            }
            if (stack[i]->type == VNODE_DIR && pos < buf_size - 1) {
                buf[pos++] = '/';
            }
        }
    }

    buf[pos] = '\0';
}

// ---------------------------------------------------------------------
// Command Block terminal state
// ---------------------------------------------------------------------

#define TERM_MAX_LINES 40
#define TERM_MAX_COLS  80

typedef struct {
    char     lines[TERM_MAX_LINES][TERM_MAX_COLS];
    uint32_t line_count;
    char     input[TERM_MAX_COLS];
    uint32_t input_len;
} TerminalState;

static TerminalState g_term;

// Simple circular scroll
static void term_add_line(TerminalState *t, const char *text) {
    if (!t || !text) return;

    if (t->line_count < TERM_MAX_LINES) {
        uint32_t idx = t->line_count++;
        uint32_t i   = 0;
        while (text[i] && i < TERM_MAX_COLS - 1) {
            t->lines[idx][i] = text[i];
            ++i;
        }
        t->lines[idx][i] = '\0';
    } else {
        // scroll up
        for (uint32_t i = 1; i < TERM_MAX_LINES; ++i) {
            for (uint32_t j = 0; j < TERM_MAX_COLS; ++j) {
                t->lines[i - 1][j] = t->lines[i][j];
            }
        }
        // last line
        uint32_t i = 0;
        while (text[i] && i < TERM_MAX_COLS - 1) {
            t->lines[TERM_MAX_LINES - 1][i] = text[i];
            ++i;
        }
        t->lines[TERM_MAX_LINES - 1][i] = '\0';
    }
}

static void term_clear(TerminalState *t) {
    t->line_count = 0;
    for (uint32_t i = 0; i < TERM_MAX_LINES; ++i) {
        t->lines[i][0] = '\0';
    }
    t->input_len = 0;
    t->input[0]  = '\0';
}

static void term_init(TerminalState *t) {
    term_clear(t);
    term_add_line(t, "LightOS 4 Command Block");
    term_add_line(t, "Type 'help' for commands.");
    char path[128];
    fs_build_path(g_cwd, path, sizeof(path));
    term_add_line(t, path);
}

static char scancode_to_char(uint8_t sc) {
    switch (sc) {
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
        case 0x1A: return '[';
        case 0x1B: return ']';
        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x27: return ';';
        case 0x28: return '\'';
        case 0x29: return '`';
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

static void term_execute_command(TerminalState *t, const char *cmd_raw, int *open_app) {
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

    // Helper to skip the first word and spaces
    const char *arg = cmd;
    while (*arg && *arg != ' ') ++arg;
    while (*arg == ' ') ++arg;

    // HELP
    if (str_eq(cmd, "help")) {
        term_add_line(t, "Supported commands (Windows + Linux style):");
        term_add_line(t, "  help");
        term_add_line(t, "  cls, clear");
        term_add_line(t, "  dir, ls");
        term_add_line(t, "  cd <dir>, cd .., cd /");
        term_add_line(t, "  pwd");
        term_add_line(t, "  type <file>, cat <file>");
        term_add_line(t, "  time, date");
        term_add_line(t, "  ver, uname");
        term_add_line(t, "  echo <text>");
        term_add_line(t, "  start <app|file>, open <file>");
        term_add_line(t, "Built-in dirs: home, system");
        return;
    }

    // CLS / CLEAR
    if (str_eq(cmd, "cls") || str_eq(cmd, "clear")) {
        term_clear(t);
        term_add_line(t, "LightOS 4 Command Block");
        term_add_line(t, "Type 'help' for commands.");
        char path[128];
        fs_build_path(g_cwd, path, sizeof(path));
        term_add_line(t, path);
        return;
    }

    // DIR / LS
    if (str_eq(cmd, "dir") || str_eq(cmd, "ls")) {
        char path[128];
        fs_build_path(g_cwd, path, sizeof(path));
        term_add_line(t, path);
        if (!g_cwd->child_count) {
            term_add_line(t, "  [empty]");
            return;
        }
        for (uint32_t i = 0; i < g_cwd->child_count; ++i) {
            const VNode *ch = &g_cwd->children[i];
            char line[TERM_MAX_COLS];
            if (ch->type == VNODE_DIR) {
                // [DIR] name/
                uint32_t p = 0;
                const char *prefix = "[DIR] ";
                for (uint32_t j = 0; prefix[j] && p < TERM_MAX_COLS - 1; ++j) {
                    line[p++] = prefix[j];
                }
                for (uint32_t j = 0; ch->name[j] && p < TERM_MAX_COLS - 1; ++j) {
                    line[p++] = ch->name[j];
                }
                if (p < TERM_MAX_COLS - 1) line[p++] = '/';
                line[p] = '\0';
                term_add_line(t, line);
            } else {
                // file
                uint32_t p = 0;
                for (uint32_t j = 0; ch->name[j] && p < TERM_MAX_COLS - 1; ++j) {
                    line[p++] = ch->name[j];
                }
                line[p] = '\0';
                term_add_line(t, line);
            }
        }
        return;
    }

    // CD
    if (str_starts_with(cmd, "cd")) {
        if (*arg == '\0') {
            // print current directory
            char path[128];
            fs_build_path(g_cwd, path, sizeof(path));
            term_add_line(t, path);
            return;
        }

        if (str_eq(arg, "/") || str_eq(arg, "C:/")) {
            g_cwd        = g_fs_root;
            g_file_dir   = g_fs_root;
            g_file_index = 0;
        } else if (str_eq(arg, "..")) {
            if (g_cwd->parent) {
                g_cwd        = g_cwd->parent;
                g_file_dir   = g_cwd;
                g_file_index = 0;
            }
        } else {
            const VNode *child = fs_find_child(g_cwd, arg);
            if (!child) {
                term_add_line(t, "The system cannot find the path specified.");
                return;
            }
            if (child->type != VNODE_DIR) {
                term_add_line(t, "Not a directory.");
                return;
            }
            g_cwd        = child;
            g_file_dir   = child;
            g_file_index = 0;
        }

        char path[128];
        fs_build_path(g_cwd, path, sizeof(path));
        term_add_line(t, path);
        return;
    }

    // PWD (Linux-style current directory)
    if (str_eq(cmd, "pwd")) {
        char path[128];
        fs_build_path(g_cwd, path, sizeof(path));
        term_add_line(t, path);
        return;
    }

    // TYPE / CAT (show text file)
    if (str_starts_with(cmd, "type") || str_starts_with(cmd, "cat")) {
        if (*arg == '\0') {
            term_add_line(t, "Usage: type <file>  or  cat <file>");
            return;
        }
        const VNode *file = fs_find_child(g_cwd, arg);
        if (!file || file->type != VNODE_FILE) {
            term_add_line(t, "File not found.");
            return;
        }
        if (!file->content) {
            term_add_line(t, "[binary file]");
            return;
        }

        // Print content line by line (split on \n)
        const char *p = file->content;
        char line[TERM_MAX_COLS];
        uint32_t lp = 0;
        while (*p) {
            if (*p == '\n' || lp == TERM_MAX_COLS - 1) {
                line[lp] = '\0';
                term_add_line(t, line);
                lp = 0;
                if (*p == '\n') { ++p; continue; }
            } else {
                line[lp++] = *p++;
            }
        }
        if (lp > 0) {
            line[lp] = '\0';
            term_add_line(t, line);
        }
        return;
    }

    // VER / UNAME
    if (str_eq(cmd, "ver") || str_eq(cmd, "uname")) {
        term_add_line(t, "LightOS 4.0 demo kernel");
        return;
    }

    // TIME / DATE (still stubbed)
    if (str_eq(cmd, "time") || str_eq(cmd, "date")) {
        term_add_line(t, "[Time/date not implemented yet]");
        return;
    }

    // ECHO
    if (str_starts_with(cmd, "echo")) {
        const char *p = arg;
        if (*p == '\0') term_add_line(t, "");
        else            term_add_line(t, p);
        return;
    }

    // START / OPEN (basic launcher)
    if (str_starts_with(cmd, "start") || str_starts_with(cmd, "open")) {
        if (*arg == '\0') {
            term_add_line(t, "Usage: start <app|file>  or  open <file>");
            return;
        }

        // App aliases
        if (str_eq(arg, "cmd") || str_eq(arg, "command") || str_eq(arg, "terminal")) {
            if (open_app) *open_app = 2;   // Command Block
            return;
        }
        if (str_eq(arg, "files") || str_eq(arg, "fileblock") || str_eq(arg, "explorer")) {
            if (open_app) *open_app = 1;   // File Block
            g_file_dir   = g_cwd;
            g_file_index = 0;
            return;
        }
        if (str_eq(arg, "browser") || str_eq(arg, "lightweb")) {
            if (open_app) *open_app = 3;   // Browser
            return;
        }

        // Try to open as file in current directory
        const VNode *file = fs_find_child(g_cwd, arg);
        if (!file) {
            term_add_line(t, "File not found.");
            return;
        }

        if (file->type == VNODE_DIR) {
            g_cwd        = file;
            g_file_dir   = file;
            g_file_index = 0;
            char path[128];
            fs_build_path(g_cwd, path, sizeof(path));
            term_add_line(t, path);
            return;
        }

        if (file->ext && str_eq(file->ext, "txt") && file->content) {
            term_add_line(t, "[Opening text file in Command Block]");
            const char *p = file->content;
            char line[TERM_MAX_COLS];
            uint32_t lp = 0;
            while (*p) {
                if (*p == '\n' || lp == TERM_MAX_COLS - 1) {
                    line[lp] = '\0';
                    term_add_line(t, line);
                    lp = 0;
                    if (*p == '\n') { ++p; continue; }
                } else {
                    line[lp++] = *p++;
                }
            }
            if (lp > 0) {
                line[lp] = '\0';
                term_add_line(t, line);
            }
            return;
        }

        if (file->ext && (str_eq(file->ext, "mp3") || str_eq(file->ext, "wav"))) {
            term_add_line(t, "[Pretending to play audio file]");
            return;
        }

        if (file->ext && (str_eq(file->ext, "mp4") || str_eq(file->ext, "avi"))) {
            term_add_line(t, "[Pretending to play video file]");
            return;
        }

        if (file->ext && str_eq(file->ext, "exe")) {
            term_add_line(t, "[Cannot execute real EXE yet; stub only]");
            return;
        }

        term_add_line(t, "[Unknown file type]");
        return;
    }

    term_add_line(t, "Unknown command. Type 'help'.");
}

static void term_handle_scancode(uint8_t sc, TerminalState *t,
                                 int *selected_icon, int *open_app) {
    (void)selected_icon; // not used while inside Command Block

    // Ignore releases; we only care about presses
    if (sc == 0xE0) return;      // ignore extended prefix for now
    if (sc & 0x80) return;

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
        term_execute_command(t, t->input, open_app);
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

static void draw_wifi_icon(uint32_t x, uint32_t y) {
    draw_text(x, y, ")))", 0xFFFFFF, 1);
    draw_text(x, y + 8, " | ", 0xFFFFFF, 1);
}

static void draw_battery_icon(uint32_t x, uint32_t y) {
    fill_rect(x,     y,     22, 10, 0x00FF00);
    fill_rect(x + 22, y + 2, 2,  6, 0xFFFFFF);
}

static void draw_clock(uint32_t x, uint32_t y) {
    draw_text(x, y, "12:34", 0xFFFFFF, 1);
    draw_text(x, y + 10, "2026-01-19", 0xFFFFFF, 1);
}

static void draw_settings_contents(uint32_t win_x, uint32_t win_y,
                                   uint32_t win_w, uint32_t win_h,
                                   uint32_t title_h) {
    (void)win_w; (void)win_h;
    uint32_t y = win_y + title_h + 16;
    draw_text(win_x + 12, y, "LightOS Settings (demo)", 0x000000, 2);
    y += 20;
    draw_text(win_x + 12, y, "- Theme: Classic Blue", 0x000000, 1);
    y += 14;
    draw_text(win_x + 12, y, "- User accounts: not implemented yet", 0x000000, 1);
}

static void draw_browser_contents(uint32_t win_x, uint32_t win_y,
                                  uint32_t win_w, uint32_t win_h,
                                  uint32_t title_h) {
    (void)win_w; (void)win_h;
    uint32_t y = win_y + title_h + 16;
    draw_text(win_x + 12, y, "LightWeb (offline demo browser)", 0x000000, 2);
    y += 20;
    draw_text(win_x + 12, y, "There is no real network stack yet,", 0x000000, 1);
    y += 14;
    draw_text(win_x + 12, y, "so this browser shows only built-in text.", 0x000000, 1);
}

static void draw_file_block(uint32_t win_x, uint32_t win_y,
                            uint32_t win_w, uint32_t win_h,
                            uint32_t title_h) {
    uint32_t y = win_y + title_h + 8;

    char path[128];
    fs_build_path(g_file_dir, path, sizeof(path));
    draw_text(win_x + 12, y, path, 0x000000, 1);
    y += 18;

    if (!g_file_dir->child_count) {
        draw_text(win_x + 12, y, "[empty]", 0x000000, 1);
        return;
    }

    uint32_t line_h = 14;
    for (uint32_t i = 0; i < g_file_dir->child_count; ++i) {
        const VNode *ch = &g_file_dir->children[i];
        uint32_t row_y = y + i * (line_h + 2);

        uint32_t bg = (i == g_file_index) ? 0x303030 : 0x000000;
        uint32_t text_col = 0xFFFFFF;

        // highlight rectangle
        fill_rect(win_x + 8, row_y - 2, win_w - 16, line_h + 4, bg);

        char line[TERM_MAX_COLS];
        uint32_t p = 0;
        if (ch->type == VNODE_DIR) {
            const char *prefix = "[DIR] ";
            for (uint32_t j = 0; prefix[j] && p < TERM_MAX_COLS - 1; ++j) {
                line[p++] = prefix[j];
            }
        }
        for (uint32_t j = 0; ch->name[j] && p < TERM_MAX_COLS - 1; ++j) {
            line[p++] = ch->name[j];
        }
        line[p] = '\0';

        draw_text(win_x + 12, row_y, line, text_col, 1);
    }

    draw_text(win_x + 8, win_y + win_h - 20,
              "Enter: open   Backspace: up   ESC: close",
              0x000000, 1);
}

static void draw_main_window(int open_app) {
    if (open_app < 0) return;

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
        case 4: body_col = 0xF4E0FF; title_col = 0x664488; break; // App Store (unused)
        default: break;
    }

    // Border
    fill_rect(win_x - 1, win_y - 1, win_w + 2, win_h + 2, border_col);

    // Title bar
    uint32_t title_h = 28;
    fill_rect(win_x, win_y, win_w, title_h, title_col);

    // Window body
    fill_rect(win_x, win_y + title_h, win_w, win_h - title_h, body_col);

    // Close button (visual only)
    uint32_t btn   = (title_h > 8) ? (title_h - 8) : (title_h / 2);
    uint32_t btn_x = win_x + win_w - btn - 4;
    uint32_t btn_y = win_y + (title_h - btn) / 2;
    fill_rect(btn_x, btn_y, btn, btn, 0x800000);

    switch (open_app) {
        case 0:
            draw_settings_contents(win_x, win_y, win_w, win_h, title_h);
            break;
        case 1:
            draw_file_block(win_x, win_y, win_w, win_h, title_h);
            break;
        case 2:
            draw_terminal_contents(win_x, win_y, win_w, win_h, title_h);
            break;
        case 3:
            draw_browser_contents(win_x, win_y, win_w, win_h, title_h);
            break;
        default:
            break;
    }
}

// ... (keep your existing draw_terminal_contents, icon drawing, desktop
// drawing, and boot splash functions here – unchanged, except they now
// call draw_main_window(open_app) which will display the right app.)

// ---------------------------------------------------------------------
// File Block keyboard handler
// ---------------------------------------------------------------------

static void file_block_handle_scancode(uint8_t sc,
                                       int *selected_icon,
                                       int *open_app) {
    (void)selected_icon;

    static uint8_t ext = 0;

    if (sc == 0xE0) {
        ext = 1;
        return;
    }

    if (ext) {
        uint8_t code = sc & 0x7F;
        if (!(sc & 0x80)) {
            switch (code) {
                case 0x48: // Up arrow
                    if (g_file_dir->child_count) {
                        if (g_file_index == 0) g_file_index = g_file_dir->child_count - 1;
                        else                   g_file_index--;
                    }
                    break;
                case 0x50: // Down arrow
                    if (g_file_dir->child_count) {
                        g_file_index++;
                        if (g_file_index >= g_file_dir->child_count) g_file_index = 0;
                    }
                    break;
                default:
                    break;
            }
        }
        ext = 0;
        return;
    }

    if (sc & 0x80) return; // ignore releases

    if (sc == 0x01) {      // Esc
        *open_app = -1;
        return;
    }

    if (sc == 0x0E) {      // Backspace: go up one directory
        if (g_file_dir->parent) {
            g_file_dir   = g_file_dir->parent;
            g_cwd        = g_file_dir;
            g_file_index = 0;
        }
        return;
    }

    if (sc == 0x1C) {      // Enter: open selected entry
        if (!g_file_dir->child_count) return;
        if (g_file_index >= g_file_dir->child_count) {
            g_file_index = g_file_dir->child_count - 1;
        }
        const VNode *node = &g_file_dir->children[g_file_index];

        if (node->type == VNODE_DIR) {
            g_file_dir   = node;
            g_cwd        = node;
            g_file_index = 0;
            return;
        }

        // For files, jump into Command Block and display something
        *open_app = 2;
        term_clear(&g_term);
        term_add_line(&g_term, "LightOS 4 Command Block");
        term_add_line(&g_term, "Opened from File Block:");

        char line[TERM_MAX_COLS];
        uint32_t lp = 0;

        if (node->ext && str_eq(node->ext, "txt") && node->content) {
            const char *p = node->content;
            while (*p) {
                if (*p == '\n' || lp == TERM_MAX_COLS - 1) {
                    line[lp] = '\0';
                    term_add_line(&g_term, line);
                    lp = 0;
                    if (*p == '\n') { ++p; continue; }
                } else {
                    line[lp++] = *p++;
                }
            }
            if (lp > 0) {
                line[lp] = '\0';
                term_add_line(&g_term, line);
            }
        } else if (node->ext && (str_eq(node->ext, "mp3") || str_eq(node->ext, "wav"))) {
            term_add_line(&g_term, "[Pretending to play audio file]");
        } else if (node->ext && (str_eq(node->ext, "mp4") || str_eq(node->ext, "avi"))) {
            term_add_line(&g_term, "[Pretending to play video file]");
        } else if (node->ext && str_eq(node->ext, "exe")) {
            term_add_line(&g_term, "[Executable stub; real apps not implemented]");
        } else {
            term_add_line(&g_term, "[Unknown file type]");
        }

        return;
    }
}

// ---------------------------------------------------------------------
// Desktop navigation handler (existing code) – only change is that
// kernel_main now calls file_block_handle_scancode / term_handle_scancode
// depending on open_app.
// ---------------------------------------------------------------------

// ... keep your handle_nav_scancode, draw_desktop, run_boot_splash etc.

// ---------------------------------------------------------------------
// Kernel entry
// ---------------------------------------------------------------------

void kernel_main(BootInfo *bi) {
    g_fb     = (uint32_t*)(uintptr_t)bi->framebuffer_base;
    g_width  = bi->framebuffer_width;
    g_height = bi->framebuffer_height;
    g_pitch  = bi->framebuffer_pitch;

    // 1) Boot splash
    run_boot_splash();

    // 2) Desktop + Command Block
    term_init(&g_term);

    int selected_icon = 2;   // highlight Command Block by default
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
            } else if (open_app == 1) {
                // File Block navigation
                file_block_handle_scancode(sc, &selected_icon, &open_app);
            } else {
                // Desktop navigation (select icons, open apps)
                handle_nav_scancode(sc, &selected_icon, &open_app);
            }

            // Redraw when something changed, or while typing in Command/File Block
            if (selected_icon != prev_sel ||
                open_app      != prev_open ||
                open_app == 2 ||
                open_app == 1) {
                draw_desktop(selected_icon, open_app);
            }
        }
    }
}

