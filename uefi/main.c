#include <efi.h>
#include <efilib.h>

// Kernel entrypoint type.
// fb_base: physical framebuffer base address
// width/height: resolution
// pitch: pixels per scan line
typedef void (*KernelEntry)(
    UINT64 fb_base,
    UINT32 width,
    UINT32 height,
    UINT32 pitch
);

// Load "kernel.bin" into the provided buffer.
static EFI_STATUS load_kernel(EFI_HANDLE ImageHandle,
                              VOID *buffer,
                              UINTN buffer_size,
                              UINTN *bytes_read)
{
    EFI_STATUS status;
    EFI_FILE_HANDLE root = NULL;
    EFI_FILE_HANDLE file = NULL;

    root = LibOpenRoot(ImageHandle);
    if (root == NULL) {
        Print(L"[boot] LibOpenRoot failed\r\n");
        return EFI_NOT_FOUND;
    }

    status = uefi_call_wrapper(
        root->Open,
        5,
        root,
        &file,
        L"kernel.bin",
        EFI_FILE_MODE_READ,
        0
    );
    if (EFI_ERROR(status)) {
        Print(L"[boot] Unable to open kernel.bin: %r\r\n", status);
        return status;
    }

    *bytes_read = buffer_size;
    status = uefi_call_wrapper(
        file->Read,
        3,
        file,
        bytes_read,
        buffer
    );

    uefi_call_wrapper(file->Close, 1, file);

    if (EFI_ERROR(status)) {
        Print(L"[boot] Failed to read kernel.bin: %r\r\n", status);
        return status;
    }

    return EFI_SUCCESS;
}

EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    InitializeLib(ImageHandle, SystemTable);
    Print(L"LightOS UEFI bootloader starting...\r\n");

    // --- Locate GOP (graphics) ---
    EFI_STATUS status;
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;

    status = uefi_call_wrapper(
        BS->LocateProtocol,
        3,
        &gopGuid,
        NULL,
        (VOID **)&gop
    );
    if (EFI_ERROR(status) || gop == NULL) {
        Print(L"[boot] Unable to locate GOP: %r\r\n", status);
        return status;
    }

    UINT64 fb_base = gop->Mode->FrameBufferBase;
    UINT32 width   = gop->Mode->Info->HorizontalResolution;
    UINT32 height  = gop->Mode->Info->VerticalResolution;
    UINT32 pitch   = gop->Mode->Info->PixelsPerScanLine;

    Print(L"[boot] Framebuffer @ 0x%lx (%ux%u, pitch %u)\r\n",
          fb_base, width, height, pitch);

    // --- Load kernel.bin into memory ---
    const UINTN kernel_max_size = 2 * 1024 * 1024; // 2 MiB
    VOID *kernel_buffer = AllocatePool(kernel_max_size);
    if (kernel_buffer == NULL) {
        Print(L"[boot] Failed to allocate kernel buffer\r\n");
        return EFI_OUT_OF_RESOURCES;
    }

    UINTN kernel_size = 0;
    status = load_kernel(ImageHandle, kernel_buffer, kernel_max_size, &kernel_size);
    if (EFI_ERROR(status)) {
        Print(L"[boot] load_kernel failed: %r\r\n", status);
        FreePool(kernel_buffer);
        return status;
    }

    Print(L"[boot] Loaded kernel.bin (%lu bytes) at 0x%lx\r\n",
          (UINT64)kernel_size, (UINT64)kernel_buffer);

    // --- Jump to kernel ---
    KernelEntry entry = (KernelEntry)kernel_buffer;
    Print(L"[boot] Jumping to kernel...\r\n");

    // For now we *do not* call ExitBootServices; this is enough to
    // play with the framebuffer and prove the kernel runs.
    entry(fb_base, width, height, pitch);

    // If the kernel ever returns, just hang.
    Print(L"[boot] Kernel returned, halting.\r\n");
    for (;;) {
        // spin
    }

    return EFI_SUCCESS; // not actually reached
}
