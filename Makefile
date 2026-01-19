ARCH := x86_64
EFI_TARGET := BOOTX64.EFI

CC      := gcc
LD      := ld
OBJCOPY := objcopy

BUILD_DIR := build
EFI_DIR   := $(BUILD_DIR)/EFI/BOOT

# These may be overridden by CI (GitHub Actions)
CRT0    ?= /usr/lib/crt0-efi-$(ARCH).o
EFI_LDS ?= /usr/lib/elf_$(ARCH)_efi.lds
EFILIB  ?= /usr/lib

# ========================= UEFI BOOTLOADER =========================

UEFI_CFLAGS := -I/usr/include/efi -I/usr/include/efi/$(ARCH) \
               -Ikernel/include \
               -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
               -DEFI_FUNCTION_WRAPPER -Wall -Wextra -O2

UEFI_LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS)
UEFI_LIBS    := -L$(EFILIB) -lefi -lgnuefi

# ============================== KERNEL ==============================

KERNEL_CFLAGS := -ffreestanding -fno-stack-protector -mno-red-zone -m64 \
                 -Wall -Wextra -O2 -Ikernel/include

KERNEL_LDFLAGS := -nostdlib -z max-page-size=0x1000 -T kernel/link.ld

# ============================ TOP LEVEL =============================

all: $(EFI_DIR)/$(EFI_TARGET) $(BUILD_DIR)/kernel.bin

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(EFI_DIR): | $(BUILD_DIR)
	mkdir -p $(EFI_DIR)

# UEFI bootloader build

$(BUILD_DIR)/bootloader.o: uefi/main.c | $(BUILD_DIR)
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(BUILD_DIR)/bootloader.elf: $(BUILD_DIR)/bootloader.o
	$(LD) $(UEFI_LDFLAGS) $(CRT0) $< \
	    -shared -Bsymbolic \
	    $(UEFI_LIBS) \
	    -o $@

$(EFI_DIR)/$(EFI_TARGET): $(BUILD_DIR)/bootloader.elf | $(EFI_DIR)
	$(OBJCOPY) \
	    -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	    -j .rel -j .rela -j .reloc \
	    --target=efi-app-$(ARCH) \
	    $< $@

# Kernel build

$(BUILD_DIR)/kernel.o: kernel/core/kernel.c | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.elf: $(BUILD_DIR)/kernel.o kernel/link.ld
	$(LD) $(KERNEL_LDFLAGS) -o $@ $<

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
