#include <efi.h>
#include <efilib.h>

EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
UINT32 ScreenWidth, ScreenHeight, Pitch;
UINT8 *FrameBuffer;

//
// --- BASIC GRAPHICS API ---
//

void putpixel(UINT32 x, UINT32 y, UINT32 color) {
    if (x >= ScreenWidth || y >= ScreenHeight) return;
    UINT32 *pixel = (UINT32 *)(FrameBuffer + (x + y * Pitch) * 4);
    *pixel = color;
}

void fill_rect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT32 color) {
    for (UINT32 iy = 0; iy < h; iy++) {
        for (UINT32 ix = 0; ix < w; ix++) {
            putpixel(x + ix, y + iy, color);
        }
    }
}

void draw_rect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT32 color) {
    // top
    for (UINT32 i = 0; i < w; i++) putpixel(x + i, y, color);
    // bottom
    for (UINT32 i = 0; i < w; i++) putpixel(x + i, y + h - 1, color);
    // left
    for (UINT32 i = 0; i < h; i++) putpixel(x, y + i, color);
    // right
    for (UINT32 i = 0; i < h; i++) putpixel(x + w - 1, y + i, color);
}

//
// --- DRAW CHROMEOS-LIKE WINDOW ---
//

void draw_window(UINT32 x, UINT32 y, UINT32 w, UINT32 h) {

    UINT32 border = 0xFFCCCCCC;      // light gray border
    UINT32 bg     = 0xFFFFFFFF;      // white window background
    UINT32 title  = 0xFFEFEFEF;      // light gray title bar (flat, no rounding)
    UINT32 shadow = 0x22000000;      // faint drop shadow

    // Draw shadow (ChromeOS style: subtle rectangular shadow)
    fill_rect(x + 4, y + 4, w, h, shadow);

    // Draw window background
    fill_rect(x, y, w, h, bg);

    // Draw title bar (no rounded corners – square)
    fill_rect(x, y, w, 28, title);

    // Draw border
    draw_rect(x, y, w, h, border);

    // Draw title text (EFI text mode, not yet rendered in framebuffer)
    // TODO: later we add custom bitmap font rendering
    Print(L"[LightOS] Demo Window Drawn.\n");
}

//
// --- ENTRY POINT ---
//

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"LightOS GUI Boot...\n");

    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_STATUS status = uefi_call_wrapper(
        SystemTable->BootServices->LocateProtocol,
        3, &gopGuid, NULL, (void**)&gop
    );
    if (EFI_ERROR(status)) {
        Print(L"Could not load GOP: %r\n", status);
        return status;
    }

    // Set graphics mode 0 (safe)
    uefi_call_wrapper(gop->SetMode, 2, gop, 0);

    // Load screen info
    FrameBuffer = (UINT8*)gop->Mode->FrameBufferBase;
    ScreenWidth = gop->Mode->Info->HorizontalResolution;
    ScreenHeight = gop->Mode->Info->VerticalResolution;
    Pitch = gop->Mode->Info->PixelsPerScanLine;

    // Fill background (ChromeOS blue-ish gray)
    fill_rect(0, 0, ScreenWidth, ScreenHeight, 0xFFE5ECF4);

    // Draw taskbar (ChromeOS style)
    fill_rect(0, ScreenHeight - 48, ScreenWidth, 48, 0xFFFFFFFF);

    // Draw demo window centered
    UINT32 winW = 600;
    UINT32 winH = 400;
    UINT32 winX = (ScreenWidth - winW) / 2;
    UINT32 winY = (ScreenHeight - winH) / 2 - 20;

    draw_window(winX, winY, winW, winH);

    Print(L"GUI initialized.\n");

    while (1);
    return EFI_SUCCESS;
}
