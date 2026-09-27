#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PyQt6 语法高亮：Terra 中文语言 + 通用（C/Python/文本）回退。"""

from PyQt6.QtGui import QSyntaxHighlighter, QTextCharFormat, QColor, QFont
from PyQt6.QtCore import QRegularExpression

from patterns import TERRA_PATTERN, GENERIC_PATTERN

# === 语法高亮主题配色 ===
HIGHLIGHT_THEMES = {
    'light': {
        'comment': "#008000",
        'string': "#A31515",
        'number': "#098658",
        'cnkw': "#0055B8",
        'color': "#267F99",
        'enkw': "#7A3E9D",
        'op': "#333333",
    },
    'dark': {
        'comment': "#6A9955",
        'string': "#CE9178",
        'number': "#B5CEA8",
        'cnkw': "#569CD6",
        'color': "#4EC9B0",
        'enkw': "#C586C0",
        'op': "#D4D4D4",
    },
}


def _fmt(color, bold=False):
    f = QTextCharFormat()
    f.setForeground(color)
    if bold:
        f.setFontWeight(QFont.Weight.Bold)
    return f


class CodeHighlighter(QSyntaxHighlighter):
    def __init__(self, parent, language):
        super().__init__(parent)
        self.language = language
        self._theme = 'light'
        self._build_formats()
        if language in ("terra", "seed"):
            self._rx = QRegularExpression(TERRA_PATTERN)
        else:
            self._rx = QRegularExpression(GENERIC_PATTERN)

    def _build_formats(self):
        """根据当前主题构建格式字典"""
        t = HIGHLIGHT_THEMES[self._theme]
        if self.language in ("terra", "seed"):
            self._groups = {
                "comment": _fmt(QColor(t['comment'])),
                "string": _fmt(QColor(t['string'])),
                "number": _fmt(QColor(t['number'])),
                "cnkw": _fmt(QColor(t['cnkw']), bold=True),
                "color": _fmt(QColor(t['color'])),
                "op": _fmt(QColor(t['op'])),
            }
        else:
            self._groups = {
                "comment": _fmt(QColor(t['comment'])),
                "string": _fmt(QColor(t['string'])),
                "number": _fmt(QColor(t['number'])),
                "enkw": _fmt(QColor(t['enkw'])),
                "op": _fmt(QColor(t['op'])),
            }

    def set_theme(self, theme):
        """切换语法高亮主题"""
        if theme not in HIGHLIGHT_THEMES:
            theme = 'light'
        if theme == self._theme:
            return
        self._theme = theme
        self._build_formats()
        # 重新高亮整个文档
        doc = self.document()
        if doc:
            self.rehighlight()

    def highlightBlock(self, text):
        it = self._rx.globalMatch(text)
        while it.hasNext():
            m = it.next()
            for name, fmt in self._groups.items():
                start = m.capturedStart(name)
                if start >= 0:
                    length = m.capturedLength(name)
                    self.setFormat(start, length, fmt)
                    break
