#include <efi.h>
#include <efilib.h>
#include <stdint.h>

// Global framebuffer state
static UINT32 *g_fb    = 0;
static UINT32  g_width = 0;
static UINT32  g_height = 0;
static UINT32  g_pitch  = 0;   // pixels per scanline

static inline void put_pixel(UINT32 x, UINT32 y, UINT32 color) {
    if (x >= g_width || y >= g_height) {
        return;
    }
    g_fb[y * g_pitch + x] = color;
}

static void fill_rect(UINT32 x, UINT32 y,
                      UINT32 w, UINT32 h,
                      UINT32 color) {
    if (x >= g_width || y >= g_height) {
        return;
    }

    UINT32 max_x = x + w;
    UINT32 max_y = y + h;
    if (max_x > g_width)  max_x = g_width;
    if (max_y > g_height) max_y = g_height;

    for (UINT32 yy = y; yy < max_y; ++yy) {
        UINT32 *row = g_fb + yy * g_pitch;
        for (UINT32 xx = x; xx < max_x; ++xx) {
            row[xx] = color;
        }
    }
}

// --- Boot screen: Light bulb + fake progress bar ---
static void draw_boot_screen(void) {
    const UINT32 bg = 0x101020;       // dark blue-ish
    const UINT32 bulb_body = 0xFFFFCC;
    const UINT32 bulb_outline = 0xCCCC99;
    const UINT32 bar_bg = 0x202030;
    const UINT32 bar_fg = 0x60A0FF;

    fill_rect(0, 0, g_width, g_height, bg);

    UINT32 cx = g_width / 2;
    UINT32 cy = g_height / 3;

    UINT32 bulb_w = g_width / 12;
    UINT32 bulb_h = g_height / 6;
    if (bulb_w < 40) bulb_w = 40;
    if (bulb_h < 60) bulb_h = 60;

    UINT32 bulb_x = (cx > bulb_w/2) ? (cx - bulb_w/2) : 0;
    UINT32 bulb_y = (cy > bulb_h/2) ? (cy - bulb_h/2) : 0;

    // bulb body
    fill_rect(bulb_x, bulb_y, bulb_w, bulb_h, bulb_body);

    // outline
    fill_rect(bulb_x, bulb_y, bulb_w, 2, bulb_outline);
    fill_rect(bulb_x, bulb_y + bulb_h - 2, bulb_w, 2, bulb_outline);
    fill_rect(bulb_x, bulb_y, 2, bulb_h, bulb_outline);
    fill_rect(bulb_x + bulb_w - 2, bulb_y, 2, bulb_h, bulb_outline);

    // socket
    UINT32 sock_h = bulb_h / 5;
    if (sock_h < 6) sock_h = 6;
    fill_rect(bulb_x, bulb_y + bulb_h, bulb_w, sock_h, bar_bg);

    // progress bar
    UINT32 bar_y = bulb_y + bulb_h + sock_h + 20;
    UINT32 bar_w = g_width / 3;
    UINT32 bar_h = (g_height / 100) + 6;
    if (bar_w < 60) bar_w = 60;
    if (bar_h < 4)  bar_h = 4;

    UINT32 x = (cx > bar_w/2) ? (cx - bar_w/2) : 0;
    fill_rect(x, bar_y, bar_w, bar_h, bar_bg);
    UINT32 filled = (bar_w * 70) / 100; // fake 70% loaded
    fill_rect(x, bar_y, filled, bar_h, bar_fg);
}

// --- Desktop UI: taskbar, tray, icons, square window ---
static void draw_desktop(void) {
    const UINT32 desktop_bg   = 0x003366;
    const UINT32 taskbar_bg   = 0x202020;
    const UINT32 tray_bg      = 0x303030;
    const UINT32 icon_bg      = 0x406080;
    const UINT32 window_border = 0x000000;
    const UINT32 window_title  = 0x004080;
    const UINT32 window_body   = 0xC0C0C0;
    const UINT32 close_btn     = 0x800000;

    // background
    fill_rect(0, 0, g_width, g_height, desktop_bg);

    // taskbar
    UINT32 taskbar_h = g_height / 14;
    if (taskbar_h < 28) taskbar_h = 28;
    UINT32 taskbar_y = g_height - taskbar_h;
    fill_rect(0, taskbar_y, g_width, taskbar_h, taskbar_bg);

    // start button (left)
    UINT32 start_margin = 4;
    UINT32 start_w = 80;
    UINT32 start_h = taskbar_h - 2 * start_margin;
    fill_rect(start_margin, taskbar_y + start_margin, start_w, start_h, 0x404040);

    // tray (right)
    UINT32 tray_w = g_width / 5;
    if (tray_w < 140) tray_w = 140;
    UINT32 tray_x = g_width - tray_w - 4;
    fill_rect(tray_x, taskbar_y + 2, tray_w, taskbar_h - 4, tray_bg);

    UINT32 icon_size = taskbar_h / 2;
    if (icon_size < 12) icon_size = 12;
    UINT32 icon_y = taskbar_y + (taskbar_h - icon_size) / 2;
    UINT32 ix = tray_x + 8;

    // wifi
    fill_rect(ix, icon_y, icon_size, icon_size, 0x808080);
    ix += icon_size + 6;
    // speaker
    fill_rect(ix, icon_y, icon_size, icon_size, 0xA0A0A0);
    ix += icon_size + 6;
    // battery
    fill_rect(ix, icon_y, icon_size + 4, icon_size, 0x70A070);
    ix += icon_size + 10;
    // time/date block
    UINT32 time_w = tray_x + tray_w - ix - 8;
    if (time_w > 0) {
        fill_rect(ix, icon_y, time_w, icon_size, 0x505050);
    }

    // desktop icons (Settings, Files, Command Block, Browser)
    UINT32 icon_w = g_width / 14;
    UINT32 icon_h = g_height / 10;
    if (icon_w < 72) icon_w = 72;
    if (icon_h < 72) icon_h = 72;

    UINT32 col_x = 20;
    UINT32 row_y = 40;
    UINT32 v_gap = 20;

    fill_rect(col_x, row_y, icon_w, icon_h, icon_bg); // Settings
    row_y += icon_h + v_gap;
    fill_rect(col_x, row_y, icon_w, icon_h, icon_bg); // File Block
    row_y += icon_h + v_gap;
    fill_rect(col_x, row_y, icon_w, icon_h, icon_bg); // Command Block
    row_y += icon_h + v_gap;
    fill_rect(col_x, row_y, icon_w, icon_h, icon_bg); // Integrated Browser

    // square window in center
    UINT32 win_w = (g_width * 3) / 5;
    UINT32 win_h = (g_height * 3) / 5;
    UINT32 win_x = (g_width - win_w) / 2;
    UINT32 win_y = (g_height - win_h) / 2;
    if (win_y < 10) win_y = 10;

    // border
    fill_rect(win_x - 1, win_y - 1, win_w + 2, win_h + 2, window_border);
    // title bar
    UINT32 title_h = 28;
    fill_rect(win_x, win_y, win_w, title_h, window_title);
    // body
    fill_rect(win_x, win_y + title_h, win_w, win_h - title_h, window_body);

    // close button (square)
    UINT32 btn_size = (title_h > 8) ? (title_h - 8) : (title_h / 2);
    UINT32 btn_x = win_x + win_w - btn_size - 4;
    UINT32 btn_y = win_y + (title_h - btn_size) / 2;
    fill_rect(btn_x, btn_y, btn_size, btn_size, close_btn);
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    // Get GOP
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    EFI_STATUS Status = uefi_call_wrapper(
        BS->LocateProtocol,
        3,
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        (VOID**)&Gop
    );
    if (EFI_ERROR(Status)) {
        Print(L"[LightOS] LocateProtocol(GOP) failed: %r\r\n", Status);
        return Status;
    }

    g_fb     = (UINT32*)(UINTN)Gop->Mode->FrameBufferBase;
    g_width  = Gop->Mode->Info->HorizontalResolution;
    g_height = Gop->Mode->Info->VerticalResolution;
    g_pitch  = Gop->Mode->Info->PixelsPerScanLine;

    // Boot screen
    draw_boot_screen();

    // crude delay loop so you can see boot screen
    for (volatile UINT64 i = 0; i < 500000000ULL; ++i) {
        __asm__ volatile("");
    }

    // Desktop UI
    draw_desktop();

    // Never return; spin forever so firmware doesn't come back
    while (1) {
        __asm__ volatile("pause");
    }

    // not reached
    return EFI_SUCCESS;
}
