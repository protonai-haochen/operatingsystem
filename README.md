# LightOS Prototype

A minimal, secure 64-bit operating system prototype with modular "Blocks".

## Features
- UEFI boot (direct, no GRUB)
- Stripped Linux kernel (<5MB)
- Security-first design (noexec mounts, sandboxing)
- Modular Block system (File, Internet, Command, etc.)
- .core package format (WebAssembly-based)

## Quick Start

### GitHub Codespaces:
1. Click "Code" → "Codespaces" → "New codespace"
2. Wait for environment setup (2-3 minutes)
3. Run: `make all`
4. Run: `make run`

### Local (Ubuntu):
```bash
sudo apt install build-essential nasm qemu-system-x86 make git
make all
make run
