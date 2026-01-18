#include <stdint.h>
#include "boot.h"

void kernel_main(BootInfo *boot) {
    uint32_t *fb = (uint32_t*)boot->framebuffer_base;
    uint32_t width  = boot->framebuffer_width;
    uint32_t height = boot->framebuffer_height;
    uint32_t pitch  = boot->framebuffer_pitch;

    // Fill screen with dark blue to prove the KERNEL is drawing, not UEFI
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            fb[x + y * pitch] = 0x00002080;  // dark-ish blue
        }
    }

    // Simple halt loop so CPU doesn't run wild
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
