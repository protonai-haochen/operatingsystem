#include <efi.h>
#include <efilib.h>

typedef struct {
    UINT64 framebuffer_base;
    UINT32 framebuffer_width;
    UINT32 framebuffer_height;
    UINT32 framebuffer_pitch;
} BOOTINFO;

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

// Open the root of the filesystem the bootloader was loaded from.
static EFI_STATUS open_root(EFI_HANDLE ImageHandle, EFI_FILE_HANDLE *Root) {
    EFI_STATUS Status;
    EFI_LOADED_IMAGE *LoadedImage;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFS;

    // Get EFI_LOADED_IMAGE for our ImageHandle
    Status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               ImageHandle,
                               &LoadedImageProtocol,
                               (VOID **)&LoadedImage);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] HandleProtocol(LoadedImage) failed: %r\r\n", Status);
        return Status;
    }

    // Get SimpleFS for the device that contains this image
    Status = uefi_call_wrapper(BS->HandleProtocol, 3,
                               LoadedImage->DeviceHandle,
                               &SimpleFileSystemProtocol,
                               (VOID **)&SimpleFS);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] HandleProtocol(SimpleFS) failed: %r\r\n", Status);
        return Status;
    }

    // Open the filesystem root
    Status = uefi_call_wrapper(SimpleFS->OpenVolume, 2, SimpleFS, Root);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] OpenVolume failed: %r\r\n", Status);
        return Status;
    }

    return EFI_SUCCESS;
}

// Load kernel.bin into memory at 1 MiB and return its address + size.
static EFI_STATUS load_kernel(EFI_HANDLE ImageHandle,
                              EFI_PHYSICAL_ADDRESS *KernelAddr,
                              UINTN *KernelSize) {
    EFI_STATUS Status;
    EFI_FILE_HANDLE Root;
    EFI_FILE_HANDLE File;

    Status = open_root(ImageHandle, &Root);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] open_root failed: %r\r\n", Status);
        return Status;
    }

    // kernel.bin is in the root of the volume
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

    // Allocate 1 MiB for the kernel at physical address 1 MiB
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
        // We can still keep going without graphics if we want.
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

    // Call into the kernel
    entry(&bi);

    // If the kernel ever returns, just halt.
    Print(L"[boot] Kernel returned, halting.\r\n");
    for (;;) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}

