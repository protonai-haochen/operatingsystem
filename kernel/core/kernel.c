#include <stdint.h>
#include "../include/boot.h"

/*
 * Super minimal test kernel:
 * - fills the screen with a blue-ish gradient
 * - draws a square window in the middle
 * - then hlt-loops forever (never returns to the bootloader)
 */

static inline void put_pixel(uint32_t* fb,
                             uint32_t pitch,
                             uint32_t x,
                             uint32_t y,
                             uint32_t color,
                             uint32_t width,
                             uint32_t height)
{
    if (x >= width || y >= height) return;
    fb[y * pitch + x] = color;
}

static void fill_rect(uint32_t* fb,
                      uint32_t pitch,
                      uint32_t x,
                      uint32_t y,
                      uint32_t w,
                      uint32_t h,
                      uint32_t color,
                      uint32_t screen_w,
                      uint32_t screen_h)
{
    if (x >= screen_w || y >= screen_h) return;

    uint32_t x_end = x + w;
    uint32_t y_end = y + h;
    if (x_end > screen_w) x_end = screen_w;
    if (y_end > screen_h) y_end = screen_h;

    for (uint32_t yy = y; yy < y_end; ++yy) {
        for (uint32_t xx = x; xx < x_end; ++xx) {
            fb[yy * pitch + xx] = color;
        }
    }
}

static void draw_desktop(uint32_t* fb,
                         uint32_t pitch,
                         uint32_t width,
                         uint32_t height)
{
    // Simple vertical blue gradient background
    for (uint32_t y = 0; y < height; ++y) {
        float t = (float)y / (float)(height ? height : 1);
        // interpolate between dark blue and light blue
        uint8_t r = (uint8_t)(10  * (1.0f - t) +  60 * t);
        uint8_t g = (uint8_t)(30  * (1.0f - t) + 160 * t);
        uint8_t b = (uint8_t)(70  * (1.0f - t) + 230 * t);

        uint32_t color = (r << 16) | (g << 8) | b;

        for (uint32_t x = 0; x < width; ++x) {
            fb[y * pitch + x] = color;
        }
    }

    // Bottom shelf / taskbar (ChromeOS-style)
    uint32_t shelf_height = height / 12; // ~8–10% of screen
    uint32_t shelf_y = height - shelf_height;
    uint32_t shelf_color = 0x00202020;   // dark grey

    fill_rect(fb, pitch,
              0, shelf_y,
              width, shelf_height,
              shelf_color,
              width, height);

    // Slight lighter strip at top of shelf for fake shadow
    uint32_t shelf_highlight = 0x00333333;
    fill_rect(fb, pitch,
              0, shelf_y,
              width, 2,
              shelf_highlight,
              width, height);
}

static void draw_window(uint32_t* fb,
                        uint32_t pitch,
                        uint32_t screen_w,
                        uint32_t screen_h,
                        uint32_t x,
                        uint32_t y,
                        uint32_t w,
                        uint32_t h)
{
    if (w < 40 || h < 40) return; // avoid silly tiny windows

    // Clamp so we don't draw off-screen
    if (x >= screen_w || y >= screen_h) return;
    if (x + w > screen_w) w = screen_w - x;
    if (y + h > screen_h) h = screen_h - y;

    // Colors (square window, no rounded anything)
    uint32_t border_color     = 0x00404040; // dark border
    uint32_t body_color       = 0x00F0F0F0; // light grey
    uint32_t titlebar_color   = 0x002A6FFF; // blue titlebar

    uint32_t titlebar_height  = 28;
    if (titlebar_height + 4 > h) titlebar_height = h / 4;

    // Outer border
    fill_rect(fb, pitch,
              x, y,
              w, h,
              border_color,
              screen_w, screen_h);

    // Inner body (client area)
    fill_rect(fb, pitch,
              x + 2, y + 2 + titlebar_height,
              w - 4, h - 4 - titlebar_height,
              body_color,
              screen_w, screen_h);

    // Title bar
    fill_rect(fb, pitch,
              x + 2, y + 2,
              w - 4, titlebar_height,
              titlebar_color,
              screen_w, screen_h);

    // Fake close button (square, top-right)
    uint32_t btn_size   = titlebar_height - 8;
    uint32_t btn_x      = x + w - 2 - btn_size - 4;
    uint32_t btn_y      = y + 2 + 4;
    uint32_t btn_bg     = 0x00DD4444; // red-ish
    uint32_t btn_border = 0x00AA0000;

    // Button background
    fill_rect(fb, pitch,
              btn_x, btn_y,
              btn_size, btn_size,
              btn_bg,
              screen_w, screen_h);

    // Button border
    fill_rect(fb, pitch, btn_x,              btn_y,               btn_size, 1,           btn_border, screen_w, screen_h); // top
    fill_rect(fb, pitch, btn_x,              btn_y + btn_size-1,  btn_size, 1,           btn_border, screen_w, screen_h); // bottom
    fill_rect(fb, pitch, btn_x,              btn_y,               1,        btn_size,    btn_border, screen_w, screen_h); // left
    fill_rect(fb, pitch, btn_x + btn_size-1, btn_y,               1,        btn_size,    btn_border, screen_w, screen_h); // right
}

void kernel_main(BootInfo* info)
{
    uint32_t* fb    = (uint32_t*)info->framebuffer_base;
    uint32_t width  = info->framebuffer_width;
    uint32_t height = info->framebuffer_height;
    uint32_t pitch  = info->framebuffer_pitch; // pixels per scanline

    // Safety: if something is weird with the framebuffer, just bail into a magenta screen
    if (!fb || !width || !height || !pitch) {
        // fallback solid magenta, just so we SEE something
        for (uint32_t y = 0; y < 480; ++y) {
            for (uint32_t x = 0; x < 640; ++x) {
                fb[y * pitch + x] = 0x00FF00FF;
            }
        }
        for (;;) { __asm__("hlt"); }
    }

    // Draw desktop + shelf
    draw_desktop(fb, pitch, width, height);

    // Center window
    uint32_t win_w = width  / 2;
    uint32_t win_h = height / 2;
    uint32_t win_x = (width  - win_w) / 2;
    uint32_t win_y = (height - win_h) / 3;

    draw_window(fb, pitch, width, height, win_x, win_y, win_w, win_h);

    // IMPORTANT: never return to the bootloader
    for (;;) {
        __asm__("hlt");
    }
}
