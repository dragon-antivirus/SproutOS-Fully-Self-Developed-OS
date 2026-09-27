# SproutOS - 适用于 Ubuntu 的构建脚本
#
# 安装依赖（Ubuntu）:
#   sudo apt update
#   sudo apt install -y build-essential nasm grub-pc-bin grub-common xorriso qemu-system-x86 mtools
#   # UEFI 启动支持（可选，推荐安装）:
#   sudo apt install -y grub-efi-amd64-bin ovmf
#
# 常用命令:
#   make            编译内核并生成 sproutos.iso（混合 BIOS+UEFI 镜像）
#   make run        用 QEMU 启动系统 (BIOS 模式)
#   make run-uefi   用 QEMU 启动系统 (UEFI 模式, 需 ovmf)
#   make debug      用 QEMU 启动并开启 GDB 调试端口 (BIOS)
#   make debug-uefi 用 QEMU 启动并开启 GDB (UEFI)
#   make iso        仅生成 ISO 镜像
#   make clean      清理构建产物
#
# UEFI 原理:
#   grub-mkrescue 在检测到 grub-efi-amd64-bin 已安装时，会自动在 ISO 中
#   创建 EFI 系统分区 (ESP)，内含 EFI/BOOT/BOOTX64.EFI。同一份 grub.cfg
#   同时被 BIOS 和 UEFI 引导使用。GRUB EFI 在加载 multiboot1 内核前会
#   退出 UEFI Boot Services 并切换到 32 位保护模式，内核代码无需任何改动。
#
# M5 包管理 (Seed / Planter):
#   Terra 是 SproutOS 的中文编程语言（类似易语言），编译成 .seed 包在 SeedVM 上运行。
#   编译器 terrac（python3 scripts/terrac.py <file>.terra）
#   源码: scripts/seed-examples/*.terra
#   OS 内 Planter 包管理器: planter / terra run / plant / weed / water / rain / garden / seek / sniff / prune
#   详见 docs/terra.md

AS      = nasm
CC      = gcc
LD      = ld
QEMU    = qemu-system-i386
# UEFI 模式必须用 x86_64 版 QEMU (OVMF 是 64 位固件)，但它仍然能执行
# 32 位内核代码 —— GRUB EFI 在跳转到内核前已将 CPU 切回 32 位保护模式。
QEMU_UEFI = qemu-system-x86_64
PYTHON  = python3

# disk.img 的 -drive 参数必须单独定义为变量，因为其中的逗号会被
# Makefile 的 $(if) 函数解析为参数分隔符，导致参数被截断。
DISK_DRIVE := -drive file=disk.img,format=raw,if=ide

# OVMF 固件路径 (Ubuntu 包名 ovmf)。优先找合并文件（可用 -bios 直接引导），
# 找不到再找分离的 CODE 文件（需 pflash 双盘 + VARS 副本）。
OVMF := $(shell for f in /usr/share/ovmf/OVMF.fd \
                        /usr/share/OVMF/OVMF_CODE.fd \
                        /usr/share/edk2/ovmf/OVMF_CODE.fd; do \
                  [ -f "$$f" ] && echo "$$f" && break; done)

CFLAGS  = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib \
          -Wall -Wextra -fno-builtin -c \
          -I $(SRC_DIR)/boot -I $(SRC_DIR)/kernel -I $(SRC_DIR)/mm \
          -I $(SRC_DIR)/fs -I $(SRC_DIR)/drivers -I $(SRC_DIR)/drivers/storage \
          -I $(SRC_DIR)/drivers/pci -I $(SRC_DIR)/ui -I $(SRC_DIR)/browser \
          -I $(SRC_DIR)/fonts
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T link.ld -nostdlib

SRC_DIR = .
BUILD   = build
ISO     = sproutos.iso

.DEFAULT_GOAL := all

SRCS = $(wildcard $(SRC_DIR)/kernel/*.c $(SRC_DIR)/mm/*.c $(SRC_DIR)/fs/*.c \
                   $(SRC_DIR)/drivers/*.c $(SRC_DIR)/drivers/storage/*.c \
                   $(SRC_DIR)/drivers/pci/*.c $(SRC_DIR)/ui/*.c \
                   $(SRC_DIR)/fonts/*.c) \
       $(filter-out $(SRC_DIR)/browser/browser_osdep_stub.c, \
                     $(wildcard $(SRC_DIR)/browser/*.c))
ASMS = $(SRC_DIR)/boot/boot.asm $(SRC_DIR)/boot/gdt.asm
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS)) \
       $(BUILD)/boot.o $(BUILD)/gdt_asm.o

# 跨机同步（Windows <-> Ubuntu VM）可能让源文件时间戳在未来。
# sync-build 先归一源码时间，再删除旧产物并启动一次新的 make 解析；
# 必须分成两次 make，才能保证依赖判断发生在 touch 之后。
sync:
	@find boot kernel mm fs drivers ui fonts -type f \( -name '*.c' -o -name '*.h' -o -name '*.asm' \) -exec touch {} +
	@touch -c Makefile link.ld grub.cfg

.PHONY: all raw-all run debug test iso dist clean sync sync-build run-qemu debug-qemu test-qemu ensure-disk run-uefi debug-uefi test-uefi

all: sync-build

iso: sync-build

# dist: 构建 ISO + 把壁纸打进磁盘镜像（make_disk.sh 会把
# wallpaper/background.bmp 复制到镜像的 /wallpaper/background.bmp）。
# 之后 `make run` 即可在桌面看到自定义壁纸。
dist: iso
	bash ./make_disk.sh

sync-build: sync
	$(MAKE) clean
	$(MAKE) raw-all

raw-all: $(ISO)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

# 开机动画 phase 时长常量——手写维护（ui/boot_logo_data.h），不再由脚本生成。

$(BUILD)/kernel/kernel.o: ui/boot_logo_data.h

$(BUILD)/boot.o: $(SRC_DIR)/boot/boot.asm | $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/gdt_asm.o: $(SRC_DIR)/boot/gdt.asm | $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(ISO): $(BUILD)/kernel.bin
	mkdir -p iso/boot/grub
	cp $(BUILD)/kernel.bin iso/boot/kernel.bin
	cp grub.cfg iso/boot/grub/grub.cfg
# grub-mkrescue 在 grub-efi-amd64-bin 已安装时会自动生成混合 ISO：
#   - El Torito BIOS 启动目录 (BIOS 模式)
#   - EFI 系统分区内含 EFI/BOOT/BOOTX64.EFI (UEFI 模式)
# 同一份 grub.cfg 同时被两种引导路径使用。无需额外参数。
	grub-mkrescue -o $(ISO) iso

run: sync-build
	$(MAKE) run-qemu

# ensure-disk: if disk.img is missing and we have the wallpaper BMP + the build
# script, run make_disk.sh automatically so the user never has to remember the
# separate `make disk` step. If the build fails (e.g. mtools not installed), warn
# but don't abort — QEMU still runs, just without storage (wallpaper then falls
# back to the procedural default). This is the real fix for "壁纸显色有问题":
# without disk.img mounted, ATA finds nothing and the wallpaper loader always
# uses the default. (Phony so the check runs every invocation.)
ensure-disk:
	@if { [ ! -f disk.img ] || [ make_disk.sh -nt disk.img ] || \
	      { [ -f fonts/SproutOS-YShiNewHeiGGJRegular.ttf ] && [ fonts/SproutOS-YShiNewHeiGGJRegular.ttf -nt disk.img ]; } || \
	      { [ -f wallpaper/background.bmp ] && [ wallpaper/background.bmp -nt disk.img ]; } || \
	      { [ -f browser/home.html ] && [ browser/home.html -nt disk.img ]; } || \
	      { [ -n "$$(find scripts/seed-examples -name '*.terra' -newer disk.img 2>/dev/null | head -1)" ]; } || \
	      { [ -f scripts/terrac.py ] && [ scripts/terrac.py -nt disk.img ]; } ; } \
	   && [ -f make_disk.sh ]; then \
	    echo ">>> [ensure-disk] (re)building disk.img ..."; \
	    bash ./make_disk.sh && echo ">>> [ensure-disk] disk.img built." \
	      || echo ">>> [ensure-disk] WARNING: disk build failed; booting without storage."; \
	fi

run-qemu: ensure-disk $(ISO)
	$(QEMU) -m 256 -boot d -cdrom $(ISO) $(if $(wildcard disk.img),$(DISK_DRIVE)) -serial stdio -netdev user,id=n0 -device e1000,netdev=n0 -display gtk,zoom-to-fit=off,grab-on-hover=on
debug: sync-build
	$(MAKE) debug-qemu

debug-qemu: ensure-disk $(ISO)
	$(QEMU) -m 256 -boot d -cdrom $(ISO) $(if $(wildcard disk.img),$(DISK_DRIVE)) -serial stdio -s -S -netdev user,id=n0 -device e1000,netdev=n0 -display gtk,zoom-to-fit=off,grab-on-hover=on
test: sync-build
	$(MAKE) test-qemu

test-qemu: ensure-disk $(ISO)
	$(QEMU) -m 256 -boot d -cdrom $(ISO) $(if $(wildcard disk.img),$(DISK_DRIVE)) -serial stdio -nographic -display none

# ===== UEFI 启动 (需 ovmf 包) =====
# run-uefi / debug-uefi / test-uefi 使用 x86_64 QEMU + OVMF 固件。
# OVMF 是 64 位 UEFI 固件，但 GRUB EFI 在加载 multiboot1 内核前会退出
# Boot Services 并切回 32 位保护模式，因此内核代码无需任何改动。
# -boot d 是 BIOS 专用参数，UEFI 下省略 (OVMF 自动搜索 El Torito 启动项)。

run-uefi: sync-build
	$(MAKE) run-uefi-qemu

run-uefi-qemu: ensure-disk $(ISO)
	@if [ -z "$(OVMF)" ]; then \
		echo "ERROR: OVMF 固件未找到。请安装: sudo apt install -y ovmf"; \
		exit 1; \
	fi; \
	OVMF_DIR=$(dir $(OVMF)); \
	if [ -f "$${OVMF_DIR}OVMF_VARS.fd" ]; then \
		cp -f "$${OVMF_DIR}OVMF_VARS.fd" /tmp/sproutos_ovmf_vars.fd 2>/dev/null || true; \
		echo ">>> [UEFI] pflash CODE=$(OVMF) + VARS=/tmp/sproutos_ovmf_vars.fd"; \
		$(QEMU_UEFI) -m 256 -cdrom $(ISO) \
		  $(if $(wildcard disk.img),$(DISK_DRIVE)) \
		  -drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		  -drive if=pflash,format=raw,file=/tmp/sproutos_ovmf_vars.fd \
		  -serial stdio -netdev user,id=n0 -device e1000,netdev=n0 \
		  -vga virtio \
		  -display gtk,zoom-to-fit=off,grab-on-hover=on; \
	else \
		echo ">>> [UEFI] -bios $(OVMF)"; \
		$(QEMU_UEFI) -m 256 -cdrom $(ISO) \
		  $(if $(wildcard disk.img),$(DISK_DRIVE)) \
		  -bios $(OVMF) \
		  -serial stdio -netdev user,id=n0 -device e1000,netdev=n0 \
		  -vga virtio \
		  -display gtk,zoom-to-fit=off,grab-on-hover=on; \
	fi

debug-uefi: sync-build
	$(MAKE) debug-uefi-qemu

debug-uefi-qemu: ensure-disk $(ISO)
	@if [ -z "$(OVMF)" ]; then \
		echo "ERROR: OVMF 固件未找到。请安装: sudo apt install -y ovmf"; \
		exit 1; \
	fi; \
	OVMF_DIR=$(dir $(OVMF)); \
	if [ -f "$${OVMF_DIR}OVMF_VARS.fd" ]; then \
		cp -f "$${OVMF_DIR}OVMF_VARS.fd" /tmp/sproutos_ovmf_vars.fd 2>/dev/null || true; \
		echo ">>> [UEFI-DBG] pflash CODE=$(OVMF) + VARS=/tmp/sproutos_ovmf_vars.fd"; \
		$(QEMU_UEFI) -m 256 -cdrom $(ISO) \
		  $(if $(wildcard disk.img),$(DISK_DRIVE)) \
		  -drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		  -drive if=pflash,format=raw,file=/tmp/sproutos_ovmf_vars.fd \
		  -serial stdio -s -S -netdev user,id=n0 -device e1000,netdev=n0 \
		  -display gtk,zoom-to-fit=off,grab-on-hover=on; \
	else \
		echo ">>> [UEFI-DBG] -bios $(OVMF)"; \
		$(QEMU_UEFI) -m 256 -cdrom $(ISO) \
		  $(if $(wildcard disk.img),$(DISK_DRIVE)) \
		  -bios $(OVMF) \
		  -serial stdio -s -S -netdev user,id=n0 -device e1000,netdev=n0 \
		  -display gtk,zoom-to-fit=off,grab-on-hover=on; \
	fi

test-uefi: sync-build
	$(MAKE) test-uefi-qemu

test-uefi-qemu: ensure-disk $(ISO)
	@if [ -z "$(OVMF)" ]; then \
		echo "ERROR: OVMF 固件未找到。请安装: sudo apt install -y ovmf"; \
		exit 1; \
	fi; \
	OVMF_DIR=$(dir $(OVMF)); \
	if [ -f "$${OVMF_DIR}OVMF_VARS.fd" ]; then \
		cp -f "$${OVMF_DIR}OVMF_VARS.fd" /tmp/sproutos_ovmf_vars.fd 2>/dev/null || true; \
		echo ">>> [UEFI-TEST] pflash CODE=$(OVMF) + VARS=/tmp/sproutos_ovmf_vars.fd"; \
		$(QEMU_UEFI) -m 256 -cdrom $(ISO) \
		  $(if $(wildcard disk.img),$(DISK_DRIVE)) \
		  -drive if=pflash,format=raw,readonly=on,file=$(OVMF) \
		  -drive if=pflash,format=raw,file=/tmp/sproutos_ovmf_vars.fd \
		  -serial stdio -nographic -display none \
		  -netdev user,id=n0 -device e1000,netdev=n0; \
	else \
		echo ">>> [UEFI-TEST] -bios $(OVMF)"; \
		$(QEMU_UEFI) -m 256 -cdrom $(ISO) \
		  $(if $(wildcard disk.img),$(DISK_DRIVE)) \
		  -bios $(OVMF) \
		  -serial stdio -nographic -display none \
		  -netdev user,id=n0 -device e1000,netdev=n0; \
	fi

disk:
	bash ./make_disk.sh

clean:
	rm -rf $(BUILD) iso $(ISO)
