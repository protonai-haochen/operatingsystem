ARCH := x86_64
EFI_TARGET := BOOTX64.EFI

CC := gcc
LD := ld

CFLAGS := -I/usr/include/efi -I/usr/include/efi/$(ARCH) \
          -fno-stack-protector -fpic -fshort-wchar -mno-red-zone \
          -DEFI_FUNCTION_WRAPPER -Wall -Wextra -O2

LDFLAGS := -nostdlib -znocombreloc \
           /usr/lib/crt0-efi-$(ARCH).o \
           -L/usr/lib -lefi -lgnuefi

BUILD_DIR := build
EFI_DIR := $(BUILD_DIR)/EFI/BOOT

all: $(EFI_DIR)/$(EFI_TARGET)

$(EFI_DIR)/$(EFI_TARGET): uefi/main.c
	mkdir -p $(BUILD_DIR) $(EFI_DIR)
	$(CC) $(CFLAGS) -c $< -o $(BUILD_DIR)/main.o
	$(LD) -nostdlib -znocombreloc \
	  /usr/lib/crt0-efi-x86_64.o \
	  $(BUILD_DIR)/main.o \
	  -L/usr/lib \
	  -lefi -lgnuefi \
	  -o $(BUILD_DIR)/main.so
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym \
	        -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) \
	        $(BUILD_DIR)/main.so $(EFI_DIR)/$(EFI_TARGET)
	cp $(EFI_DIR)/$(EFI_TARGET) $(BUILD_DIR)/$(EFI_TARGET)


clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean

