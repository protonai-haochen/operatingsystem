#include <efi.h>
#include <efilib.h>
#include <stdint.h>

// Must match the struct used in kernel/core/kernel.c
typedef struct {
    uint64_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;   // pixels per scanline
} BootInfo;

#define KERNEL_PATH      L"\\kernel.bin"
#define KERNEL_LOAD_ADDR 0x00100000ULL   // 1 MiB

typedef void (*kernel_entry_t)(BootInfo *bi);

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    Print(L"[LightOS] UEFI loader starting...\r\n");

    EFI_STATUS Status;

    // 1. Get LOADED_IMAGE for this application
    EFI_LOADED_IMAGE *LoadedImage = NULL;
    Status = uefi_call_wrapper(
        BS->HandleProtocol, 3,
        ImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (VOID **)&LoadedImage
    );
    if (EFI_ERROR(Status)) {
        Print(L"[boot] HandleProtocol(LoadedImage) failed: %r\r\n", Status);
        return Status;
    }

    // 2. Get SimpleFileSystem from the device we were loaded from
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;
    Status = uefi_call_wrapper(
        BS->HandleProtocol, 3,
        LoadedImage->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID **)&FileSystem
    );
    if (EFI_ERROR(Status)) {
        Print(L"[boot] HandleProtocol(SimpleFS) failed: %r\r\n", Status);
        return Status;
    }

    // 3. Open the volume and then \kernel.bin
    EFI_FILE_HANDLE Volume = NULL;
    Status = uefi_call_wrapper(FileSystem->OpenVolume, 2, FileSystem, &Volume);
    if (EFI_ERROR(Status)) {
        Print(L"[boot] OpenVolume failed: %r\r\n", Status);
        return Status;
    }

    EFI_FILE_HANDLE KernelFile = NULL;
    Status = uefi_call_wrapper(
        Volume->Open, 5,
        Volume,
        &KernelFile,
        KERNEL_PATH,
        EFI_FILE_MODE_READ,
        0
    );
    if (EFI_ERROR(Status)) {
        Print(L"[boot] Failed to open %s: %r\r\n", KERNEL_PATH, Status);
        return Status;
    }

    // 4. Get file size (EFI_FILE_INFO)
    EFI_FILE_INFO *FileInfo = NULL;
    UINTN InfoSize = SIZE_OF_EFI_FILE_INFO + 256;

    Status = uefi_call_wrapper(
        BS->AllocatePool, 3,
        EfiLoaderData,
        InfoSize,
        (VOID **)&FileInfo
    );
    if (EFI_ERROR(Status)) {
        Print(L"[boot] AllocatePool(FileInfo) failed: %r\r\n", Status);
        return Status;
    }

    Status = uefi_call_wrapper(
        KernelFile->GetInfo, 4,
        KernelFile,
        &gEfiFileInfoGuid,
        &InfoSize,
        FileInfo
    );
    if (EFI_ERROR(Status)) {
        Print(L"[boot] GetInfo(FileInfo) failed: %r\r\n", Status);
        return Status;
    }

    UINTN KernelSize = (UINTN)FileInfo->FileSize;
    Print(L"[boot] kernel.bin size: %lu bytes\r\n", KernelSize);

    // 5. Read kernel into 0x00100000
    VOID *KernelBuffer = (VOID *)(UINTN)KERNEL_LOAD_ADDR;
    UINTN ReadSize = KernelSize;

    Status = uefi_call_wrapper(
        KernelFile->Read, 3,
        KernelFile,
        &ReadSize,
        KernelBuffer
    );
    if (EFI_ERROR(Status) || ReadSize != KernelSize) {
        Print(L"[boot] Read(kernel.bin) failed: %r (read %lu / %lu)\r\n",
              Status, ReadSize, KernelSize);
        return Status;
    }

    Print(L"[boot] kernel.bin loaded at 0x%lx (%lu bytes)\r\n",
          (UINT64)KERNEL_LOAD_ADDR, KernelSize);

    // Done with files
    uefi_call_wrapper(KernelFile->Close, 1, KernelFile);
    uefi_call_wrapper(Volume->Close, 1, Volume);

    // 6. Get graphics output protocol (for framebuffer info)
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    Status = uefi_call_wrapper(
        BS->LocateProtocol, 3,
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        (VOID **)&Gop
    );
    if (EFI_ERROR(Status)) {
        Print(L"[boot] LocateProtocol(GOP) failed: %r\r\n", Status);
        return Status;
    }

    BootInfo bi;
    bi.framebuffer_base   = Gop->Mode->FrameBufferBase;
    bi.framebuffer_width  = Gop->Mode->Info->HorizontalResolution;
    bi.framebuffer_height = Gop->Mode->Info->VerticalResolution;
    bi.framebuffer_pitch  = Gop->Mode->Info->PixelsPerScanLine;

    Print(L"[boot] Framebuffer @ 0x%lx (%ux%u, pitch %u)\r\n",
          bi.framebuffer_base, bi.framebuffer_width,
          bi.framebuffer_height, bi.framebuffer_pitch);

    // 7. Jump to kernel
    kernel_entry_t entry = (kernel_entry_t)(UINTN)KERNEL_LOAD_ADDR;

    Print(L"[boot] Jumping to kernel at 0x%lx\r\n", (UINT64)KERNEL_LOAD_ADDR);
    entry(&bi);   // << kernel_main(BootInfo*)

    // 8. If we ever come back, just halt
    Print(L"[boot] Kernel returned, halting.\r\n");
    for (;;) {
        __asm__ volatile("hlt");
    }

    // not reached
    return EFI_SUCCESS;
}
