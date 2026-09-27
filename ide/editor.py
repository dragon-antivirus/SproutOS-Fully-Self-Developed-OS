#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PyQt6 代码编辑器：行号栏 + 当前行高亮 + Tab 缩进 + 关联语法高亮。"""

from PyQt6.QtWidgets import QPlainTextEdit, QWidget, QApplication, QTextEdit, QCompleter
from PyQt6.QtCore import Qt, QRect, QSize, QStringListModel
from PyQt6.QtGui import QPainter, QColor, QTextFormat, QFont, QTextCursor

from highlighter import CodeHighlighter

# === 主题配色 ===
EDITOR_THEMES = {
    'light': {
        'bg': "#FFFFFF",
        'fg': "#1E1E1E",
        'cur_line': "#EAF2FF",
        'gutter_bg': "#FAFAFA",
        'gutter_fg': "#999999",
        'gutter_cur': "#007ACC",
        'sel_bg': "#CFE2FF",
        'guide_line': "#C8D6E5",   # 代码块范围指示线
        'guide_line_active': "#4A90D9",  # 当前光标所在块的指示线
    },
    'dark': {
        'bg': "#1E1E1E",
        'fg': "#D4D4D4",
        'cur_line': "#282828",
        'gutter_bg': "#1E1E1E",
        'gutter_fg': "#858585",
        'gutter_cur': "#C6C6C6",
        'sel_bg': "#264F78",
        'guide_line': "#3A3A3A",
        'guide_line_active': "#569CD6",
    },
}


class LineNumberArea(QWidget):
    def __init__(self, editor):
        super().__init__(editor)
        self.editor = editor

    def sizeHint(self):
        return QSize(self.editor.line_number_area_width(), 0)

    def paintEvent(self, event):
        self.editor.line_number_area_paint_event(event)


class CodeEditor(QPlainTextEdit):
    def __init__(self, parent=None, language="terra"):
        super().__init__(parent)
        self.language = language
        self.file_path = None
        self._dirty = False
        self._theme = 'light'
        self._completer = None
        self._completion_enabled = True
        self._guide_line_color = QColor("#C8D6E5")
        self._guide_line_active_color = QColor("#4A90D9")

        self.setFont(QFont("Consolas", 12))

        self.gutter = LineNumberArea(self)
        self._highlighter = CodeHighlighter(self.document(), language)

        self.blockCountChanged.connect(self.update_line_number_area_width)
        self.updateRequest.connect(self.update_line_number_area)
        self.cursorPositionChanged.connect(self.highlight_current_line)
        self.textChanged.connect(self._on_text_changed)

        self.update_line_number_area_width(0)
        self.set_theme('light')
        self.highlight_current_line()

    # ---- 行号 ----
    def line_number_area_width(self):
        digits = max(3, len(str(max(1, self.blockCount()))))
        return 8 + self.fontMetrics().horizontalAdvance("9") * digits

    def update_line_number_area_width(self, _=0):
        self.setViewportMargins(self.line_number_area_width(), 0, 0, 0)

    def update_line_number_area(self, rect, dy):
        if dy:
            self.gutter.scroll(0, dy)
        else:
            self.gutter.update(0, rect.y(), self.gutter.width(), rect.height())
        if rect.contains(self.viewport().rect()):
            self.update_line_number_area_width(0)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        cr = self.contentsRect()
        self.gutter.setGeometry(QRect(cr.left(), cr.top(),
                                      self.line_number_area_width(), cr.height()))

    def line_number_area_paint_event(self, event):
        painter = QPainter(self.gutter)
        painter.fillRect(event.rect(), self._gutter_bg)
        block = self.firstVisibleBlock()
        block_number = block.blockNumber()
        top = int(self.blockBoundingGeometry(block).translated(self.contentOffset()).top())
        bottom = top + int(self.blockBoundingRect(block).height())
        cur = self.textCursor().blockNumber()
        while block.isValid() and top <= event.rect().bottom():
            if block.isVisible() and bottom >= event.rect().top():
                num = str(block_number + 1)
                color = self._gutter_fg if block_number != cur else self._gutter_cur
                painter.setPen(color)
                painter.drawText(0, top, self.gutter.width() - 4, self.fontMetrics().height(),
                                 Qt.AlignmentFlag.AlignRight, num)
            block = block.next()
            top = bottom
            bottom = top + int(self.blockBoundingRect(block).height())
            block_number += 1

    def highlight_current_line(self):
        extra = []
        if not self.isReadOnly():
            sel = QTextEdit.ExtraSelection()
            sel.format.setBackground(self._cur_line_color)
            sel.format.setProperty(QTextFormat.Property.FullWidthSelection, True)
            sel.cursor = self.textCursor()
            sel.cursor.clearSelection()
            extra.append(sel)
        self.setExtraSelections(extra)
        # 触发代码块指示线重绘
        self.viewport().update()

    # ---- 代码块范围指示线 ----
    # Terra 语言块开头关键词（支持中英文括号、无括号）
    _BLOCK_STARTERS = ("如果", "计数循环", "标准循环", "判断", "循环")
    # 块结尾关键词
    _BLOCK_ENDERS = {
        "如果": "如果结束",
        "计数循环": "循环结束",
        "标准循环": "循环结束",
        "判断": "判断结束",
        "循环": "循环结束",
    }

    def _find_block_pairs(self):
        """解析代码，找到所有成对的代码块，返回 [(start_line, end_line, keyword), ...]"""
        pairs = []
        stack = []  # [(line_number, keyword), ...]
        block = self.document().firstBlock()
        line_num = 0
        while block.isValid():
            text = block.text().strip()
            matched_starter = None

            # 检查是否是块开头（行以关键词开头，后面不是"结束"即可）
            for starter in self._BLOCK_STARTERS:
                if text.startswith(starter):
                    rest = text[len(starter):]
                    # 排除"如果结束"、"循环结束"等结尾行
                    if not rest.startswith("结束"):
                        matched_starter = starter
                        break

            if matched_starter:
                stack.append((line_num, matched_starter))
            else:
                # 检查是否是块结尾
                matched_ender = None
                for starter, ender in self._BLOCK_ENDERS.items():
                    if text == ender or text.startswith(ender):
                        matched_ender = ender
                        break

                if matched_ender:
                    # 循环结束 匹配栈顶任意循环类型（计数循环/标准循环/循环）
                    if matched_ender == "循环结束":
                        for i in range(len(stack) - 1, -1, -1):
                            if stack[i][1] in ("计数循环", "标准循环", "循环"):
                                pairs.append((stack[i][0], line_num, stack[i][1]))
                                del stack[i]
                                break
                    else:
                        # 其他结尾匹配对应类型
                        for starter, ender in self._BLOCK_ENDERS.items():
                            if ender == matched_ender:
                                for i in range(len(stack) - 1, -1, -1):
                                    if stack[i][1] == starter:
                                        pairs.append((stack[i][0], line_num, starter))
                                        del stack[i]
                                        break
                                break

            block = block.next()
            line_num += 1
        return pairs

    def paintEvent(self, event):
        """重写 paintEvent，在父类绘制完成后绘制代码块范围指示线"""
        super().paintEvent(event)

        if not self.document() or self.blockCount() == 0:
            return

        pairs = self._find_block_pairs()
        if not pairs:
            return

        cur_line = self.textCursor().blockNumber()
        painter = QPainter(self.viewport())
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)

        # 指示线 x 坐标：紧贴代码左边缘
        # QPlainTextEdit 文本有默认左内边距，线画在字的左侧
        line_x = 4

        for start_line, end_line, keyword in pairs:
            # 计算起始行顶部 y 坐标
            start_block = self.document().findBlockByNumber(start_line)
            if not start_block.isValid():
                continue
            start_top = int(self.blockBoundingGeometry(start_block).translated(self.contentOffset()).top())

            # 计算结束行底部 y 坐标
            end_block = self.document().findBlockByNumber(end_line)
            if not end_block.isValid():
                continue
            end_bottom = int(self.blockBoundingGeometry(end_block).translated(self.contentOffset()).bottom())

            # 只绘制可见区域内的线
            if end_bottom < event.rect().top() or start_top > event.rect().bottom():
                continue

            # 当前光标所在块用高亮色
            if start_line <= cur_line <= end_line:
                painter.setPen(self._guide_line_active_color)
            else:
                painter.setPen(self._guide_line_color)

            # 绘制竖线（1像素宽）
            painter.drawLine(line_x, start_top, line_x, end_bottom)

        painter.end()

    # ---- 内容改动标记 ----
    def _on_text_changed(self):
        if not self._dirty:
            self._dirty = True
            self.parentWidget()._mark_dirty(self) if hasattr(self.parentWidget(), "_mark_dirty") else None

    def set_theme(self, theme):
        """切换编辑器主题（浅色/暗色）"""
        if theme not in EDITOR_THEMES:
            theme = 'light'
        self._theme = theme
        t = EDITOR_THEMES[theme]
        self.setStyleSheet(
            f"QPlainTextEdit{{background-color:{t['bg']};color:{t['fg']};"
            f"border:0;selection-background-color:{t['sel_bg']};}}"
        )
        self._cur_line_color = QColor(t['cur_line'])
        self._gutter_bg = QColor(t['gutter_bg'])
        self._gutter_fg = QColor(t['gutter_fg'])
        self._gutter_cur = QColor(t['gutter_cur'])
        self._guide_line_color = QColor(t['guide_line'])
        self._guide_line_active_color = QColor(t['guide_line_active'])
        self.gutter.update()
        self.highlight_current_line()
        # 同步语法高亮主题
        if hasattr(self, '_highlighter'):
            self._highlighter.set_theme(theme)

    def set_language(self, language):
        if language == self.language:
            return
        self.language = language
        self._highlighter.setParent(None)
        self._highlighter = CodeHighlighter(self.document(), language)

    def set_clean(self):
        self._dirty = False

    def is_dirty(self):
        return self._dirty

    # ---- 自动补全 ----
    def set_completer(self, completer):
        """设置自动补全器"""
        if self._completer:
            self._completer.widget().removeEventFilter(self)
        self._completer = completer
        if not completer:
            return
        completer.setWidget(self)
        completer.setCompletionMode(QCompleter.CompletionMode.PopupCompletion)
        completer.setCaseSensitivity(Qt.CaseSensitivity.CaseInsensitive)
        completer.setFilterMode(Qt.MatchFlag.MatchContains)
        completer.activated.connect(self.insert_completion)

    def completer(self):
        return self._completer

    def text_under_cursor(self):
        """获取光标下的单词（用于补全匹配）"""
        cur = self.textCursor()
        cur.select(QTextCursor.SelectionType.WordUnderCursor)
        return cur.selectedText()

    def insert_completion(self, completion):
        """插入选中的补全项"""
        if not self._completer or self._completer.widget() != self:
            return
        cur = self.textCursor()
        extra = len(completion) - len(self._completer.completionPrefix())
        cur.movePosition(QTextCursor.MoveOperation.Left)
        cur.movePosition(QTextCursor.MoveOperation.EndOfWord)
        cur.insertText(completion[-extra:])
        self.setTextCursor(cur)

    def focusInEvent(self, event):
        if self._completer:
            self._completer.setWidget(self)
        super().focusInEvent(event)

    # ---- Tab 缩进 + 自动补全（VSCode 风格）----
    def keyPressEvent(self, event):
        # 补全弹窗显示时的按键处理
        if self._completer and self._completer.popup().isVisible():
            if event.key() in (Qt.Key.Key_Enter, Qt.Key.Key_Return, Qt.Key.Key_Tab):
                event.ignore()
                completion = self._completer.currentCompletion()
                if completion:
                    self.insert_completion(completion)
                return
            elif event.key() == Qt.Key.Key_Escape:
                self._completer.popup().hide()
                return
            elif event.key() in (Qt.Key.Key_Up, Qt.Key.Key_Down, Qt.Key.Key_PageUp, Qt.Key.Key_PageDown):
                event.ignore()
                return

        if event.key() == Qt.Key.Key_Tab and not (self._completer and self._completer.popup().isVisible()):
            self.insertPlainText("    ")
            return
        if event.key() == Qt.Key.Key_Backtab:
            # 减少缩进（简单实现：删除行首至多 4 个空格）
            cur = self.textCursor()
            cur.movePosition(cur.MoveOperation.StartOfLine)
            self.setTextCursor(cur)
            for _ in range(4):
                if self.document().characterAt(cur.position()) == " ":
                    cur.deleteChar()
                else:
                    break
            return

        # 回车键自动缩进：匹配上一行缩进，结构关键词后多缩进一级
        if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            cursor = self.textCursor()
            block = cursor.block()
            line_text = block.text()
            # 计算当前行前导空格数
            indent = len(line_text) - len(line_text.lstrip(' '))
            # 如果当前行以结构关键词开头，多缩进一级（4空格）
            stripped = line_text.strip()
            if any(stripped.startswith(kw) for kw in ['如果', '计数循环', '判断循环', '标准循环']):
                indent += 4
            # 先执行默认换行
            super().keyPressEvent(event)
            # 再插入缩进空格
            if indent > 0:
                self.insertPlainText(' ' * indent)
            return

        super().keyPressEvent(event)

        # 输入后显示补全
        if self._completer and self._completion_enabled:
            completion_prefix = self.text_under_cursor()
            if len(completion_prefix) < 1:
                self._completer.popup().hide()
                return
            if completion_prefix != self._completer.completionPrefix():
                self._completer.setCompletionPrefix(completion_prefix)
                self._completer.popup().setCurrentIndex(self._completer.completionModel().index(0, 0))
            # 计算补全弹窗位置
            cr = self.cursorRect()
            cr.setWidth(self._completer.popup().sizeHintForColumn(0)
                        + self._completer.popup().verticalScrollBar().sizeHint().width())
            self._completer.complete(cr)
