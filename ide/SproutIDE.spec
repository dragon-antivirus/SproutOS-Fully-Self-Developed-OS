# -*- mode: python ; coding: utf-8 -*-
r"""SproutIDE PyInstaller spec —— 单文件 exe，双击直接启动，无需 Python 环境。

关键点：
  - 入口用 ide/app.py（包内 import 同目录模块）
  - scripts/ 必须显式收集全部 .py 进 datas，否则 frozen 模式下
    backend._load_module 会找不到 terrac.py / seedvm_test.py
  - 一切路径用 os.path.abspath()，避免 cmd 下相对路径反斜杠被吞
"""

import os
import sys
from glob import glob

# ---- 关键：spec 在 ide/ 下执行，IDE_DIR = ide/，SCRIPTS_DIR = ../scripts ----
IDE_DIR    = os.path.abspath(os.path.dirname(os.path.abspath(SPEC)))  # SPEC = this file
APP_DIR    = IDE_DIR
REPO_DIR   = os.path.dirname(IDE_DIR)                                  # SproutOS 仓库根
SCRIPTS_DIR = os.path.join(REPO_DIR, "SproutOS", "scripts")
SEED_EX_DIR = os.path.join(SCRIPTS_DIR, "seed-examples")

# 收集 scripts/ 下所有 .py 作为数据（编译+运行后端）
datas = []
for src in glob(os.path.join(SCRIPTS_DIR, "*.py")):
    datas.append((src, "scripts"))
for src in glob(os.path.join(SEED_EX_DIR, "*.terra")):
    datas.append((src, "scripts/seed-examples"))

# 收集图标
icon_png = os.path.join(IDE_DIR, "icon.png")
icon_ico = os.path.join(IDE_DIR, "icon.ico")
if os.path.isfile(icon_png):
    datas.append((icon_png, "."))
if os.path.isfile(icon_ico):
    datas.append((icon_ico, "."))

# 入口
entry = os.path.join(IDE_DIR, "app.py")

a = Analysis(
    [entry],
    pathex=[IDE_DIR],
    binaries=[],
    datas=datas,
    hiddenimports=[
        "backend", "editor", "highlighter", "patterns", "main_window",
        "PyQt6.QtCore", "PyQt6.QtGui", "PyQt6.QtWidgets",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=None,
    noarchive=False,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="SproutIDE",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,              # 双击启动：不弹黑窗
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=icon_ico if os.path.isfile(icon_ico) else None,
)