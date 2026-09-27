#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""纯正则模式（无 PyQt 依赖），供 highlighter.py 与单元测试使用。"""

TERRA_KEYWORDS = [
    "支持库引入", "交互_取终端反馈", "交互_输出",
    "计数循环", "判断循环", "标准循环",
    "跳出循环", "循环结束", "新建变量", "如果结束",
    "画矩形", "画文字", "画圆", "画线", "画点",
    "到文本", "到整数", "取宽", "取高", "清屏", "刷新",
    "输出", "如果", "否则", "真", "假",
    "且", "或", "非",
    "小于", "大于", "等于", "不等于", "小于等于", "大于等于",
]
TERRA_COLORS = ["黑色", "白色", "红色", "绿色", "蓝色", "黄色", "青色", "紫色"]

EN_KEYWORDS = [
    "def", "class", "if", "elif", "else", "for", "while", "return", "import",
    "from", "try", "except", "finally", "with", "as", "lambda", "pass",
    "break", "continue", "global", "yield", "raise", "assert", "del", "in",
    "is", "not", "and", "or", "None", "True", "False", "self",
    "public", "private", "protected", "static", "void", "int", "char",
    "float", "double", "struct", "typedef", "enum", "unsigned", "signed",
    "include", "define", "ifdef", "ifndef", "endif", "uint8_t", "uint16_t",
    "uint32_t", "int8_t", "int16_t", "int32_t", "size_t",
]

# 纯字符串正则（与 PyQt6.QRegularExpression 同一套 PCRE 语法）
TERRA_PATTERN = (
    r"(?P<comment>//[^\n]*|#[^\n]*)"
    r"|(?P<string>\"[^\"]*\"|'[^']*')"
    r"|(?P<number>\b\d+(\.\d+)?\b)"
    r"|(?P<cnkw>(?:" + "|".join(TERRA_KEYWORDS) + r"))"
    r"|(?P<color>(?:" + "|".join(TERRA_COLORS) + r"))"
    r"|(?P<op>[=<>!+\-*/%()\[\],;:.])"
)

GENERIC_PATTERN = (
    r"(?P<comment>//[^\n]*|#[^\n]*|/\*.*?\*/)"
    r"|(?P<string>\"[^\"]*\"|'[^']*'|`[^`]*`)"
    r"|(?P<number>\b\d+(\.\d+)?\b)"
    r"|(?P<enkw>\b(?:" + "|".join(EN_KEYWORDS) + r")\b)"
    r"|(?P<op>[=<>!+\-*/%()\[\],;:.{}])"
)
