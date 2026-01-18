#include <stdint.h>
#include "../include/boot.h"

void kernel_main(BootInfo* info) {
    uint32_t* fb = (uint32_t*) info->framebuffer_base;
    uint32_t width  = info->framebuffer_width;
    uint32_t height = info->framebuffer_height;
    uint32_t pitch  = info->framebuffer_pitch; // pixels per scanline

    // Super-obvious debug color (bright magenta)
    uint32_t color = 0x00FF00FF;  // 0x00RRGGBB

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            fb[y * pitch + x] = color;
        }
    }

    // Stay here forever so QEMU doesn't return to firmware
    for (;;) {
        __asm__("hlt");
    }
}

