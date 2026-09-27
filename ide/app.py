#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SproutOS 原生 IDE 入口（PyQt6，真正运行在本机桌面）。"""

import os
import sys

from PyQt6.QtWidgets import QApplication
from PyQt6.QtGui import QFont, QIcon

# 保证导入同目录模块
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from main_window import MainWindow  # noqa: E402


def main():
    app = QApplication(sys.argv)
    # 中文字体：Windows 用微软雅黑，Ubuntu 等回退到 CJK 字体，保证菜单/对话框中文清晰
    f = QFont()
    f.setPointSize(9)
    f.setFamilies(["Microsoft YaHei", "Noto Sans CJK SC",
                   "WenQuanYi Micro Hei", "PingFang SC", "Sans Serif"])
    app.setFont(f)

    # 应用图标（任务栏 + 窗口标题栏）
    icon_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon.png")
    if not os.path.isfile(icon_path) and getattr(sys, "frozen", False):
        icon_path = os.path.join(sys._MEIPASS, "icon.png")
    if os.path.isfile(icon_path):
        app.setWindowIcon(QIcon(icon_path))

    win = MainWindow()
    win.show()

    # 处理命令行参数：双击 .terra 文件时，操作系统会把文件路径作为参数传入
    if len(sys.argv) > 1:
        file_path = sys.argv[1]
        if os.path.isfile(file_path):
            win.open_file(file_path)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
