#ifndef LIGHTOS_BOOT_H
#define LIGHTOS_BOOT_H

#include <stdint.h>

typedef struct {
    uint64_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
} BootInfo;

#endif
