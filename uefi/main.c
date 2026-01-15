#include <efi.h>
#include <efilib.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);

    Print(L"LightOS booted (UEFI app) ✅\r\n");

    SystemTable->BootServices->Stall(2 * 1000 * 1000);
    return EFI_SUCCESS;
}
