ARCH := x86_64

EFI_TARGET := BOOTX64.EFI
KERNEL_ELF := build/kernel.elf
KERNEL_BIN := build/kernel.bin

CC      := gcc
LD      := ld
OBJCOPY := objcopy

# UEFI headers
EFI_INC ?= /usr/include/efi

# These are filled in by the GitHub Actions workflow,
# but we provide sane defaults for local builds.
CRT0    ?= /usr/lib/crt0-efi-$(ARCH).o
EFI_LDS ?= /usr/lib/elf_$(ARCH)_efi.lds
EFILIB  ?= /usr/lib

CFLAGS_COMMON := -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone \
                 -Wall -Wextra -O2

EFI_CFLAGS := $(CFLAGS_COMMON) \
              -fshort-wchar \
              -I$(EFI_INC) -I$(EFI_INC)/$(ARCH) \
              -DEFI_FUNCTION_WRAPPER

KERNEL_CFLAGS := $(CFLAGS_COMMON) -m64 -Ikernel/include

LIBGCC := $(shell $(CC) $(EFI_CFLAGS) -print-libgcc-file-name 2>/dev/null)

EFI_LDFLAGS := -nostdlib -znocombreloc -T $(EFI_LDS)
EFI_LIBS    := -L$(EFILIB) -lefi -lgnuefi $(LIBGCC)

KERNEL_LDFLAGS := -nostdlib -z max-page-size=0x1000

BUILD_DIR := build
EFI_DIR   := $(BUILD_DIR)/EFI/BOOT

all: $(EFI_DIR)/$(EFI_TARGET) $(KERNEL_BIN)

# -----------------------
# UEFI bootloader
# -----------------------

$(EFI_DIR)/$(EFI_TARGET): $(BUILD_DIR)/bootloader.elf
	mkdir -p $(EFI_DIR)
	$(OBJCOPY) \
	    -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	    -j .rel -j .rela -j .reloc \
	    --target=efi-app-$(ARCH) \
	    $< $@

$(BUILD_DIR)/bootloader.elf: $(BUILD_DIR)/bootloader.o
	mkdir -p $(BUILD_DIR)
	$(LD) $(EFI_LDFLAGS) $(CRT0) $< \
	    -shared -Bsymbolic \
	    $(EFI_LIBS) \
	    -o $@

$(BUILD_DIR)/bootloader.o: uefi/main.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(EFI_CFLAGS) -c $< -o $@

# -----------------------
# Kernel
# -----------------------

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

$(KERNEL_ELF): $(BUILD_DIR)/kernel.o kernel/link.ld
	mkdir -p $(BUILD_DIR)
	$(LD) $(KERNEL_LDFLAGS) -T kernel/link.ld $< -o $@

$(BUILD_DIR)/kernel.o: kernel/core/kernel.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
