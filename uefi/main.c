#include <efi.h>
#include <efilib.h>

EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    //
    // Initialize UEFI Library
    //
    InitializeLib(ImageHandle, SystemTable);
    Print(L"LightOS booting (UEFI mode)...\n");

    //
    // Locate the Graphics Output Protocol (GOP)
    //
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_STATUS status;

    Print(L"Locating GOP...\n");

    status = uefi_call_wrapper(
        SystemTable->BootServices->LocateProtocol,
        3,
        &gopGuid,
        NULL,
        (void **)&gop
    );

    if (EFI_ERROR(status)) {
        Print(L"[ERROR] Could not locate GOP: %r\n", status);
        return status;
    }

    //
    // Use the default graphics mode (Mode 0)
    //
    Print(L"GOP located. Switching to graphics mode...\n");

    status = uefi_call_wrapper(gop->SetMode, 2, gop, 0);
    if (EFI_ERROR(status)) {
        Print(L"[ERROR] SetMode failed: %r\n", status);
        return status;
    }

    //
    // Retrieve framebuffer information
    //
    UINT8  *fb     = (UINT8 *) gop->Mode->FrameBufferBase;
    UINT32 width   = gop->Mode->Info->HorizontalResolution;
    UINT32 height  = gop->Mode->Info->VerticalResolution;
    UINT32 pitch   = gop->Mode->Info->PixelsPerScanLine;

    Print(L"Graphics Mode: %dx%d\n", width, height);

    //
    // Simple GUI test — fill the entire screen with blue
    //
    Print(L"Drawing GUI background...\n");

    for (UINT32 y = 0; y < height; y++) {
        for (UINT32 x = 0; x < width; x++) {

            UINT32 *pixel = (UINT32 *)(fb + (x + y * pitch) * 4);

            UINT8 blue  = 255;
            UINT8 green = 0;
            UINT8 red   = 0;

            *pixel = (blue) | (green << 8) | (red << 16);
        }
    }

    Print(L"LightOS GUI initialized.\n");

    //
    // Freeze the screen (UEFI apps exit instantly otherwise)
    //
    while (1);

    return EFI_SUCCESS;
}
