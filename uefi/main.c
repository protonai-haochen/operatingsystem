#include <efi.h>
#include <efilib.h>
#include "../kernel/include/boot.h"

typedef void (*KernelEntry)(BootInfo *boot_info);

EFI_STATUS
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"LightOS UEFI Bootloader\n");

    EFI_STATUS status;

    //
    // 1. Locate GOP (graphics output) so we can hand framebuffer to kernel
    //
    EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;

    status = uefi_call_wrapper(
        SystemTable->BootServices->LocateProtocol,
        3,
        &gopGuid,
        NULL,
        (void**)&gop
    );
    if (EFI_ERROR(status) || gop == NULL) {
        Print(L"[BOOT] Failed to locate GOP: %r\n", status);
        return status;
    }

    UINT32 width  = gop->Mode->Info->HorizontalResolution;
    UINT32 height = gop->Mode->Info->VerticalResolution;
    UINT32 pitch  = gop->Mode->Info->PixelsPerScanLine;
    EFI_PHYSICAL_ADDRESS fb_base = gop->Mode->FrameBufferBase;

    Print(L"[BOOT] GOP: %d x %d, pitch=%d\n", width, height, pitch);

    //
    // 2. Open filesystem root
    //
    EFI_FILE_HANDLE root = LibOpenRoot(ImageHandle);
    if (!root) {
        Print(L"[BOOT] Failed to open filesystem root\n");
        return EFI_LOAD_ERROR;
    }

    //
    // 3. Open kernel.bin from the volume root
    //
    EFI_FILE_HANDLE kernelFile;
    status = uefi_call_wrapper(
        root->Open,
        5,
        root,
        &kernelFile,
        L"kernel.bin",
        EFI_FILE_MODE_READ,
        0
    );
    if (EFI_ERROR(status)) {
        Print(L"[BOOT] Failed to open kernel.bin: %r\n", status);
        return status;
    }

    //
    // 4. Get kernel file size
    //
    EFI_FILE_INFO *info = LibFileInfo(kernelFile);
    if (!info) {
        Print(L"[BOOT] LibFileInfo failed\n");
        kernelFile->Close(kernelFile);
        return EFI_LOAD_ERROR;
    }

    UINTN kernelSize = info->FileSize;
    Print(L"[BOOT] kernel.bin size: %d bytes\n", (UINT32)kernelSize);
    FreePool(info);

    //
    // 5. Allocate memory for kernel at 1 MiB (0x100000)
    //
    EFI_PHYSICAL_ADDRESS kernel_addr = 0x100000;
    UINTN pages = (kernelSize + 0xFFF) / 0x1000;

    status = uefi_call_wrapper(
        SystemTable->BootServices->AllocatePages,
        4,
        AllocateAddress,
        EfiLoaderData,
        pages,
        &kernel_addr
    );
    if (EFI_ERROR(status)) {
        Print(L"[BOOT] AllocatePages failed: %r\n", status);
        kernelFile->Close(kernelFile);
        return status;
    }

    //
    // 6. Read kernel into memory
    //
    UINTN readSize = kernelSize;
    status = uefi_call_wrapper(
        kernelFile->Read,
        3,
        kernelFile,
        &readSize,
        (void*)kernel_addr
    );
    kernelFile->Close(kernelFile);

    if (EFI_ERROR(status) || readSize != kernelSize) {
        Print(L"[BOOT] Read kernel.bin failed: %r (read %d bytes)\n",
              status, (UINT32)readSize);
        return status;
    }

    Print(L"[BOOT] Kernel loaded at 0x%x (%d pages)\n",
          (UINTN)kernel_addr, (UINT32)pages);

    //
    // 7. Build BootInfo struct for the kernel
    //
    BootInfo boot;
    boot.framebuffer_base   = (uint64_t)fb_base;
    boot.framebuffer_width  = width;
    boot.framebuffer_height = height;
    boot.framebuffer_pitch  = pitch;

    //
    // 8. Get memory map, then ExitBootServices (UEFI is gone after this)
    //
    UINTN mmapSize = 0;
    EFI_MEMORY_DESCRIPTOR *mmap = NULL;
    UINTN mapKey;
    UINTN descSize;
    UINT32 descVersion;

    status = uefi_call_wrapper(
        SystemTable->BootServices->GetMemoryMap,
        5,
        &mmapSize,
        mmap,
        &mapKey,
        &descSize,
        &descVersion
    );

    if (status == EFI_BUFFER_TOO_SMALL) {
        mmapSize += descSize * 2;
        status = uefi_call_wrapper(
            SystemTable->BootServices->AllocatePool,
            3,
            EfiLoaderData,
            mmapSize,
            (void**)&mmap
        );
        if (EFI_ERROR(status)) {
            Print(L"[BOOT] AllocatePool for memory map failed: %r\n", status);
            return status;
        }

        status = uefi_call_wrapper(
            SystemTable->BootServices->GetMemoryMap,
            5,
            &mmapSize,
            mmap,
            &mapKey,
            &descSize,
            &descVersion
        );
    }

    if (EFI_ERROR(status)) {
        Print(L"[BOOT] GetMemoryMap failed: %r\n", status);
        return status;
    }

    status = uefi_call_wrapper(
        SystemTable->BootServices->ExitBootServices,
        2,
        ImageHandle,
        mapKey
    );
    if (EFI_ERROR(status)) {
        // Proper retry logic is a bit more work; for now we just bail.
        return status;
    }

    //
    // 9. Jump to kernel entry point at 1 MiB
    //
    KernelEntry entry = (KernelEntry)( (UINTN)kernel_addr );
    entry(&boot);

    // Should never return
    while (1) { }

    return EFI_SUCCESS;
}
