// kernel/core/kernel.c

#include <stdint.h>
#include "boot.h"

// Simple wrapper so we can halt the CPU cleanly forever.
static inline void hlt(void) {
    __asm__ volatile("hlt");
}

// This is the ONLY entry point the bootloader calls.
// It must never return.
__attribute__((noreturn))
void kernel_main(BootInfo* bi) {
    // Framebuffer info from BootInfo (must match uefi/main.c)
    uint32_t* fb   = (uint32_t*)(uintptr_t)bi->framebuffer_base;
    uint32_t width  = bi->framebuffer_width;
    uint32_t height = bi->framebuffer_height;
    uint32_t pitch  = bi->framebuffer_pitch;   // pixels per scanline

    // Magenta in 0xRRGGBB
    const uint32_t color = 0xFF00FF;

    // Fill the entire framebuffer
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t* row = fb + (uintptr_t)y * pitch;
        for (uint32_t x = 0; x < width; ++x) {
            row[x] = color;
        }
    }

    // Kernel MUST NOT return – just halt forever.
    for (;;) {
        hlt();
    }
}
