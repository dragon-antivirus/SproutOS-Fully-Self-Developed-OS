#!/usr/bin/env python3
"""
terrac.py — Terra 编译器
将 .terra 源文件编译成 .seed 二进制包（SproutOS SeedVM 字节码）。

用法:
    python3 terrac.py input.terra [-o output.seed] [-n 包名] [-a 作者]

Terra是 SproutOS 的专属中文编程语言，关键词全中文（易语言风格）：

  输出(值)                        — 打印到屏幕
  新建变量 名                     — 声明变量（默认 0）；赋值用  名 = 值
  如果 (条件) ... 否则 ... 如果结束 — 条件分支
  计数循环 (次数, 计数变量) ... 循环结束 ()  — 计数循环
  判断循环 (条件) ... 循环结束 ()  — 条件循环
  标准循环 (间隔毫秒, 计数变量) ... 循环结束 ()  — 无限循环（带间隔）
  跳出循环 ()                     — 跳出最内层循环
  支持库引入 (交互支持库1.0)       — 引入交互支持库
  交互_输出 (内容, 文本|数字)      — 输出并等待用户回答（类型错误重输）
  交互_取终端反馈 ()              — 取回最近一次交互回答
  画矩形(x,y,w,h,颜色)            — 画填充矩形
  画文字(x,y,文本,颜色)           — 画文字
  画圆(x,y,r,颜色)                 — 画填充圆
  画线(x1,y1,x2,y2,颜色)          — 画线
  画点(x,y,颜色)                   — 画点
  清屏() / 刷新()                  — 清屏 / 刷新
  取宽() / 取高()                  — 取屏幕宽 / 高
  到文本(数) / 到整数(文本)        — 类型转换
  取随机数 (最小 到 最大)          — 生成范围内随机整数（易语言风格范围语法）
  绝对值 (数) / 求平方根 (数)       — 数学函数
  取文本长度 (文本)                 — 取字符串长度
  到大写 (文本) / 到小写 (文本)     — 字符串大小写转换
  延时 (毫秒)                       — 暂停指定毫秒数（易语言风格）
  信息框 (内容)                     — 弹出提示（模拟环境下输出）
  颜色: 黑/白/红/绿/蓝/黄/青/紫色
  全角符号（（），“”、、等）自动转半角，写代码不用切输入法。
"""

import sys
import os
import struct
import argparse

# === 操作码 (与 kernel/seedvm.h 一致) ===
OP_NOP        = 0x00
OP_HALT       = 0x01
OP_JMP        = 0x02
OP_JMPZ       = 0x03
OP_JMPNZ      = 0x04
OP_PUSH8      = 0x10
OP_PUSH32     = 0x11
OP_PUSHSTR    = 0x12
OP_POP        = 0x13
OP_DUP        = 0x14
OP_ADD        = 0x20
OP_SUB        = 0x21
OP_MUL        = 0x22
OP_DIV        = 0x23
OP_MOD        = 0x24
OP_NEG        = 0x25
OP_EQ         = 0x30
OP_NEQ        = 0x31
OP_LT         = 0x32
OP_GT         = 0x33
OP_LE         = 0x34
OP_GE         = 0x35
OP_AND        = 0x36
OP_OR         = 0x37
OP_NOT        = 0x38
OP_LOAD       = 0x40
OP_STORE      = 0x41
OP_PRINT_STR  = 0x50
OP_PRINT_NUM  = 0x51
OP_PRINT_NL   = 0x52
OP_PRINT_CH   = 0x53
OP_PRINT      = 0x54
OP_TOSTR      = 0x60
OP_TOINT      = 0x61
OP_CONCAT     = 0x70
OP_G_RECT     = 0x80
OP_G_TEXT     = 0x81
OP_G_CIRCLE   = 0x82
OP_G_CLEAR    = 0x83
OP_G_FLUSH    = 0x84
OP_G_WIDTH    = 0x85
OP_G_HEIGHT   = 0x86
OP_G_PIXEL    = 0x87
OP_G_LINE     = 0x88
OP_PUSH_COLOR = 0x90
OP_SLEEP      = 0x91
OP_ASK        = 0xA0
OP_GETINPUT   = 0xA1
OP_RAND       = 0xA2
OP_ABS        = 0xA3
OP_SQRT       = 0xA4
OP_STRLEN     = 0xA5
OP_UPPER      = 0xA6
OP_LOWER      = 0xA7

COLORS = {'黑色':0,'白色':1,'红色':2,'绿色':3,'蓝色':4,'黄色':5,'青色':6,'紫色':7}

SEED_MAGIC = 0x44454553
SEED_VERSION = 1
SEED_HDR_SIZE = 88

# === 中文关键词（按长度降序匹配） ===
# 易语言风格。旧语法（变量…为/如果…则…结束/循环 从/当…结束循环）已移除。
CN_KEYWORDS = sorted([
    '支持库引入','交互_取终端反馈','交互_输出',
    '计数循环','判断循环','标准循环',
    '跳出循环','循环结束','新建变量','如果结束',
    '画矩形','画文字','画圆','画线','画点',
    '到文本','到整数','取宽','取高','清屏','刷新',
    '取随机数','绝对值','求平方根','取文本长度','到大写','到小写',
    '延时','信息框',
    '输出','如果','否则',
    '真','假',
    '且','或','非',
    '小于','大于','等于','不等于','小于等于','大于等于',
], key=len, reverse=True)

CN_COLORS = list(COLORS.keys())

def is_cn(c):
    cp = ord(c)
    return 0x4E00 <= cp <= 0x9FFF

# ============================================================================
# 全角→半角规范化（易语言风格友好：直接用中文标点也能编译）
# ============================================================================
def normalize_src(s):
    out = []
    for c in s:
        cp = ord(c)
        # 全角 ASCII 范围 FF01–FF5E → 半角（覆盖（、）、，、＝、＜、＞、！、＋、－、＊、／、％、；、：、？…）
        if 0xFF01 <= cp <= 0xFF5E:
            out.append(chr(cp - 0xFEE0))
        else:
            out.append(c)
    s = ''.join(out)
    # 一般中文标点（非 FF01–FF5E）→ 半角
    repl = {
        '“': '"', '”': '"',       # 中文左右双引号 → "
        '‘': "'", '’': "'",       # 中文左右单引号 → '
        '，': ',',                  # ，
        '、': ',',                  # 、 顿号 → ,
        '（': '(', '）': ')',       # （）
        '≤': '<=', '≥': '>=',      # ≤ ≥
        '≠': '!=',                  # ≠
        '。': '.',                  # 。
        '【': '[', '】': ']',       # 【】
    }
    for k, v in repl.items():
        s = s.replace(k, v)
    return s

# ============================================================================
# 词法分析
# ============================================================================

class Tok:
    __slots__ = ('t','v','ln')
    def __init__(self, t, v, ln):
        self.t = t; self.v = v; self.ln = ln
    def __repr__(self):
        return f"Tok({self.t},{self.v!r},L{self.ln})"

def tokenize(src):
    src = normalize_src(src)   # 全角 → 半角（中文标点直接用也能编译）
    toks = []
    i, ln, n = 0, 1, len(src)
    while i < n:
        c = src[i]
        if c in ' \t\r': i += 1; continue
        if c == '\n': toks.append(Tok('NL','\n',ln)); ln += 1; i += 1; continue
        # 行注释
        if c == '/' and i+1 < n and src[i+1] == '/':
            while i < n and src[i] != '\n': i += 1
            continue
        # 字符串
        if c == '"':
            i += 1; s = []
            while i < n and src[i] != '"':
                if src[i] == '\\' and i+1 < n:
                    e = src[i+1]
                    s.append({'n':'\n','t':'\t','\\':'\\','"':'"'}.get(e, e))
                    i += 2
                else: s.append(src[i]); i += 1
            i += 1
            toks.append(Tok('STR', ''.join(s), ln)); continue
        # 数字
        if c.isdigit():
            j = i
            while j < n and src[j].isdigit(): j += 1
            toks.append(Tok('NUM', int(src[i:j]), ln)); i = j; continue
        # 括号/逗号
        if c == '(': toks.append(Tok('LP','(',ln)); i+=1; continue
        if c == ')': toks.append(Tok('RP',')',ln)); i+=1; continue
        if c == ',': toks.append(Tok('CM',',',ln)); i+=1; continue
        # 多字符运算符
        if i+1 < n and src[i:i+2] in ('==','!=','<=','>='):
            m = {'==':'EQ','!=':'NE','<=':'LE','>=':'GE'}
            toks.append(Tok('OP', m[src[i:i+2]], ln)); i += 2; continue
        # 单字符运算符
        single = {'<':'LT','>':'GT','=':'AS','+':'PL','-':'MI','*':'MU','/':'DV','%':'MO','!':'NT','.':'DOT'}
        if c in single:
            toks.append(Tok('OP', single[c], ln)); i += 1; continue
        # 中文关键词 / 颜色
        # 规则：关键词后若紧跟中文字符，则视为更长标识符的一部分，不匹配关键词。
        # （如 "当前" 不会被拆成关键词"当"+标识符"前"；"循环变量" 作为整体关键词优先匹配）
        if is_cn(c):
            matched = False
            for kw in CN_COLORS:
                if src[i:i+len(kw)] == kw:
                    nxt = i + len(kw)
                    if nxt < n and is_cn(src[nxt]): continue  # 更长标识符
                    toks.append(Tok('CLR', kw, ln)); i += len(kw); matched = True; break
            if matched: continue
            for kw in CN_KEYWORDS:  # 已按长度降序
                if src[i:i+len(kw)] == kw:
                    nxt = i + len(kw)
                    if nxt < n and is_cn(src[nxt]): continue  # 更长标识符
                    toks.append(Tok('KW', kw, ln)); i += len(kw); matched = True; break
            if matched: continue
            # 未匹配中文 → 标识符（读到关键词边界或非标识符字符）
            j = i
            while j < n and (is_cn(src[j]) or src[j].isalnum() or src[j] == '_'):
                # 检查是否到了关键词边界
                hit = False
                for kw in CN_KEYWORDS + CN_COLORS:
                    if src[j:j+len(kw)] == kw:
                        # 关键词后不能紧跟中文（同上规则），否则继续读标识符
                        nxt = j + len(kw)
                        if nxt < n and is_cn(src[nxt]): continue
                        hit = True; break
                if hit: break
                j += 1
            if j > i:
                toks.append(Tok('ID', src[i:j], ln)); i = j; continue
            toks.append(Tok('ID', c, ln)); i += 1; continue
        # ASCII 标识符
        if c.isalpha() or c == '_':
            j = i
            while j < n and (src[j].isalnum() or src[j] == '_' or is_cn(src[j])): j += 1
            toks.append(Tok('ID', src[i:j], ln)); i = j; continue
        raise SyntaxError(f"第{ln}行: 无法识别 '{c}' (U+{ord(c):04X})")
    toks.append(Tok('EOF', None, ln))
    return toks

# ============================================================================
# 编译器
# ============================================================================

class Compiler:
    def __init__(self):
        self.code = bytearray()
        self.data = bytearray()
        self.strings = {}
        self.vars = {}
        self.nvars = 0
        self.has_gui = False
        self._type = 'int'   # 当前表达式类型
        self._loop_var_idx = -1  # 当前循环计数器变量索引（供调试/扩展）
        self._loop_depth = 0     # 循环嵌套深度
        self._break_stack = []   # 每层循环的 跳出循环 回填偏移列表
        self.use_interactive = False  # 是否引入过交互支持库

    def add_str(self, s):
        if s in self.strings: return self.strings[s]
        idx = len(self.data)
        self.data.extend(s.encode('utf-8'))
        self.data.append(0)
        self.strings[s] = idx
        return idx

    def var(self, name):
        if name not in self.vars:
            self.vars[name] = self.nvars; self.nvars += 1
        return self.vars[name]

    def emit(self, *bs):
        for b in bs: self.code.append(b & 0xFF)

    def u16(self, v):
        self.code.append(v & 0xFF); self.code.append((v>>8) & 0xFF)

    def u32(self, v):
        self.code.extend(struct.pack('<I', v))

    def s16_hold(self, opcode):
        """发跳转操作码 + 预留 2 字节偏移，返回偏移位置用于回填。
        回填值 = target_ip - (hold_off + 2)。"""
        self.code.append(opcode)
        off = len(self.code)
        self.code.extend(b'\x00\x00')
        return off

    def s16_fix(self, off, val):
        self.code[off] = val & 0xFF
        self.code[off+1] = (val>>8) & 0xFF

    def ip(self): return len(self.code)

    # ---- 入口 ----
    def compile(self, toks):
        self.toks = toks; self.p = 0
        while self.tk().t != 'EOF':
            self.stmt()
        self.emit(OP_HALT)
        return self.code, self.data

    def tk(self, k=0):
        i = self.p + k
        return self.toks[i] if i < len(self.toks) else self.toks[-1]

    def adv(self):
        t = self.toks[self.p]
        if self.p < len(self.toks)-1: self.p += 1
        return t

    def skip_nl(self):
        while self.tk().t == 'NL': self.adv()

    def expect(self, t, v=None):
        tk = self.tk()
        if tk.t != t or (v is not None and tk.v != v):
            exp = f"{t}" + (f" '{v}'" if v else "")
            raise SyntaxError(f"第{tk.ln}行: 期望 {exp}, 得到 {tk.t} '{tk.v}'")
        return self.adv()

    # ---- 语句 ----
    def stmt(self):
        self.skip_nl()
        t = self.tk()
        if t.t == 'EOF': return
        if t.t == 'KW':
            dispatch = {
                '输出': self.stmt_print,
                '新建变量': self.stmt_var_new,
                '如果': self.stmt_if,
                '计数循环': self.stmt_loop,
                '判断循环': self.stmt_loop,
                '标准循环': self.stmt_loop,
                '跳出循环': self.stmt_break,
                '支持库引入': self.stmt_lib,
                '交互_输出': self.stmt_interact_out,
                '清屏': self.stmt_gui,
                '刷新': self.stmt_gui,
                '画矩形': self.stmt_gui, '画文字': self.stmt_gui,
                '画圆': self.stmt_gui, '画线': self.stmt_gui, '画点': self.stmt_gui,
                '延时': self.stmt_sleep,
                '信息框': self.stmt_msgbox,
            }
            fn = dispatch.get(t.v)
            if fn: fn()
            elif t.v in ('取宽','取高'):
                self.expr(); self.skip_nl()  # 表达式语句
            else:
                raise SyntaxError(f"第{t.ln}行: 未预期的关键词 '{t.v}'")
        elif t.t == 'ID':
            self.stmt_assign()
        else:
            raise SyntaxError(f"第{t.ln}行: 意外 token {t.t} '{t.v}'")
        self.skip_nl()

    def stmt_print(self):
        self.expect('KW','输出'); self.expect('LP')
        first = True
        while self.tk().t != 'RP':
            if not first: self.expect('CM')
            first = False
            self.expr()
            self.emit(OP_PRINT)  # 运行时类型检测
        self.expect('RP')
        self.emit(OP_PRINT_NL)

    def stmt_assign(self):
        nm = self.adv().v
        self.expect('OP','AS')
        self.expr()
        self.emit(OP_STORE, self.var(nm))

    def stmt_if(self):
        """如果 (条件) … 否则 … 如果结束"""
        self.expect('KW','如果')
        self.expect('LP')
        self.expr()
        self.expect('RP')
        self.skip_nl()
        jmpz = self.s16_hold(OP_JMPZ)
        while True:
            t = self.tk()
            if t.t == 'EOF': break
            if t.t == 'KW' and t.v in ('否则','如果结束'): break
            self.stmt()
        has_else = False
        jmp_end = None
        if self.tk().t == 'KW' and self.tk().v == '否则':
            jmp_end = self.s16_hold(OP_JMP)
            has_else = True
            self.s16_fix(jmpz, self.ip() - (jmpz + 2))
            self.adv()          # 否则
            self.skip_nl()
            while True:
                t = self.tk()
                if t.t == 'EOF': break
                if t.t == 'KW' and t.v == '如果结束': break
                self.stmt()
        if has_else:
            self.s16_fix(jmp_end, self.ip() - (jmp_end + 2))
        else:
            self.s16_fix(jmpz, self.ip() - (jmpz + 2))
        term = self.tk()
        if term.t == 'KW' and term.v == '如果结束':
            self.adv()
        else:
            raise SyntaxError(f"第{term.ln}行: '如果' 缺少 '如果结束'")

    def stmt_var_new(self):
        """易语言风格：新建变量 名 （默认 0），之后用  名 = 值  赋值"""
        self.expect('KW','新建变量')
        nm = self.adv()
        if nm.t != 'ID': raise SyntaxError(f"第{nm.ln}行: '新建变量' 后须为变量名")
        idx = self.var(nm.v)
        self.emit(OP_PUSH8, 0)
        self.emit(OP_STORE, idx)

    def stmt_loop(self):
        kw = self.tk().v
        if kw == '计数循环':
            # 计数循环 (次数, 计数变量) … 循环结束 ()
            self.adv()
            self.expect('LP')
            self.expr()                                  # 循环次数
            self._loop_depth += 1
            d = self._loop_depth
            cnt_idx = self.var(f'__cnt{d}__')
            self.emit(OP_STORE, cnt_idx)                 # 存次数
            self.expect('CM')
            nm = self.adv()
            if nm.t != 'ID': raise SyntaxError(f"第{nm.ln}行: '计数循环' 第二个参数须为变量名")
            counter_idx = self.var(nm.v)
            self.expect('RP')
            self.emit(OP_PUSH8, 1); self.emit(OP_STORE, counter_idx)  # 计数器 = 1
            self.skip_nl()
            start = self.ip()
            self.emit(OP_LOAD, counter_idx)
            self.emit(OP_LOAD, cnt_idx)
            self.emit(OP_LE)
            jmpz = self.s16_hold(OP_JMPZ)
            self._break_stack.append([])
            while True:
                t = self.tk()
                if t.t == 'EOF': break
                if t.t == 'KW' and t.v == '循环结束': break
                self.stmt()
            self._loop_depth -= 1
            self.emit(OP_LOAD, counter_idx)
            self.emit(OP_PUSH8, 1); self.emit(OP_ADD)
            self.emit(OP_STORE, counter_idx)
            hold = self.s16_hold(OP_JMP)
            self.s16_fix(hold, start - (hold + 2))
            exit_ip = self.ip()
            self.s16_fix(jmpz, exit_ip - (jmpz + 2))
            for off in self._break_stack.pop():
                self.s16_fix(off, exit_ip - (off + 2))
            self._expect_loop_end()
        elif kw == '判断循环':
            # 判断循环 (条件) … 循环结束 ()
            self.adv()
            self.expect('LP')
            start = self.ip()
            self.expr()
            jmpz = self.s16_hold(OP_JMPZ)
            self.expect('RP')
            self.skip_nl()
            self._break_stack.append([])
            while True:
                t = self.tk()
                if t.t == 'EOF': break
                if t.t == 'KW' and t.v == '循环结束': break
                self.stmt()
            hold = self.s16_hold(OP_JMP)
            self.s16_fix(hold, start - (hold + 2))
            exit_ip = self.ip()
            self.s16_fix(jmpz, exit_ip - (jmpz + 2))
            for off in self._break_stack.pop():
                self.s16_fix(off, exit_ip - (off + 2))
            self._expect_loop_end()
        elif kw == '标准循环':
            # 标准循环 (间隔毫秒, 计数变量) … 循环结束 ()  无限循环
            self.adv()
            self.expect('LP')
            self.expr()                                  # 间隔时间(毫秒)
            self._loop_depth += 1
            d = self._loop_depth
            iv_idx = self.var(f'__iv{d}__')
            self.emit(OP_STORE, iv_idx)
            self.expect('CM')
            nm = self.adv()
            if nm.t != 'ID': raise SyntaxError(f"第{nm.ln}行: '标准循环' 第二个参数须为变量名")
            counter_idx = self.var(nm.v)
            self.expect('RP')
            self.emit(OP_PUSH8, 0); self.emit(OP_STORE, counter_idx)  # 计数器 = 0
            self.skip_nl()
            start = self.ip()
            self._break_stack.append([])
            while True:
                t = self.tk()
                if t.t == 'EOF': break
                if t.t == 'KW' and t.v == '循环结束': break
                self.stmt()
            self._loop_depth -= 1
            self.emit(OP_LOAD, counter_idx)              # 计数器 += 1
            self.emit(OP_PUSH8, 1); self.emit(OP_ADD)
            self.emit(OP_STORE, counter_idx)
            self.emit(OP_LOAD, iv_idx)                   # 睡眠 间隔毫秒
            self.emit(OP_SLEEP)
            hold = self.s16_hold(OP_JMP)
            self.s16_fix(hold, start - (hold + 2))
            exit_ip = self.ip()
            for off in self._break_stack.pop():
                self.s16_fix(off, exit_ip - (off + 2))
            self._expect_loop_end()
        else:
            raise SyntaxError(f"第{self.tk().ln}行: 未预期的循环类型 '{kw}'")

    def _expect_loop_end(self):
        """循环终止符：循环结束 ()  （括号可省略）"""
        t = self.tk()
        if t.t == 'KW' and t.v == '循环结束':
            self.adv()
        else:
            raise SyntaxError(f"第{t.ln}行: 循环缺少 '循环结束'")
        if self.tk().t == 'LP':
            self.adv(); self.expect('RP')

    def stmt_break(self):
        """跳出循环 () — 跳出当前最内层循环"""
        ln = self.tk().ln
        self.expect('KW','跳出循环')
        self.expect('LP'); self.expect('RP')
        if not self._break_stack:
            raise SyntaxError(f"第{ln}行: '跳出循环' 只能在循环内使用")
        self._break_stack[-1].append(self.s16_hold(OP_JMP))

    def stmt_lib(self):
        """支持库引入 (交互支持库1.0)"""
        ln = self.tk().ln
        self.expect('KW','支持库引入')
        self.expect('LP')
        parts = []
        while self.tk().t != 'RP':
            parts.append(str(self.adv().v))
        self.expect('RP')
        text = ''.join(parts)
        if '交互支持库' not in text:
            raise SyntaxError(f"第{ln}行: 未知支持库 '{text}'")
        self.use_interactive = True

    def stmt_interact_out(self):
        """交互_输出 (内容, 回答类型) — 输出并等待回答；类型错误会重新询问。
        回答类型: 文本 / 数字(或 整数)。与 交互_取终端反馈() 配套。"""
        self.expect('KW','交互_输出')
        self.expect('LP')
        self.expr()                                      # 提示内容
        self.expect('CM')
        t = self.adv()
        if t.t != 'ID' or t.v not in ('文本','数字','整数'):
            raise SyntaxError(f"第{t.ln}行: '交互_输出' 第二参数须为 文本/数字/整数")
        self.expect('RP')
        typ = 0 if t.v == '文本' else 1
        self.emit(OP_ASK, typ)

    def stmt_gui(self):
        kw = self.adv().v
        self.has_gui = True
        if kw == '清屏': self.expect('LP'); self.expect('RP'); self.emit(OP_G_CLEAR); return
        if kw == '刷新': self.expect('LP'); self.expect('RP'); self.emit(OP_G_FLUSH); return
        self.expect('LP')
        nargs = 0
        while self.tk().t != 'RP':
            if nargs > 0: self.expect('CM')
            self.expr(); nargs += 1
        self.expect('RP')
        ops = {'画矩形':OP_G_RECT,'画文字':OP_G_TEXT,'画圆':OP_G_CIRCLE,
               '画线':OP_G_LINE,'画点':OP_G_PIXEL}
        self.emit(ops[kw])

    def stmt_sleep(self):
        """延时 (毫秒) — 易语言风格，暂停指定毫秒数"""
        self.expect('KW','延时')
        self.expect('LP')
        self.expr()
        self.expect('RP')
        self.emit(OP_SLEEP)

    def stmt_msgbox(self):
        """信息框 (内容) — 易语言风格，弹出提示（模拟环境下输出到控制台）"""
        self.expect('KW','信息框')
        self.expect('LP')
        first = True
        while self.tk().t != 'RP':
            if not first: self.expect('CM')
            first = False
            self.expr()
            self.emit(OP_PRINT)
        self.expect('RP')
        self.emit(OP_PRINT_NL)

    # ---- 表达式 ----
    def expr(self): self.p_or()

    def p_or(self):
        self.p_and()
        while self.tk().t == 'KW' and self.tk().v == '或':
            self.adv(); self.p_and(); self.emit(OP_OR); self._type = 'int'

    def p_and(self):
        self.p_not()
        while self.tk().t == 'KW' and self.tk().v == '且':
            self.adv(); self.p_not(); self.emit(OP_AND); self._type = 'int'

    def p_not(self):
        if self.tk().t == 'KW' and self.tk().v == '非':
            self.adv(); self.p_not(); self.emit(OP_NOT); self._type = 'int'
        elif self.tk().t == 'OP' and self.tk().v == 'NT':
            self.adv(); self.p_not(); self.emit(OP_NOT); self._type = 'int'
        else:
            self.p_cmp()

    def p_cmp(self):
        self.p_add()
        t = self.tk()
        cmp_map = {'LT':OP_LT,'GT':OP_GT,'EQ':OP_EQ,'NE':OP_NEQ,'LE':OP_LE,'GE':OP_GE}
        cn_cmp = {'小于':OP_LT,'大于':OP_GT,'等于':OP_EQ,'不等于':OP_NEQ,
                  '小于等于':OP_LE,'大于等于':OP_GE}
        if t.t == 'OP' and t.v in cmp_map:
            self.adv(); self.p_add(); self.emit(cmp_map[t.v]); self._type = 'int'
        elif t.t == 'KW' and t.v in cn_cmp:
            self.adv(); self.p_add(); self.emit(cn_cmp[t.v]); self._type = 'int'

    def p_add(self):
        self.p_mul()
        while self.tk().t == 'OP' and self.tk().v in ('PL','MI'):
            op = self.adv().v
            lt = self._type
            self.p_mul()
            if op == 'PL':
                if lt == 'str' or self._type == 'str':
                    self.emit(OP_CONCAT); self._type = 'str'
                else:
                    self.emit(OP_ADD); self._type = 'int'
            else:
                self.emit(OP_SUB); self._type = 'int'

    def p_mul(self):
        self.p_unary()
        while self.tk().t == 'OP' and self.tk().v in ('MU','DV','MO'):
            op = self.adv().v
            self.p_unary()
            self.emit({'MU':OP_MUL,'DV':OP_DIV,'MO':OP_MOD}[op])
            self._type = 'int'

    def p_unary(self):
        if self.tk().t == 'OP' and self.tk().v == 'MI':
            self.adv(); self.p_unary(); self.emit(OP_NEG); self._type = 'int'
        else:
            self.p_primary()

    def p_primary(self):
        t = self.tk()
        if t.t == 'NUM':
            self.adv()
            if 0 <= t.v <= 255: self.emit(OP_PUSH8, t.v)
            else: self.emit(OP_PUSH32); self.u32(t.v)
            self._type = 'int'; return
        if t.t == 'STR':
            self.adv()
            idx = self.add_str(t.v)
            self.emit(OP_PUSHSTR); self.u16(idx)
            self._type = 'str'; return
        if t.t == 'CLR':
            self.adv()
            self.emit(OP_PUSH_COLOR, COLORS[t.v])
            self._type = 'int'; return
        if t.t == 'KW':
            if t.v == '真': self.adv(); self.emit(OP_PUSH8,1); self._type='int'; return
            if t.v == '假': self.adv(); self.emit(OP_PUSH8,0); self._type='int'; return
            if t.v == '交互_取终端反馈':
                self.adv(); self.expect('LP'); self.expect('RP')
                self.emit(OP_GETINPUT); self._type = 'unknown'; return
            if t.v == '到文本':
                self.adv(); self.expect('LP'); self.expr(); self.expect('RP')
                self.emit(OP_TOSTR); self._type = 'str'; return
            if t.v == '到整数':
                self.adv(); self.expect('LP'); self.expr(); self.expect('RP')
                self.emit(OP_TOINT); self._type = 'int'; return
            if t.v in ('取宽','取高'):
                self.adv(); self.expect('LP'); self.expect('RP')
                self.emit(OP_G_WIDTH if t.v=='取宽' else OP_G_HEIGHT)
                self._type = 'int'; return
            if t.v == '取随机数':
                # 取随机数 (最小 到 最大) — 易语言风格范围语法
                self.adv(); self.expect('LP')
                self.expr()                              # 最小值
                to_tok = self.tk()
                if to_tok.v != '到':
                    raise SyntaxError(f"第{to_tok.ln}行: '取随机数' 范围须用 '到' 连接，如 取随机数(1 到 10)")
                self.adv()
                self.expr()                              # 最大值
                self.expect('RP')
                self.emit(OP_RAND)
                self._type = 'int'; return
            if t.v == '绝对值':
                self.adv(); self.expect('LP'); self.expr(); self.expect('RP')
                self.emit(OP_ABS); self._type = 'int'; return
            if t.v == '求平方根':
                self.adv(); self.expect('LP'); self.expr(); self.expect('RP')
                self.emit(OP_SQRT); self._type = 'int'; return
            if t.v == '取文本长度':
                self.adv(); self.expect('LP'); self.expr(); self.expect('RP')
                self.emit(OP_STRLEN); self._type = 'int'; return
            if t.v == '到大写':
                self.adv(); self.expect('LP'); self.expr(); self.expect('RP')
                self.emit(OP_UPPER); self._type = 'str'; return
            if t.v == '到小写':
                self.adv(); self.expect('LP'); self.expr(); self.expect('RP')
                self.emit(OP_LOWER); self._type = 'str'; return
        if t.t == 'ID':
            self.adv()
            self.emit(OP_LOAD, self.var(t.v))
            self._type = 'unknown'; return
        if t.t == 'LP':
            self.adv(); self.expr(); self.expect('RP'); return
        raise SyntaxError(f"第{t.ln}行: 意外 token {t.t} '{t.v}'")

# ============================================================================
# .seed 文件写入
# ============================================================================

def write_seed(path, name, author, code, data, has_gui):
    nb = name.encode('utf-8')[:31]
    ab = author.encode('utf-8')[:31]
    hdr = bytearray(SEED_HDR_SIZE)
    struct.pack_into('<I', hdr, 0, SEED_MAGIC)
    struct.pack_into('<H', hdr, 4, SEED_VERSION)
    struct.pack_into('<H', hdr, 6, 1 if has_gui else 0)
    hdr[8:8+len(nb)] = nb
    hdr[40:40+len(ab)] = ab
    struct.pack_into('<I', hdr, 72, len(code))
    struct.pack_into('<I', hdr, 76, len(data))
    struct.pack_into('<I', hdr, 80, 0)
    struct.pack_into('<I', hdr, 84, 0)
    with open(path, 'wb') as f:
        f.write(hdr); f.write(code); f.write(data)
    print(f"  → {path}")
    print(f"  代码 {len(code)}B  数据 {len(data)}B  总计 {SEED_HDR_SIZE+len(code)+len(data)}B")

# ============================================================================
# main
# ============================================================================

def main():
    ap = argparse.ArgumentParser(description='Terra 编译器')
    ap.add_argument('input', help='.terra 源文件')
    ap.add_argument('-o','--output', help='输出路径')
    ap.add_argument('-n','--name', help='包名')
    ap.add_argument('-a','--author', help='作者')
    args = ap.parse_args()

    with open(args.input, 'r', encoding='utf-8') as f:
        src = f.read()
    out = args.output or os.path.splitext(args.input)[0] + '.seed'
    name = args.name or os.path.splitext(os.path.basename(args.input))[0]
    author = args.author or 'SproutOS'

    print(f"编译: {args.input}")
    print(f"包名: {name} / 作者: {author}")

    toks = tokenize(src)
    print(f"词法: {len(toks)} token")

    c = Compiler()
    code, data = c.compile(toks)
    write_seed(out, name, author, code, data, c.has_gui)
    print(f"完成! {'含GUI' if c.has_gui else '纯文本'}")

if __name__ == '__main__':
    main()
