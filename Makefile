ARCH := x86_64
EFI_TARGET := BOOTX64.EFI

CC := gcc
LD := ld
OBJCOPY := objcopy

BUILD_DIR := build
EFI_DIR := $(BUILD_DIR)/EFI/BOOT

# ----------------------------------------------------------------------
# UEFI bootloader build
# ----------------------------------------------------------------------

UEFI_CFLAGS := -I/usr/include/efi -I/usr/include/efi/$(ARCH) \
               -Ikernel/include \
               -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
               -DEFI_FUNCTION_WRAPPER -Wall -Wextra -O2

UEFI_LDFLAGS := -nostdlib -znocombreloc \
                /usr/lib/crt0-efi-$(ARCH).o \
                -L/usr/lib -lefi -lgnuefi

UEFI_OBJ := $(BUILD_DIR)/bootloader.o

# ----------------------------------------------------------------------
# Kernel build
# ----------------------------------------------------------------------

KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
KERNEL_OBJS := $(BUILD_DIR)/kernel.o
KERNEL_LDS := kernel/link.ld

KERNEL_CFLAGS := -ffreestanding -fno-stack-protector -mno-red-zone -m64 \
                 -Wall -Wextra -O2 -Ikernel/include

KERNEL_LDFLAGS := -nostdlib -z max-page-size=0x1000 -T $(KERNEL_LDS)

# ----------------------------------------------------------------------
# Targets
# ----------------------------------------------------------------------

all: $(EFI_DIR)/$(EFI_TARGET) $(KERNEL_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(EFI_DIR): | $(BUILD_DIR)
	mkdir -p $(EFI_DIR)

# --- UEFI bootloader (.efi) ---

$(UEFI_OBJ): uefi/main.c | $(BUILD_DIR)
	$(CC) $(UEFI_CFLAGS) -c $< -o $@

$(EFI_DIR)/$(EFI_TARGET): $(UEFI_OBJ) | $(EFI_DIR)
	$(LD) $(UEFI_OBJ) $(UEFI_LDFLAGS) -o $(BUILD_DIR)/bootloader.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	           -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) \
	           $(BUILD_DIR)/bootloader.so $(EFI_DIR)/$(EFI_TARGET)
	cp $(EFI_DIR)/$(EFI_TARGET) $(BUILD_DIR)/$(EFI_TARGET)

# --- Kernel binary ---

$(KERNEL_OBJS): kernel/core/kernel.c | $(BUILD_DIR)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD) $(KERNEL_LDFLAGS) -o $@ $^

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
