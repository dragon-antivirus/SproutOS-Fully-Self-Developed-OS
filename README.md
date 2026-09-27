# SproutOS · 一个完全自主研发的操作系统

> SproutOS — A Fully Self-Developed Operating System

SproutOS 是一个**从零开始、完全自主研发**的 x86 操作系统：拥有自研内核、图形界面（GUI）、内存管理、文件系统、网络栈，以及一套用于编写并运行应用程序的自研编程语言 **Terra**。

SproutOS is a from-scratch, fully self-developed x86 operating system: it ships its own
kernel, GUI, memory manager, filesystem, network stack, and a self-designed programming
language called **Terra** for building and running native apps.

---

## ✨ 主要特性 / Features

- 🖥️ **BIOS / UEFI 双启动**：统一 Multiboot1 头，GRUB 引导，SeaBIOS 与 OVMF 均可启动。
- 🧠 **内存管理**：物理内存分配（PMM）+ 分页虚拟内存（VMM，含 NOCACHE 帧缓冲映射）。
- 📦 **包管理模块**：可安装并运行由 Terra 语言编译出的 `.seed` 应用程序。
- 🌐 **网络**：内置 **QEMU（e1000）与 VMware（rtl8139）** 网卡驱动，以及用于网络收发的协议栈。
- 🌍 **浏览器**：内核内集成可浏览 **HTTP** 的简易浏览器。
- 💾 **FAT32 支持**：可加载一块 FAT32 软盘 / 磁盘镜像，读写文件。
- 🎨 **图形界面**：VGA 帧缓冲桌面、文件管理器、编辑器、终端、画图、关于等应用。
- 🛡️ **自研语言 Terra**：中文关键字编程语言，编译为 SeedVM 字节码（`.seed`）。

---

## 📁 仓库结构 / Repository Layout

```
SproutOS/            # 操作系统源码（内核 + 驱动 + GUI + 应用）
  boot/              # 引导（Multiboot 头、GDT）
  kernel/            # 内核：启动、内存、调度、SeedVM
  mm/                # 物理/虚拟内存管理
  fs/                # FAT32 / VFS
  drivers/           # 设备驱动（PCI、ATA、串口、键盘、鼠标、网卡…）
  ui/                # 图形界面（VGA、GUI、字体）
  browser/           # 内核内 HTTP 浏览器
  fonts/             # 点阵 + TrueType 字体
  scripts/           # Terra 编译器(terrac.py) + SeedVM 模拟器 + 示例
    seed-examples/   # .terra 源码 与 编译好的 .seed 示例
  tools/ wallpaper/ docs/
  Makefile grub.cfg link.ld make_disk.sh setup.sh VERSION

ide/                 # 原生桌面 IDE（PyQt6，真正运行在本机）
  app.py main_window.py editor.py highlighter.py patterns.py backend.py
  build_exe.bat      # 打包为单文件 SproutIDE.exe
  run.sh             # Ubuntu 启动脚本
```

---

## 🔧 构建与运行 / Build & Run

> 需要在 **Ubuntu / Linux** 环境下构建（依赖 `nasm`、`grub-pc-bin`、`xorriso`、
> `qemu-system-x86`、`mtools`）。

```bash
cd SproutOS
make iso            # 生成 sproutos.iso
make run            # BIOS 启动 (SeaBIOS + QEMU)
make run-uefi       # UEFI 启动 (OVMF + QEMU)
make disk           # 生成 disk.img（FAT32，含示例 .seed）
```

在 QEMU 里用 `F5` 之类的方式运行已部署的 `.seed` 应用，或在文件管理器 / 应用花园中打开。

---

## 💻 原生 IDE（ide/）

`ide/` 是一个 **真正运行在本机桌面** 的 VSCode 风格 IDE（PyQt6），用于编写 `.terra`
程序、一键编译为 `.seed`、在主机用 SeedVM 模拟运行，并可直接部署到 `disk.img`
供真机 SproutOS 测试。

- Windows：先 `pip install pyqt6 pyinstaller`，再双击 `build_exe.bat` 生成
  `dist/SproutIDE.exe`（单文件，无需 Python 环境即可运行）。
- Ubuntu：`bash run.sh`（首次会自动安装 PyQt6）。

---

## 🌱 Terra 编程语言

Terra 是一套中文关键字的编程语言，例：`输出 "你好"`、`如果 … 则 …`、`循环 … 次`。
源码经 `scripts/terrac.py` 编译为 SeedVM 字节码（88 字节头 + 代码段 + 数据段），
由内核内的 SeedVM 或主机 `ide/backend.py` 中的模拟器执行。示例见
`SproutOS/scripts/seed-examples/`（含 `hello.terra`、`calc.terra`、`draw.terra` 等，
以及已编译的 `*.seed`）。

---

## 📜 许可证 / License

本项目以 **MIT License** 开源。参见仓库内的 `LICENSE` 文件。

---

© SproutOS · 完全自主研发的操作系统
