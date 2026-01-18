#include <efi.h>
#include <efilib.h>

typedef struct {
    UINT64 framebuffer_base;
    UINT32 framebuffer_width;
    UINT32 framebuffer_height;
    UINT32 framebuffer_pitch;
} BOOTINFO;

// ---------------------------------------------------------------------
// Graphics init (GOP)
// ---------------------------------------------------------------------
static EFI_STATUS init_graphics(EFI_GRAPHICS_OUTPUT_PROTOCOL **Gop,
                                BOOTINFO *bi) {
    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    Status = uefi_call_wrapper(BS->LocateProtocol, 3,
                               &gopGuid, NULL, (VOID **)&gop);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] LocateProtocol(GOP) failed: %r\r\n", Status);
        return Status;
    }

    *Gop = gop;

    bi->framebuffer_base   = gop->Mode->FrameBufferBase;
    bi->framebuffer_width  = gop->Mode->Info->HorizontalResolution;
    bi->framebuffer_height = gop->Mode->Info->VerticalResolution;
    bi->framebuffer_pitch  = gop->Mode->Info->PixelsPerScanLine;

    Print(L"[boot] Framebuffer @ 0x%lx (%ux%u, pitch %u)\r\n",
          bi->framebuffer_base,
          bi->framebuffer_width,
          bi->framebuffer_height,
          bi->framebuffer_pitch);

    return EFI_SUCCESS;
}

// ---------------------------------------------------------------------
// Load kernel.bin from the same disk as this UEFI app
// ---------------------------------------------------------------------
static EFI_STATUS load_kernel(EFI_HANDLE ImageHandle,
                              EFI_PHYSICAL_ADDRESS *KernelAddr,
                              UINTN *KernelSize) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_FILE_HANDLE Root;
    EFI_FILE_HANDLE File;

    //
    // 1) Get EFI_LOADED_IMAGE for our ImageHandle
    //
    Status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               ImageHandle,
                               &LoadedImageProtocol,
                               (VOID **)&LoadedImage);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] HandleProtocol(LoadedImage) failed: %r\r\n", Status);
        return Status;
    }

    //
    // 2) Open the filesystem root of the device we were loaded from.
    //    IMPORTANT: use LoadedImage->DeviceHandle (this is what was wrong
    //    in the older code that printed 'LibOpenRoot failed').
    //
    Status = LibOpenRoot(LoadedImage->DeviceHandle, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] LibOpenRoot failed: %r\r\n", Status);
        return Status;
    }

    //
    // 3) Open \kernel.bin from the root of that filesystem
    //
    Status = uefi_call_wrapper(Root->Open, 5,
                               Root,
                               &File,
                               L"kernel.bin",
                               EFI_FILE_MODE_READ,
                               0);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] open kernel.bin failed: %r\r\n", Status);
        return Status;
    }

    //
    // 4) Allocate 1 MiB at physical 0x00100000 and read kernel.bin there
    //
    EFI_PHYSICAL_ADDRESS Dest = 0x00100000;
    UINTN Pages = EFI_SIZE_TO_PAGES(1024 * 1024);

    Status = uefi_call_wrapper(BS->AllocatePages, 4,
                               AllocateAddress,
                               EfiLoaderCode,
                               Pages,
                               &Dest);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] AllocatePages failed: %r\r\n", Status);
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    UINTN BufferSize = 1024 * 1024;
    Status = uefi_call_wrapper(File->Read, 3,
                               File,
                               &BufferSize,
                               (VOID *)(UINTN)Dest);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] File->Read failed: %r\r\n", Status);
        uefi_call_wrapper(File->Close, 1, File);
        return Status;
    }

    uefi_call_wrapper(File->Close, 1, File);

    *KernelAddr = Dest;
    *KernelSize = BufferSize;

    Print(L"[boot] kernel.bin loaded at 0x%lx (%lu bytes)\r\n",
          Dest, BufferSize);

    return EFI_SUCCESS;
}

// ---------------------------------------------------------------------
// UEFI entrypoint
// ---------------------------------------------------------------------
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle,
                           EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    Print(L"LightOS UEFI bootloader starting...\r\n");

    EFI_STATUS Status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    BOOTINFO bi;

    Status = init_graphics(&Gop, &bi);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] init_graphics failed: %r\r\n", Status);
        // We *could* continue in text mode, but for now just fall through.
    }

    EFI_PHYSICAL_ADDRESS KernelAddr = 0;
    UINTN KernelSize = 0;

    Status = load_kernel(ImageHandle, &KernelAddr, &KernelSize);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] load_kernel failed: %r\r\n", Status);
        Print(L"[boot] Halting in bootloader.\r\n");
        for (;;) {
            __asm__ __volatile__("hlt");
        }
        return Status;
    }

    Print(L"[boot] Jumping to kernel at 0x%lx\r\n", KernelAddr);

    typedef void (*kernel_entry_t)(BOOTINFO *);
    kernel_entry_t entry = (kernel_entry_t)(UINTN)KernelAddr;

    // Transfer control to the kernel
    entry(&bi);

    // If the kernel ever returns, just halt.
    Print(L"[boot] Kernel returned, halting.\r\n");
    for (;;) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}
