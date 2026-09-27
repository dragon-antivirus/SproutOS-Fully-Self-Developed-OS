#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""SproutOS 原生 IDE —— 主窗口（易语言 PRO 风格浅色界面）。"""

import os
import io
import re
import json
import shutil
import subprocess
import contextlib
import webbrowser

from PyQt6.QtWidgets import (
    QMainWindow, QApplication, QWidget, QTabWidget, QDockWidget, QTreeView,
    QListWidget, QListWidgetItem, QPlainTextEdit, QTableWidget,
    QTableWidgetItem, QVBoxLayout, QHBoxLayout, QFrame, QMenuBar, QMenu, QToolBar,
    QStatusBar, QPushButton, QLabel, QMessageBox, QInputDialog, QFileDialog,
    QLineEdit, QSizePolicy, QCompleter,
)
from PyQt6.QtCore import Qt, QDir, QModelIndex, QSize
from PyQt6.QtGui import QFont, QAction, QKeySequence, QColor, QTextCursor, QFileSystemModel, QIcon

import backend
from editor import CodeEditor

APP_TITLE = "SproutOS IDE — Terra 开发平台"

# === Terra 自动补全关键词列表 ===
TERRA_COMPLETIONS = [
    # 流程控制
    '如果', '否则', '如果结束',
    '计数循环', '判断循环', '标准循环', '循环结束', '跳出循环',
    '新建变量',
    # 输出与交互
    '输出', '信息框', '延时',
    '支持库引入', '交互_输出', '交互_取终端反馈',
    # 类型转换
    '到文本', '到整数',
    # 数学函数
    '取随机数', '绝对值', '求平方根',
    # 字符串函数
    '取文本长度', '到大写', '到小写',
    # 绘图
    '画矩形', '画文字', '画圆', '画线', '画点',
    '清屏', '刷新', '取宽', '取高',
    # 逻辑与比较
    '且', '或', '非', '真', '假',
    '小于', '大于', '等于', '不等于', '小于等于', '大于等于',
    # 颜色
    '黑色', '白色', '红色', '绿色', '蓝色', '黄色', '青色', '紫色',
]

# === 语法速查文本（右侧面板 + 帮助对话框共用）===
SYNTAX_REFERENCE = """═══ Terra 语言语法速查 ═══

【变量】
  新建变量 名              声明变量（默认值 0）
  名 = 值                  变量赋值

【输出】
  输出(内容)               打印到屏幕，支持多参数用逗号分隔
  信息框(内容)             弹出提示框
  延时(毫秒)               暂停指定毫秒数

【条件分支】
  如果 (条件)
      ...
  否则
      ...
  如果结束

【循环】
  计数循环 (次数, 变量)    变量从 1 到 次数
      ...
  循环结束 ()

  判断循环 (条件)          每轮重新判断条件
      ...
  循环结束 ()

  标准循环 (间隔毫秒, 变量) 无限循环，带间隔
      ...
  循环结束 ()

  跳出循环 ()              跳出当前最内层循环（三种循环通用）

【类型转换】
  到文本(数)               数字转文本
  到整数(文本)             文本转整数

【数学函数】
  取随机数(最小 到 最大)   生成范围内随机整数
  绝对值(数)               取绝对值
  求平方根(数)             求平方根（取整）

【字符串函数】
  取文本长度(文本)         取字符串长度
  到大写(文本)             转大写
  到小写(文本)             转小写

【运算符】
  算术: +  -  *  /  %
  比较: <  >  =  !=  <=  >=
        小于  大于  等于  不等于  小于等于  大于等于
  逻辑: 且  或  非
  常量: 真  假

【绘图】
  画矩形(x,y,w,h,颜色)    画填充矩形
  画文字(x,y,文本,颜色)   画文字
  画圆(x,y,r,颜色)         画填充圆
  画线(x1,y1,x2,y2,颜色)  画线
  画点(x,y,颜色)           画点
  清屏()                   清屏
  刷新()                   刷新画面
  取宽() / 取高()          取屏幕宽/高

【颜色常量】
  黑色  白色  红色  绿色  蓝色  黄色  青色  紫色

【交互支持库】
  支持库引入(交互支持库1.0)
  交互_输出(内容, 文本|数字)   输出并等待用户回答
  交互_取终端反馈()             取回最近一次交互回答

【注释】
  // 单行注释

【全角符号支持】
  （），""、、等中文标点自动转半角，写代码不用切输入法
"""

# === 主题样式表（浅色 / 暗色）===
THEMES = {
    'light': {
        'global': (
            "QMainWindow{background:#FFFFFF;} "
            "QDockWidget{titlebar-close-icon:none;titlebar-normal-icon:none;} "
            "QDockWidget::title{background:#F0F0F0;color:#333333;padding:4px 8px;"
            "border-bottom:1px solid #E0E0E0;} "
            "QToolTip{background:#FFFFE1;color:#333333;border:1px solid #C0C0C0;padding:2px;}"
        ),
        'tabs': (
            "QTabWidget::pane{border:0;border-top:1px solid #E0E0E0;} "
            "QTabBar::tab{background:#F0F0F0;color:#666666;padding:6px 16px;"
            "border-top:2px solid transparent;border-right:1px solid #E0E0E0;} "
            "QTabBar::tab:selected{background:#FFFFFF;color:#1E1E1E;"
            "border-top:2px solid #007ACC;}"
        ),
        'side_tabs': (
            "QTabWidget::pane{border:0;} "
            "QTabBar::tab{background:#F5F5F5;color:#666666;padding:4px 12px;"
            "border:1px solid #E0E0E0;border-bottom:none;} "
            "QTabBar::tab:selected{color:#1E1E1E;background:#FFFFFF;}"
        ),
        'tree': (
            "QTreeView{background:#FFFFFF;color:#333333;border:0;outline:none;} "
            "QTreeView::item:selected{background:#E4F0FF;color:#1E1E1E;} "
            "QTreeView::item:hover{background:#F0F7FF;}"
        ),
        'ex_list': (
            "QListWidget{background:#FFFFFF;color:#333333;border:0;outline:none;} "
            "QListWidget::item:selected{background:#E4F0FF;color:#1E1E1E;} "
            "QListWidget::item:hover{background:#F0F7FF;}"
        ),
        'bottom_tabs': (
            "QTabWidget::pane{border:0;border-top:1px solid #E0E0E0;} "
            "QTabBar::tab{background:#F5F5F5;color:#666666;padding:4px 14px;"
            "border:1px solid #E0E0E0;border-bottom:none;} "
            "QTabBar::tab:selected{color:#1E1E1E;background:#FFFFFF;}"
        ),
        'out': "QPlainTextEdit{background:#FFFFFF;color:#333333;border:0;}",
        'problems': (
            "QListWidget{background:#FFFFFF;color:#333333;border:0;outline:none;} "
            "QListWidget::item:selected{background:#E4F0FF;color:#1E1E1E;}"
        ),
        'debug': "QPlainTextEdit{background:#FFFFFF;color:#0066B3;border:0;}",
        'watch': (
            "QTableWidget{background:#FFFFFF;color:#333333;gridline-color:#E0E0E0;outline:none;} "
            "QTableWidget::item:selected{background:#E4F0FF;color:#1E1E1E;} "
            "QHeaderView::section{background:#F5F5F5;color:#333333;"
            "border:1px solid #E0E0E0;padding:4px;}"
        ),
        'menubar': (
            "QMenuBar{background:#FAFAFA;color:#333333;border-bottom:1px solid #E0E0E0;} "
            "QMenuBar::item:selected{background:#E4F0FF;} "
            "QMenu{background:#FFFFFF;color:#333333;border:1px solid #D0D0D0;} "
            "QMenu::item:selected{background:#E4F0FF;color:#1E1E1E;} "
            "QMenu::separator{height:1px;background:#E0E0E0;margin:4px 8px;}"
        ),
        'toolbar': (
            "QToolBar{background:#FAFAFA;border:0;border-bottom:1px solid #E0E0E0;"
            "spacing:2px;padding:2px;} "
            "QToolButton{background:transparent;color:#333333;padding:4px 10px;border-radius:2px;} "
            "QToolButton:hover{background:#E4F0FF;} "
            "QToolButton:pressed{background:#D0E4FF;} "
            "QToolButton:disabled{color:#AAAAAA;}"
        ),
        'find_box': (
            "QLineEdit{background:#FFFFFF;color:#333333;border:1px solid #D0D0D0;"
            "border-radius:2px;padding:3px 8px;} "
            "QLineEdit:focus{border:1px solid #007ACC;}"
        ),
        'status': (
            "QStatusBar{background:#F0F0F0;color:#333333;border-top:1px solid #E0E0E0;} "
            "QStatusBar::item{border:none;}"
        ),
    },
    'dark': {
        'global': (
            "QMainWindow{background:#1E1E1E;} "
            "QDockWidget{titlebar-close-icon:none;titlebar-normal-icon:none;} "
            "QDockWidget::title{background:#252526;color:#CCCCCC;padding:4px 8px;"
            "border-bottom:1px solid #333333;} "
            "QToolTip{background:#252526;color:#CCCCCC;border:1px solid #444444;padding:2px;}"
        ),
        'tabs': (
            "QTabWidget::pane{border:0;} "
            "QTabBar::tab{background:#2D2D2D;color:#969696;padding:6px 16px;"
            "border-top:2px solid transparent;} "
            "QTabBar::tab:selected{background:#1E1E1E;color:#FFFFFF;"
            "border-top:2px solid #569CD6;}"
        ),
        'side_tabs': (
            "QTabWidget::pane{border:0;} "
            "QTabBar::tab{background:#252526;color:#969696;padding:4px 12px;} "
            "QTabBar::tab:selected{color:#FFFFFF;background:#1E1E1E;}"
        ),
        'tree': (
            "QTreeView{background:#1E1E1E;color:#CCCCCC;border:0;outline:none;} "
            "QTreeView::item:selected{background:#094771;color:#FFFFFF;}"
        ),
        'ex_list': (
            "QListWidget{background:#1E1E1E;color:#CCCCCC;border:0;outline:none;} "
            "QListWidget::item:selected{background:#094771;color:#FFFFFF;}"
        ),
        'bottom_tabs': (
            "QTabWidget::pane{border:0;} "
            "QTabBar::tab{background:#252526;color:#969696;padding:4px 14px;} "
            "QTabBar::tab:selected{color:#FFFFFF;background:#1E1E1E;}"
        ),
        'out': "QPlainTextEdit{background:#1E1E1E;color:#D4D4D4;border:0;}",
        'problems': (
            "QListWidget{background:#1E1E1E;color:#CCCCCC;border:0;outline:none;} "
            "QListWidget::item:selected{background:#094771;color:#FFFFFF;}"
        ),
        'debug': "QPlainTextEdit{background:#1E1E1E;color:#9CDCFE;border:0;}",
        'watch': (
            "QTableWidget{background:#1E1E1E;color:#CCCCCC;gridline-color:#333333;outline:none;} "
            "QTableWidget::item:selected{background:#094771;color:#FFFFFF;} "
            "QHeaderView::section{background:#252526;color:#CCCCCC;"
            "border:1px solid #333333;padding:4px;}"
        ),
        'menubar': (
            "QMenuBar{background:#1E1E1E;color:#CCCCCC;} "
            "QMenuBar::item:selected{background:#094771;} "
            "QMenu{background:#1E1E1E;color:#CCCCCC;border:1px solid #444444;} "
            "QMenu::item:selected{background:#094771;color:#FFFFFF;} "
            "QMenu::separator{height:1px;background:#333333;margin:4px 8px;}"
        ),
        'toolbar': (
            "QToolBar{background:#2D2D30;border:0;spacing:2px;padding:2px;} "
            "QToolButton{background:transparent;color:#CCCCCC;padding:4px 10px;border-radius:2px;} "
            "QToolButton:hover{background:#3E3E40;} "
            "QToolButton:pressed{background:#4A4A4A;} "
            "QToolButton:disabled{color:#666666;}"
        ),
        'find_box': (
            "QLineEdit{background:#1E1E1E;color:#CCCCCC;border:1px solid #444444;"
            "border-radius:2px;padding:3px 8px;} "
            "QLineEdit:focus{border:1px solid #569CD6;}"
        ),
        'status': (
            "QStatusBar{background:#007ACC;color:#FFFFFF;} "
            "QStatusBar::item{border:none;}"
        ),
    },
}


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(APP_TITLE)
        # 设置窗口图标（兼容源码运行和 PyInstaller 打包）
        icon_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icon.png")
        if not os.path.isfile(icon_path) and getattr(sys, "frozen", False):
            icon_path = os.path.join(sys._MEIPASS, "icon.png")
        if os.path.isfile(icon_path):
            self.setWindowIcon(QIcon(icon_path))
        self.resize(1180, 760)

        self._running = False
        self._stop_requested = False
        self._run_session = None
        self._qemu_proc = None

        self._cfg = self._load_cfg()
        self._disk_img = self._cfg.get("disk_img")
        self._theme = self._cfg.get("theme", "light")
        # 读取自定义工作空间
        _ws = self._cfg.get("workspace")
        if _ws and os.path.isdir(_ws):
            backend.set_workspace(_ws)
        if self._theme not in THEMES:
            self._theme = "light"

        self._build_central()
        self._build_sidebar()
        self._build_right_panel()
        self._build_bottom()
        self._build_menu()
        self._build_toolbar()
        self._build_status()

        # 应用主题（统一设置所有控件样式）
        self.apply_theme()

        # 启动时打开一个空白 Terra 文件
        self.new_file()

    # ===================== 中央编辑区 =====================
    def _build_central(self):
        self._tabs = QTabWidget()
        self._tabs.setTabsClosable(True)
        self._tabs.setMovable(True)
        self._tabs.tabCloseRequested.connect(self._close_tab)
        self._tabs.currentChanged.connect(self._on_tab_change)
        self._tabs.setStyleSheet(
            "QTabWidget::pane{border:0;border-top:1px solid #E0E0E0;} "
            "QTabBar::tab{background:#F0F0F0;color:#666666;padding:6px 16px;"
            "border-top:2px solid transparent;border-right:1px solid #E0E0E0;} "
            "QTabBar::tab:selected{background:#FFFFFF;color:#1E1E1E;"
            "border-top:2px solid #007ACC;}"
        )
        self.setCentralWidget(self._tabs)

    def _add_tab(self, editor, title):
        idx = self._tabs.addTab(editor, title)
        self._tabs.setCurrentIndex(idx)
        editor.textChanged.connect(lambda: self._mark_current_dirty())
        return idx

    def _create_editor(self, language="terra"):
        """创建带主题和自动补全的代码编辑器"""
        ed = CodeEditor(language=language)
        ed.set_theme(self._theme)
        completer = QCompleter(TERRA_COMPLETIONS, self)
        ed.set_completer(completer)
        return ed

    def _current_editor(self):
        w = self._tabs.currentWidget()
        return w if isinstance(w, CodeEditor) else None

    def _mark_current_dirty(self):
        idx = self._tabs.currentIndex()
        if idx < 0:
            return
        text = self._tabs.tabText(idx)
        if not text.endswith("*"):
            self._tabs.setTabText(idx, text + "*")

    def _mark_current_clean(self, title):
        idx = self._tabs.currentIndex()
        if idx >= 0:
            base = title if not title.endswith("*") else title[:-1]
            self._tabs.setTabText(idx, base)

    def _on_tab_change(self, idx):
        ed = self._current_editor()
        if ed is None:
            self._set_status("")
            return
        lang = backend.detect_language(ed.file_path or "x.terra")
        ed.set_language(lang)
        self._update_cursor_status(ed)
        self._set_status(f"语言: {lang.upper()}    就绪")

    # ===================== 侧边栏 =====================
    def _build_sidebar(self):
        self._side = QTabWidget()
        self._side.setTabPosition(QTabWidget.TabPosition.South)
        self._side.setStyleSheet(
            "QTabWidget::pane{border:0;} "
            "QTabBar::tab{background:#F5F5F5;color:#666666;padding:4px 12px;"
            "border:1px solid #E0E0E0;border-bottom:none;} "
            "QTabBar::tab:selected{color:#1E1E1E;background:#FFFFFF;}"
        )

        # 资源管理器
        self._fs_model = QFileSystemModel()
        self._fs_model.setRootPath(backend.WORKSPACE)
        self._tree = QTreeView()
        self._tree.setModel(self._fs_model)
        self._tree.setRootIndex(self._fs_model.index(backend.WORKSPACE))
        self._tree.doubleClicked.connect(self._on_tree_open)
        self._tree.setHeaderHidden(True)
        self._tree.setColumnHidden(1, True)
        self._tree.setColumnHidden(2, True)
        self._tree.setColumnHidden(3, True)
        self._tree.setStyleSheet("QTreeView{background:#FFFFFF;color:#333333;"
                                 "border:0;outline:none;} "
                                 "QTreeView::item:selected{background:#E4F0FF;color:#1E1E1E;} "
                                 "QTreeView::item:hover{background:#F0F7FF;}")

        # 示例
        self._ex_list = QListWidget()
        self._ex_list.itemDoubleClicked.connect(self._on_example_open)
        self._ex_list.setStyleSheet("QListWidget{background:#FFFFFF;color:#333333;"
                                    "border:0;outline:none;} "
                                    "QListWidget::item:selected{background:#E4F0FF;color:#1E1E1E;} "
                                    "QListWidget::item:hover{background:#F0F7FF;}")
        self._refresh_examples()

        self._side.addTab(self._tree, "资源管理器")
        self._side.addTab(self._ex_list, "示例")

        dock = QDockWidget("资源管理器", self)
        dock.setWidget(self._side)
        dock.setAllowedAreas(Qt.DockWidgetArea.LeftDockWidgetArea)
        dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.addDockWidget(Qt.DockWidgetArea.LeftDockWidgetArea, dock)
        dock.setFixedWidth(260)

    # ===================== 右侧语法速查面板 =====================
    def _build_right_panel(self):
        self._ref = QPlainTextEdit()
        self._ref.setReadOnly(True)
        self._ref.setFont(QFont("Consolas", 10))
        self._ref.setPlainText(SYNTAX_REFERENCE)
        self._ref.setStyleSheet(
            "QPlainTextEdit{background:#FAFAFA;color:#333333;border:0;"
            "padding:8px;} "
            "QScrollBar:vertical{width:10px;background:#F0F0F0;} "
            "QScrollBar::handle:vertical{background:#C0C0C0;border-radius:5px;min-height:20px;} "
            "QScrollBar::handle:vertical:hover{background:#A0A0A0;}"
        )

        dock = QDockWidget("语法速查", self)
        dock.setWidget(self._ref)
        dock.setAllowedAreas(Qt.DockWidgetArea.RightDockWidgetArea)
        dock.setFeatures(QDockWidget.DockWidgetFeature.DockWidgetClosable |
                          QDockWidget.DockWidgetFeature.DockWidgetMovable)
        self.addDockWidget(Qt.DockWidgetArea.RightDockWidgetArea, dock)
        dock.setFixedWidth(320)
        self._ref_dock = dock

    def _refresh_examples(self):
        self._ex_list.clear()
        for ex in backend.load_examples():
            QListWidgetItem(ex["name"], self._ex_list)

    def _on_tree_open(self, index: QModelIndex):
        path = self._fs_model.filePath(index)
        if os.path.isfile(path):
            self.open_file(path)

    def _on_example_open(self, item: QListWidgetItem):
        name = item.text()
        ex = next((e for e in backend.load_examples() if e["name"] == name), None)
        if not ex:
            return
        ed = self._create_editor("terra")
        ed.setPlainText(ex["source"])
        ed.set_clean()
        ed.file_path = None  # 示例未保存，存盘时落到工作区
        self._add_tab(ed, name + ".terra")
        self._set_status(f"已载入示例: {name}")

    # ===================== 底部面板 =====================
    def _build_bottom(self):
        self._bottom = QTabWidget()
        self._bottom.setTabPosition(QTabWidget.TabPosition.North)
        self._bottom.setStyleSheet(
            "QTabWidget::pane{border:0;border-top:1px solid #E0E0E0;} "
            "QTabBar::tab{background:#F5F5F5;color:#666666;padding:4px 14px;"
            "border:1px solid #E0E0E0;border-bottom:none;} "
            "QTabBar::tab:selected{color:#1E1E1E;background:#FFFFFF;}"
        )

        self._out = QPlainTextEdit()
        self._out.setReadOnly(True)
        self._out.setFont(QFont("Consolas", 11))
        self._out.setStyleSheet("QPlainTextEdit{background:#FFFFFF;color:#333333;border:0;}")

        self._problems = QListWidget()
        self._problems.itemDoubleClicked.connect(self._goto_problem)
        self._problems.setStyleSheet("QListWidget{background:#FFFFFF;color:#333333;border:0;"
                                     "outline:none;} "
                                     "QListWidget::item:selected{background:#E4F0FF;color:#1E1E1E;}")

        self._debug = QPlainTextEdit()
        self._debug.setReadOnly(True)
        self._debug.setFont(QFont("Consolas", 10))
        self._debug.setStyleSheet("QPlainTextEdit{background:#FFFFFF;color:#0066B3;border:0;}")

        self._watch = QTableWidget(0, 2)
        self._watch.setHorizontalHeaderLabels(["变量", "值"])
        self._watch.setStyleSheet("QTableWidget{background:#FFFFFF;color:#333333;"
                                  "gridline-color:#E0E0E0;outline:none;} "
                                  "QTableWidget::item:selected{background:#E4F0FF;color:#1E1E1E;} "
                                  "QHeaderView::section{background:#F5F5F5;color:#333333;"
                                  "border:1px solid #E0E0E0;padding:4px;}")
        self._watch.horizontalHeader().setStretchLastSection(True)

        self._bottom.addTab(self._out, "输出")
        self._bottom.addTab(self._problems, "问题")
        self._bottom.addTab(self._debug, "调试")
        self._bottom.addTab(self._watch, "变量监视")

        dock = QDockWidget("面板", self)
        dock.setWidget(self._bottom)
        dock.setAllowedAreas(Qt.DockWidgetArea.BottomDockWidgetArea)
        dock.setFeatures(QDockWidget.DockWidgetFeature.NoDockWidgetFeatures)
        self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, dock)
        dock.setFixedHeight(220)

    # ===================== 菜单 =====================
    def _build_menu(self):
        menubar = self.menuBar()
        menubar.setStyleSheet("QMenuBar{background:#FAFAFA;color:#333333;"
                              "border-bottom:1px solid #E0E0E0;} "
                              "QMenuBar::item:selected{background:#E4F0FF;} "
                              "QMenu{background:#FFFFFF;color:#333333;"
                              "border:1px solid #D0D0D0;} "
                              "QMenu::item:selected{background:#E4F0FF;color:#1E1E1E;} "
                              "QMenu::separator{height:1px;background:#E0E0E0;margin:4px 8px;}")

        f = menubar.addMenu("文件(&F)")
        f.addAction(self._act("新建文件", "Ctrl+N", self.new_file))
        f.addAction(self._act("打开文件...", "Ctrl+O", self.open_dialog))
        f.addAction(self._act("打开文件夹...", None, self.open_folder))
        f.addSeparator()
        f.addAction(self._act("保存", "Ctrl+S", self.save))
        f.addAction(self._act("另存为...", "Ctrl+Shift+S", self.save_as))
        f.addSeparator()
        f.addAction(self._act("选择工作空间...", None, self.choose_workspace))
        f.addSeparator()
        f.addAction(self._act("设置 .terra 文件关联", None, self._associate_terra))
        f.addSeparator()
        f.addAction(self._act("退出", "Ctrl+Q", self.close))

        e = menubar.addMenu("编辑(&E)")
        e.addAction(self._act("撤销", "Ctrl+Z", lambda: self._cur().undo() if self._cur() else None))
        e.addAction(self._act("重做", "Ctrl+Y", lambda: self._cur().redo() if self._cur() else None))
        e.addSeparator()
        e.addAction(self._act("剪切", "Ctrl+X", lambda: self._cur().cut() if self._cur() else None))
        e.addAction(self._act("复制", "Ctrl+C", lambda: self._cur().copy() if self._cur() else None))
        e.addAction(self._act("粘贴", "Ctrl+V", lambda: self._cur().paste() if self._cur() else None))
        e.addSeparator()
        e.addAction(self._act("查找", "Ctrl+F", self._find))

        v = menubar.addMenu("视图(&V)")
        v.addAction(self._act("切换深浅色主题", "Ctrl+T", self.toggle_theme))

        c = menubar.addMenu("编译(&C)")
        c.addAction(self._act("编译检查", "Ctrl+B", self.compile_current))
        c.addAction(self._act("导出 .seed 文件...", "Ctrl+E", self.export_seed))
        c.addSeparator()
        c.addAction(self._act("打开工作区文件夹", None, self._open_workspace_in_explorer))

        h = menubar.addMenu("帮助(&H)")
        h.addAction(self._act("关于", None, self._about))
        h.addAction(self._act("Terra 语法速查", None, self._syntax_help))

    def _act(self, text, shortcut, slot):
        a = QAction(text, self)
        if shortcut:
            a.setShortcut(QKeySequence(shortcut))
        a.triggered.connect(slot)
        return a

    def _cur(self):
        return self._current_editor()

    # ===================== 工具栏 =====================
    def _build_toolbar(self):
        self._toolbar = QToolBar("主工具栏")
        self._toolbar.setMovable(False)
        self._toolbar.setStyleSheet("QToolBar{background:#FAFAFA;border:0;"
                         "border-bottom:1px solid #E0E0E0;spacing:2px;padding:2px;} "
                         "QToolButton{background:transparent;color:#333333;padding:4px 10px;"
                         "border-radius:2px;} "
                         "QToolButton:hover{background:#E4F0FF;} "
                         "QToolButton:pressed{background:#D0E4FF;} "
                         "QToolButton:disabled{color:#AAAAAA;}")
        self._toolbar.addAction(self._act("新建", None, self.new_file))
        self._toolbar.addAction(self._act("打开", None, self.open_dialog))
        self._toolbar.addAction(self._act("保存", None, self.save))
        self._toolbar.addSeparator()
        self._btn_compile = self._tbtn("编译", self.compile_current, self._toolbar)
        self._btn_export = self._tbtn("生成.seed文件", self.export_seed, self._toolbar)
        self._toolbar.addSeparator()
        self._find_box = QLineEdit()
        self._find_box.setPlaceholderText("查找...")
        self._find_box.setFixedWidth(180)
        self._find_box.setStyleSheet("QLineEdit{background:#FFFFFF;color:#333333;"
                                      "border:1px solid #D0D0D0;border-radius:2px;"
                                      "padding:3px 8px;} "
                                      "QLineEdit:focus{border:1px solid #007ACC;}")
        self._find_box.returnPressed.connect(self._do_find)
        self._toolbar.addWidget(self._find_box)
        self.addToolBar(self._toolbar)

    def _tbtn(self, text, slot, tb):
        a = QAction(text, self)
        a.triggered.connect(slot)
        tb.addAction(a)
        return a

    # ===================== 主题切换 =====================
    def apply_theme(self):
        """根据当前主题应用所有控件样式"""
        t = THEMES[self._theme]
        self.setStyleSheet(t['global'])
        self._tabs.setStyleSheet(t['tabs'])
        self._side.setStyleSheet(t['side_tabs'])
        self._tree.setStyleSheet(t['tree'])
        self._ex_list.setStyleSheet(t['ex_list'])
        self._bottom.setStyleSheet(t['bottom_tabs'])
        self._out.setStyleSheet(t['out'])
        self._problems.setStyleSheet(t['problems'])
        self._debug.setStyleSheet(t['debug'])
        self._watch.setStyleSheet(t['watch'])
        self.menuBar().setStyleSheet(t['menubar'])
        self._toolbar.setStyleSheet(t['toolbar'])
        self._find_box.setStyleSheet(t['find_box'])
        self._status.setStyleSheet(t['status'])
        # 右侧语法速查面板
        if self._theme == 'light':
            self._ref.setStyleSheet(
                "QPlainTextEdit{background:#FAFAFA;color:#333333;border:0;padding:8px;} "
                "QScrollBar:vertical{width:10px;background:#F0F0F0;} "
                "QScrollBar::handle:vertical{background:#C0C0C0;border-radius:5px;min-height:20px;} "
                "QScrollBar::handle:vertical:hover{background:#A0A0A0;}"
            )
        else:
            self._ref.setStyleSheet(
                "QPlainTextEdit{background:#1E1E1E;color:#CCCCCC;border:0;padding:8px;} "
                "QScrollBar:vertical{width:10px;background:#2D2D2D;} "
                "QScrollBar::handle:vertical{background:#555555;border-radius:5px;min-height:20px;} "
                "QScrollBar::handle:vertical:hover{background:#777777;}"
            )
        # 更新所有已打开的编辑器
        for i in range(self._tabs.count()):
            w = self._tabs.widget(i)
            if isinstance(w, CodeEditor):
                w.set_theme(self._theme)

    def toggle_theme(self):
        """切换深浅色主题并保存设置"""
        self._theme = 'dark' if self._theme == 'light' else 'light'
        self._cfg['theme'] = self._theme
        self._save_cfg()
        self.apply_theme()
        name = '暗色' if self._theme == 'dark' else '浅色'
        self._set_status(f"已切换到{name}主题")

    # ===================== 状态栏 =====================
    def _build_status(self):
        self._status = QStatusBar()
        self._status.setStyleSheet("QStatusBar{background:#F0F0F0;color:#333333;"
                                   "border-top:1px solid #E0E0E0;} "
                                   "QStatusBar::item{border:none;}")
        self._status_msg = QLabel("就绪")
        self._status_pos = QLabel("行 1, 列 1")
        self._status_enc = QLabel("UTF-8")
        self._status.addPermanentWidget(self._status_msg, 1)
        self._status.addPermanentWidget(self._status_pos)
        self._status.addPermanentWidget(self._status_enc)
        self.setStatusBar(self._status)

    def _set_status(self, msg):
        self._status_msg.setText(msg)

    def _update_cursor_status(self, ed):
        cur = ed.textCursor()
        line = cur.blockNumber() + 1
        col = cur.positionInBlock() + 1
        self._status_pos.setText(f"行 {line}, 列 {col}")

    # ===================== 文件操作 =====================
    def new_file(self):
        ed = self._create_editor("terra")
        self._add_tab(ed, "未命名.terra")
        self._set_status("新建文件")

    def open_dialog(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "打开文件", backend.WORKSPACE,
            "Terra 源文件 (*.terra);;Seed 包 (*.seed);;所有文件 (*.*)")
        if path:
            self.open_file(path)

    def open_folder(self):
        d = QFileDialog.getExistingDirectory(self, "打开文件夹", backend.WORKSPACE)
        if d:
            self._fs_model.setRootPath(d)
            self._tree.setRootIndex(self._fs_model.index(d))
            self._set_status(f"已打开文件夹: {d}")

    def open_file(self, path):
        # 已打开则聚焦
        for i in range(self._tabs.count()):
            w = self._tabs.widget(i)
            if isinstance(w, CodeEditor) and w.file_path == path:
                self._tabs.setCurrentIndex(i)
                return
        try:
            with open(path, "r", encoding="utf-8") as f:
                src = f.read()
        except OSError as e:
            QMessageBox.warning(self, "打开失败", str(e))
            return
        lang = backend.detect_language(path)
        ed = self._create_editor(lang)
        ed.setPlainText(src)
        ed.set_clean()
        ed.file_path = path
        title = os.path.basename(path)
        self._add_tab(ed, title)
        self._set_status(f"已打开: {path}")

    def save(self):
        ed = self._current_editor()
        if ed is None:
            return
        if ed.file_path and os.path.isfile(ed.file_path):
            self._write_file(ed, ed.file_path)
        else:
            self.save_as()

    def save_as(self):
        ed = self._current_editor()
        if ed is None:
            return
        default = os.path.join(backend.WORKSPACE, "未命名.terra")
        path, _ = QFileDialog.getSaveFileName(self, "另存为", default,
                                              "Terra 源文件 (*.terra);;所有文件 (*.*)")
        if path:
            self._write_file(ed, path)

    def _write_file(self, ed, path):
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(ed.toPlainText())
        except OSError as e:
            QMessageBox.warning(self, "保存失败", str(e))
            return
        ed.file_path = path
        ed.set_clean()
        self._mark_current_clean(os.path.basename(path))
        if backend.detect_language(path) != ed.language:
            ed.set_language(backend.detect_language(path))
        self._set_status(f"已保存: {path}")

    def _close_tab(self, idx):
        w = self._tabs.widget(idx)
        if isinstance(w, CodeEditor) and w.is_dirty():
            name = self._tabs.tabText(idx).rstrip("*")
            r = QMessageBox.question(self, "未保存",
                                     f"「{name}」尚未保存，是否保存？",
                                     QMessageBox.StandardButton.Save |
                                     QMessageBox.StandardButton.Discard |
                                     QMessageBox.StandardButton.Cancel)
            if r == QMessageBox.StandardButton.Save:
                self._tabs.setCurrentIndex(idx)
                self.save()
            elif r == QMessageBox.StandardButton.Cancel:
                return
        self._tabs.removeTab(idx)

    # ===================== 编译 / 运行 =====================
    def _current_name(self):
        ed = self._current_editor()
        if ed is None:
            return "main", None
        if ed.file_path:
            return os.path.splitext(os.path.basename(ed.file_path))[0], ed.file_path
        # 示例 / 未命名：用标签页标题
        title = self._tabs.tabText(self._tabs.currentIndex()).rstrip("*")
        return os.path.splitext(title)[0] or "main", None

    def compile_current(self):
        ed = self._current_editor()
        if ed is None:
            return None
        name, fpath = self._current_name()
        src = ed.toPlainText()
        self._problems.clear()
        self._out.clear()
        res = backend.compile_source(src, name)
        if not res["ok"]:
            self._problems.addItem(f"❌ 第 {res['line']} 行: {res['error']}")
            self._bottom.setCurrentWidget(self._problems)
            self._set_status("编译失败")
            return None
        summary = (f"✅ 编译成功  [{name}]\n"
                   f"   令牌数 : {res['token_count']}\n"
                   f"   代码段 : {res['code_size']} 字节\n"
                   f"   数据段 : {res['data_size']} 字节\n"
                   f"   含 GUI : {'是' if res['has_gui'] else '否'}\n"
                   f"   变量   : {', '.join(res['vars'].keys()) or '（无）'}")
        self._out.setPlainText(summary)
        self._bottom.setCurrentWidget(self._out)
        self._set_status(f"编译成功：{res['code_size']}B 代码 / {res['data_size']}B 数据")
        return res

    def export_seed(self):
        """编译当前代码并导出为 .seed 文件"""
        res = self.compile_current()
        if res is None:
            return
        name, _ = self._current_name()
        default_name = (name or "output") + ".seed"
        filePath, _ = QFileDialog.getSaveFileName(
            self, "导出 .seed 文件", default_name,
            "Seed 包 (*.seed);;所有文件 (*)")
        if not filePath:
            return
        try:
            with open(filePath, "wb") as f:
                f.write(res["seed"])
            self._set_status(f"已导出: {filePath}")
            QMessageBox.information(
                self, "导出成功",
                f"已导出到:\n{filePath}\n\n"
                f"代码段: {res['code_size']} 字节\n"
                f"数据段: {res['data_size']} 字节\n"
                f"总大小: {len(res['seed'])} 字节\n\n"
                f"下一步: 把此 .seed 文件复制到 Ubuntu，\n"
                f"用 inject_seed.sh 注入到 disk.img")
        except OSError as e:
            QMessageBox.critical(self, "导出失败", str(e))

    def run_current(self, trace):
        if self._running:
            return
        ed = self._current_editor()
        if ed is None:
            return
        name, fpath = self._current_name()
        lang = backend.detect_language(fpath or "x.terra") if fpath else ed.language

        blob = None
        var_names = None
        # .seed 直接运行；.terra 先编译
        if lang == "seed" and fpath:
            try:
                with open(fpath, "rb") as f:
                    blob = f.read()
            except OSError as e:
                QMessageBox.warning(self, "打开失败", str(e))
                return
        else:
            res = self.compile_current()
            if res is None:
                return
            blob = res["seed"]
            var_names = res["vars"]

        self._out.clear()
        self._debug.clear()
        self._watch.setRowCount(0)
        self._set_status("运行中...")
        self._running = True
        self._stop_requested = False
        QApplication.processEvents()

        session = backend.create_run_session(blob, trace, var_names)
        self._run_session = session
        buf = io.StringIO()
        last_out = 0
        last_log = 0
        steps = 0
        total_steps = 0
        try:
            while self._running:
                QApplication.processEvents()
                if self._stop_requested:
                    self._out.append("\n⏹ 已停止。")
                    break
                with contextlib.redirect_stdout(buf):
                    steps = session["vm"].run(max_steps=3000)
                total_steps += steps
                # 实时输出
                full = "".join(session["vm"].out)
                if len(full) > last_out:
                    self._out.append(full[last_out:])
                    last_out = len(full)
                    QApplication.processEvents()
                # 实时追踪
                log = buf.getvalue()
                if len(log) > last_log:
                    self._debug.append(log[last_log:])
                    last_log = len(log)
                # 交互输入
                if session["vm"].waiting_input:
                    prompt = str(session["vm"].ask_prompt)
                    type_hint = "（数字）" if session["vm"].ask_type == 1 else "（文本）"
                    ans, ok = QInputDialog.getText(
                        self, "程序请求输入", prompt + type_hint)
                    session["vm"].input_queue.append(str(ans) if ok else "")
                    continue
                if session["vm"].halted:
                    break
        except Exception as e:
            import traceback
            err_msg = f"运行出错：{e}\n\n{traceback.format_exc()}"
            self._out.append("\n❌ " + err_msg)
            self._set_status("运行出错")
            QMessageBox.critical(self, "运行出错", err_msg)
        finally:
            self._running = False
            self._run_session = None

        # 变量监视
        vars_map = var_names or {}
        watch = {nm: session["vm"].locals.get(idx, 0) for nm, idx in vars_map.items()} \
            if vars_map else {f"L{k}": v for k, v in session["vm"].locals.items()}
        self._show_watch(watch)
        self._bottom.setCurrentWidget(self._out)
        if not self._status_msg.text().startswith("运行"):
            pass  # 出错或停止时保留原状态
        else:
            self._set_status(f"运行结束：{total_steps} 步")

    def stop_run(self):
        if self._running:
            self._stop_requested = True
            self._set_status("正在停止...")

    # ===================== 部署到 disk.img =====================
    def _load_cfg(self):
        p = os.path.join(os.path.expanduser("~"), ".sproutide.json")
        self._cfg_path = p
        try:
            with open(p, "r", encoding="utf-8") as f:
                return json.load(f)
        except (OSError, ValueError):
            return {}

    def _save_cfg(self):
        try:
            with open(self._cfg_path, "w", encoding="utf-8") as f:
                json.dump(self._cfg, f, ensure_ascii=False, indent=2)
        except OSError:
            pass

    def deploy_current(self):
        ed = self._current_editor()
        if ed is None:
            return
        name, fpath = self._current_name()
        lang = backend.detect_language(fpath or "x.terra") if fpath else ed.language
        blob = None
        if lang == "seed" and fpath:
            try:
                with open(fpath, "rb") as f:
                    blob = f.read()
            except OSError as e:
                QMessageBox.warning(self, "打开失败", str(e))
                return
        else:
            res = self.compile_current()
            if res is None:
                return
            blob = res["seed"]
        img = self._disk_img or backend.default_disk_img()
        if not img:
            QMessageBox.warning(
                self, "未设置 disk.img",
                "未在项目目录或当前目录找到 disk.img。\n\n"
                "请先生成镜像（make disk / bash ./make_disk.sh），\n"
                "或在「部署 → 设置 disk.img 路径」中指定。")
            return
        self._set_status("正在部署到 disk.img ...")
        QApplication.processEvents()
        ok, msg = backend.deploy_seed_to_disk(img, name, blob)
        self._out.setPlainText(msg)
        self._bottom.setCurrentWidget(self._out)
        self._set_status("部署" + ("成功" if ok else "失败"))

    def configure_disk_img(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择 disk.img",
            self._disk_img or backend.project_dir(),
            "Disk image (disk.img);;所有文件 (*.*)")
        if path:
            self._disk_img = path
            self._cfg["disk_img"] = path
            self._save_cfg()
            self._set_status(f"disk.img 已设为: {path}")

    def launch_qemu(self):
        proj = backend.project_dir()
        if not (shutil.which("make") or shutil.which("bash")):
            QMessageBox.warning(
                self, "未找到 make/qemu",
                "本机未检测到 make 或 qemu，无法自动启动。\n"
                "请在 SproutOS 项目目录手动运行：make run")
            return
        try:
            self._qemu_proc = subprocess.Popen(["make", "run"], cwd=proj,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self._set_status("已启动 QEMU（独立窗口）。Ctrl+Alt+2 看串口，Ctrl+Alt+1 回 GUI。")
        except Exception as e:  # noqa
            QMessageBox.warning(self, "启动失败", str(e))

    def _show_watch(self, watch):
        self._watch.setRowCount(0)
        for k, v in watch.items():
            row = self._watch.rowCount()
            self._watch.insertRow(row)
            self._watch.setItem(row, 0, QTableWidgetItem(str(k)))
            self._watch.setItem(row, 1, QTableWidgetItem(str(v)))
        self._bottom.setCurrentWidget(self._watch)

    def _goto_problem(self, item):
        m = re.search(r"第 (\d+) 行", item.text())
        if not m:
            return
        ln = int(m.group(1))
        ed = self._current_editor()
        if ed is None:
            return
        cur = ed.textCursor()
        cur.movePosition(QTextCursor.MoveOperation.Start)
        for _ in range(ln - 1):
            if not cur.movePosition(QTextCursor.MoveOperation.Down):
                break
        ed.setTextCursor(cur)
        ed.setFocus()

    # ===================== 查找 =====================
    def _find(self):
        self._find_box.setFocus()
        self._find_box.selectAll()

    def _do_find(self):
        ed = self._current_editor()
        if ed is None:
            return
        text = self._find_box.text()
        if not text:
            return
        cur = ed.textCursor()
        doc = ed.document()
        start = cur.position() if cur.hasSelection() else 0
        found = doc.find(text, start)
        if found.isNull():
            found = doc.find(text, 0)
        if not found.isNull():
            ed.setTextCursor(found)
            ed.setFocus()

    # ===================== 帮助 =====================
    def _associate_terra(self):
        """设置 .terra 文件默认用本 IDE 打开（Windows 修改注册表，Linux 提示手动设置）。"""
        import sys
        if sys.platform == "win32":
            try:
                import winreg
                import ctypes

                # 获取当前 exe 路径（打包后）或脚本路径
                if getattr(sys, "frozen", False):
                    exe_path = sys.executable
                else:
                    # 开发模式：用 app.py 入口
                    app_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "app.py")
                    exe_path = f'"{sys.executable}" "{app_path}"'

                # 1. .terra 扩展名 -> SproutIDE.TerraFile
                key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\Classes\.terra")
                winreg.SetValueEx(key, "", 0, winreg.REG_SZ, "SproutIDE.TerraFile")
                winreg.CloseKey(key)

                # 2. 文件类型描述
                key = winreg.CreateKey(winreg.HKEY_CURRENT_USER, r"Software\Classes\SproutIDE.TerraFile")
                winreg.SetValueEx(key, "", 0, winreg.REG_SZ, "Terra 源文件")
                winreg.CloseKey(key)

                # 3. 打开命令
                key = winreg.CreateKey(winreg.HKEY_CURRENT_USER,
                                       r"Software\Classes\SproutIDE.TerraFile\shell\open\command")
                winreg.SetValueEx(key, "", 0, winreg.REG_SZ, f'"{exe_path}" "%1"')
                winreg.CloseKey(key)

                # 4. 图标（用 exe 自带的图标）
                key = winreg.CreateKey(winreg.HKEY_CURRENT_USER,
                                       r"Software\Classes\SproutIDE.TerraFile\DefaultIcon")
                winreg.SetValueEx(key, "", 0, winreg.REG_SZ, f'"{exe_path}",0')
                winreg.CloseKey(key)

                # 通知 Windows 刷新文件关联
                ctypes.windll.shell32.SHChangeNotify(0x08000000, 0, None, None)

                QMessageBox.information(self, "文件关联",
                                        "已成功设置 .terra 文件默认用 SproutOS IDE 打开！\n\n"
                                        "双击 .terra 文件即可直接在 IDE 中打开。")
            except Exception as e:
                QMessageBox.warning(self, "文件关联", f"设置失败：{e}\n\n请以管理员身份运行 IDE 后重试。")
        else:
            # Linux：提示手动设置
            QMessageBox.information(self, "文件关联",
                                    "Linux 下设置 .terra 文件关联：\n\n"
                                    "1. 右键任意 .terra 文件\n"
                                    "2. 选择「属性」→「打开方式」\n"
                                    "3. 选择 SproutIDE 并设为默认\n\n"
                                    "或在终端执行：\n"
                                    "xdg-mime default sproutide.desktop text/x-terra")

    def _about(self):
        QMessageBox.about(self, "关于 SproutOS IDE",
                          "SproutOS IDE — Terra 语言原生开发平台\n\n"
                          "• 真正运行在本机的桌面 IDE（PyQt6），非浏览器版\n"
                          "• 内置 Terra 中文编译器 (terrac.py) + SeedVM 模拟器 (seedvm_test.py)\n"
                          "• 编写 .terra 即可编译为 .seed 并在主机模拟运行\n\n"
                          "版本：1.0 （Sprout BIOS/UEFI 双启版配套工具）")

    def _syntax_help(self):
        box = QMessageBox(self)
        box.setWindowTitle("Terra 语法速查")
        box.setText(SYNTAX_REFERENCE)
        box.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        box.exec()

    def choose_workspace(self):
        """选择工作空间目录"""
        current = backend.WORKSPACE
        path = QFileDialog.getExistingDirectory(self, "选择工作空间目录", current)
        if not path:
            return
        ok, msg = backend.set_workspace(path)
        if ok:
            self._cfg["workspace"] = path
            self._save_cfg()
            # 刷新左侧文件树
            self._fs_model.setRootPath(path)
            self._tree.setRootIndex(self._fs_model.index(path))
            self._set_status(msg)
            QMessageBox.information(self, "工作空间", msg + "\n\n重启 IDE 后完全生效。")
        else:
            QMessageBox.warning(self, "工作空间", msg)

    def _open_workspace_in_explorer(self):
        try:
            webbrowser.open(backend.WORKSPACE)
        except Exception:
            QMessageBox.information(self, "工作区", backend.WORKSPACE)

    # 光标状态更新
    def eventFilter(self, obj, event):
        return super().eventFilter(obj, event)

    def closeEvent(self, event):
        """程序退出时清理子进程，避免临时目录被占用"""
        # 停止正在运行的 Terra 程序
        if self._running:
            self._stop_requested = True
        # 终止 QEMU 子进程
        if self._qemu_proc and self._qemu_proc.poll() is None:
            try:
                self._qemu_proc.terminate()
                self._qemu_proc.wait(timeout=3)
            except Exception:
                try:
                    self._qemu_proc.kill()
                except Exception:
                    pass
        # 终止所有子进程（make 可能启动了 qemu-system-i386 等）
        try:
            import psutil
            current = psutil.Process()
            children = current.children(recursive=True)
            for child in children:
                try:
                    child.terminate()
                except Exception:
                    pass
            psutil.wait_procs(children, timeout=3)
        except ImportError:
            pass
        super().closeEvent(event)

    def keyPressEvent(self, event):
        # 全局快捷键已由 QAction 处理；这里仅更新光标位置
        super().keyPressEvent(event)
        ed = self._current_editor()
        if ed is not None:
            self._update_cursor_status(ed)
