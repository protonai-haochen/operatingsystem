#include <stdint.h>

static inline void hlt(void) {
    __asm__ __volatile__("hlt");
}

// Very tiny test kernel: draw a gradient and halt.
void kernel_main(uint64_t fb_base,
                 uint32_t width,
                 uint32_t height,
                 uint32_t pitch)
{
    uint32_t *fb = (uint32_t *)fb_base;

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t r = (x * 255) / (width  ? width  : 1);
            uint8_t g = (y * 255) / (height ? height : 1);
            uint8_t b = 64;

            uint32_t color =
                ((uint32_t)r << 16) |
                ((uint32_t)g << 8)  |
                ((uint32_t)b);

            fb[y * pitch + x] = color;
        }
    }

    // Hang the CPU so the screen stays put.
    for (;;) {
        hlt();
    }
}
