#!/usr/bin/env bash
# SproutOS Ubuntu 环境准备 + 构建 + 运行
set -e

echo "==> 安装构建依赖 (需要 sudo)"
sudo apt update
# grub-efi-amd64-bin : 让 grub-mkrescue 自动在 ISO 中内嵌 EFI 启动镜像 (BOOTX64.EFI)
# ovmf             : QEMU UEFI 固件，供 make run-uefi 测试 UEFI 启动
sudo apt install -y build-essential nasm grub-pc-bin grub-common xorriso \
     qemu-system-x86 mtools gcc-multilib grub-efi-amd64-bin ovmf

echo "==> 编译内核并生成 ISO"
make clean
make

echo "==> 启动 SproutOS (QEMU)"
make run
