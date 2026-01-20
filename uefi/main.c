#include <efi.h>
#include <efilib.h>
#include <stdint.h>

#define KERNEL_PATH      L"\\kernel.bin"
#define KERNEL_LOAD_ADDR 0x00100000ULL

// Must match kernel/include/boot.h
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

// Kernel entry: must match kernel/core/kernel.c
typedef void (*KernelEntry)(BootInfo *);

// Small helper to panic out
static EFI_STATUS boot_panic(EFI_STATUS Status, CHAR16 *Message) {
    Print(L"[boot] ERROR: %s: %r\r\n", Message, Status);
    return Status;
}

EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    Print(L"[LightOS] UEFI loader starting...\r\n");

    EFI_STATUS Status;

    // --- 1. Get LOADED_IMAGE for this application ---
    EFI_LOADED_IMAGE *LoadedImage = NULL;
    Status = uefi_call_wrapper(
        BS->HandleProtocol, 3,
        ImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (VOID **)&LoadedImage
    );
    if (EFI_ERROR(Status)) {
        return boot_panic(Status, L"HandleProtocol(LoadedImage) failed");
    }

    // --- 2. Get SimpleFileSystem from the device we were loaded from ---
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FileSystem = NULL;
    Status = uefi_call_wrapper(
        BS->HandleProtocol, 3,
        LoadedImage->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID **)&FileSystem
    );
    if (EFI_ERROR(Status)) {
        return boot_panic(Status, L"HandleProtocol(SimpleFileSystem) failed");
    }

    // --- 3. Open volume (root directory) ---
    EFI_FILE_PROTOCOL *Root = NULL;
    Status = uefi_call_wrapper(
        FileSystem->OpenVolume, 2,
        FileSystem, &Root
    );
    if (EFI_ERROR(Status)) {
        return boot_panic(Status, L"OpenVolume failed");
    }

    // --- 4. Open kernel file ---
    EFI_FILE_PROTOCOL *KernelFile = NULL;
    Status = uefi_call_wrapper(
        Root->Open, 5,
        Root,
        &KernelFile,
        KERNEL_PATH,
        EFI_FILE_MODE_READ,
        0
    );
    if (EFI_ERROR(Status)) {
        return boot_panic(Status, L"Failed to open \\kernel.bin");
    }

    // --- 5. Get kernel file size ---
    EFI_FILE_INFO *FileInfo = NULL;
    UINTN FileInfoSize = sizeof(EFI_FILE_INFO) + 256;
    Status = uefi_call_wrapper(
        BS->AllocatePool, 3,
        EfiLoaderData,
        FileInfoSize,
        (VOID **)&FileInfo
    );
    if (EFI_ERROR(Status)) {
        return boot_panic(Status, L"AllocatePool(FileInfo) failed");
    }

    Status = uefi_call_wrapper(
        KernelFile->GetInfo, 4,
        KernelFile,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        FileInfo
    );
    if (EFI_ERROR(Status)) {
        uefi_call_wrapper(BS->FreePool, 1, FileInfo);
        return boot_panic(Status, L"GetInfo(FileInfo) failed");
    }

    UINTN KernelSize = FileInfo->FileSize;
    uefi_call_wrapper(BS->FreePool, 1, FileInfo);

    Print(L"[boot] kernel.bin size: %lu bytes\r\n", KernelSize);

    // --- 6. Read kernel into physical memory at KERNEL_LOAD_ADDR ---
    VOID *KernelBuf = (VOID *)KERNEL_LOAD_ADDR;
    Status = uefi_call_wrapper(
        KernelFile->Read, 3,
        KernelFile,
        &KernelSize,
        KernelBuf
    );
    uefi_call_wrapper(KernelFile->Close, 1, KernelFile);

    if (EFI_ERROR(Status)) {
        return boot_panic(Status, L"Failed to read kernel.bin");
    }

    // --- 7. Locate GOP (framebuffer) ---
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    Status = uefi_call_wrapper(
        BS->LocateProtocol, 3,
        &gEfiGraphicsOutputProtocolGuid,
        NULL,
        (VOID **)&Gop
    );
    if (EFI_ERROR(Status) || !Gop || !Gop->Mode || !Gop->Mode->Info) {
        return boot_panic(Status, L"LocateProtocol(GOP) failed");
    }

    Print(L"[boot] GOP mode %u: %ux%u pitch %u\r\n",
          Gop->Mode->Mode,
          Gop->Mode->Info->HorizontalResolution,
          Gop->Mode->Info->VerticalResolution,
          Gop->Mode->Info->PixelsPerScanLine);

    // --- 8. Fill BootInfo including RTC time ---
    BootInfo bi;
    bi.framebuffer_base   = Gop->Mode->FrameBufferBase;
    bi.framebuffer_width  = Gop->Mode->Info->HorizontalResolution;
    bi.framebuffer_height = Gop->Mode->Info->VerticalResolution;
    bi.framebuffer_pitch  = Gop->Mode->Info->PixelsPerScanLine;

    EFI_TIME Now;
    Status = uefi_call_wrapper(RT->GetTime, 2, &Now, NULL);
    if (EFI_ERROR(Status)) {
        bi.year   = 2026;
        bi.month  = 1;
        bi.day    = 1;
        bi.hour   = 0;
        bi.minute = 0;
        bi.second = 0;
        Print(L"[boot] GetTime failed, using fallback 2026-01-01 00:00:00\r\n");
    } else {
        bi.year   = Now.Year;
        bi.month  = Now.Month;
        bi.day    = Now.Day;
        bi.hour   = Now.Hour;
        bi.minute = Now.Minute;
        bi.second = Now.Second;
        Print(L"[boot] RTC time: %04u-%02u-%02u %02u:%02u:%02u\r\n",
              bi.year, bi.month, bi.day,
              bi.hour, bi.minute, bi.second);
    }

    Print(L"[boot] Jumping to kernel at 0x%lx\r\n", (UINT64)KERNEL_LOAD_ADDR);

    // --- 9. Call kernel entry. It should not normally return. ---
    KernelEntry entry = (KernelEntry)KERNEL_LOAD_ADDR;
    entry(&bi);

    // If we ever get here, the kernel actually returned.
    Print(L"[boot] Kernel returned, halting.\r\n");
    for (;;) {
        __asm__ volatile("hlt");
    }

    // Not reached
    return EFI_SUCCESS;
}
