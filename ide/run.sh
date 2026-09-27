#!/usr/bin/env bash
# ============================================================
#  SproutOS IDE —— Ubuntu / Linux 启动器
#  首次运行自动用 pip 安装 PyQt6（仅需一次）。
#  用法：  bash run.sh   或   chmod +x run.sh && ./run.sh
# ============================================================
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

if ! python3 -c "import PyQt6" >/dev/null 2>&1; then
    echo "[SproutIDE] 未检测到 PyQt6，正在安装..."
    if command -v pip3 >/dev/null 2>&1; then
        pip3 install --user PyQt6 2>/dev/null \
          || sudo pip3 install PyQt6 \
          || { echo "pip 安装失败，请改用系统包：sudo apt install python3-pyqt6"; exit 1; }
    else
        echo "未找到 pip3。请二选一："
        echo "  sudo apt install python3-pip  然后重跑本脚本"
        echo "  sudo apt install python3-pyqt6"
        exit 1
    fi
fi

# 中文字体（Ubuntu 一般已带 Noto/WenQuanYi；若中文显示为方块可装 fonts-wqy-zenhei）
exec python3 app.py "$@"
