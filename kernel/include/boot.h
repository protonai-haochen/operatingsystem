#ifndef LIGHTOS_BOOT_H
#define LIGHTOS_BOOT_H

#include <stdint.h>

// This structure is passed from the UEFI loader to the kernel.
// We extended it with RTC date/time so the kernel can show a real clock.
typedef struct {
    uint64_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;

    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} BootInfo;

#endif
