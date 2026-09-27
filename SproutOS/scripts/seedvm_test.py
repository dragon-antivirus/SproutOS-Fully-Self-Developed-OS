#!/usr/bin/env python3
"""seedvm_test.py — SeedVM 字节码模拟器（用于在主机上验证 .seed 包逻辑）"""

import struct
import sys
import time
import random

OP_NAMES = {0x00:'NOP',0x01:'HALT',0x02:'JMP',0x03:'JMPZ',0x04:'JMPNZ',
            0x10:'PUSH8',0x11:'PUSH32',0x12:'PUSHSTR',0x13:'POP',0x14:'DUP',
            0x20:'ADD',0x21:'SUB',0x22:'MUL',0x23:'DIV',0x24:'MOD',0x25:'NEG',
            0x30:'EQ',0x31:'NEQ',0x32:'LT',0x33:'GT',0x34:'LE',0x35:'GE',
            0x36:'AND',0x37:'OR',0x38:'NOT',
            0x40:'LOAD',0x41:'STORE',
            0x50:'PRINT_STR',0x51:'PRINT_NUM',0x52:'PRINT_NL',0x53:'PRINT_CH',0x54:'PRINT',
            0x60:'TOSTR',0x61:'TOINT',0x70:'CONCAT',
            0x80:'G_RECT',0x81:'G_TEXT',0x82:'G_CIRCLE',0x83:'G_CLEAR',0x84:'G_FLUSH',
            0x85:'G_WIDTH',0x86:'G_HEIGHT',0x87:'G_PIXEL',0x88:'G_LINE',
            0x90:'PUSH_COLOR',
            0x91:'SLEEP',
            0xA0:'ASK',0xA1:'GETINPUT',
            0xA2:'RAND',0xA3:'ABS',0xA4:'SQRT',0xA5:'STRLEN',
            0xA6:'UPPER',0xA7:'LOWER'}

COLORS = [0xFF000000,0xFFFFFFFF,0xFFE53935,0xFF4CAF50,0xFF2196F3,
          0xFFFFEB3B,0xFF00BCD4,0xFF9C27B0]

class VM:
    def __init__(self, path):
        with open(path,'rb') as f: blob = f.read()
        magic, ver, flags = struct.unpack_from('<IHH', blob, 0)
        assert magic == 0x44454553, "bad magic"
        self.name = blob[8:40].split(b'\0')[0].decode('utf-8',errors='replace')
        self.author = blob[40:72].split(b'\0')[0].decode('utf-8',errors='replace')
        cs, ds, entry, _ = struct.unpack_from('<IIII', blob, 72)
        self.code = blob[88:88+cs]
        self.data = blob[88+cs:88+cs+ds]
        self.ip = entry
        self.stack = []
        self.locals = {}
        self.heap = {}       # 模拟堆地址 → 字符串
        self.next_heap = 0x1800000
        self.out = []
        self.halted = False
        self.trace = '-t' in sys.argv

        # 交互输入（OP_ASK / OP_GETINPUT）
        self.input_queue = []    # 待回答队列（按顺序消费）
        self.waiting_input = False
        self.ask_type = 0        # 0=文本 1=数字
        self.ask_prompt = ""
        self.ask_printed = False # 提示是否已打印（等待期间只打一次）
        self.answer = None
        self.has_answer = False

    def alloc(self, s):
        addr = self.next_heap; self.next_heap += 64
        self.heap[addr] = s
        return addr

    def rd8(self):
        v = self.code[self.ip]; self.ip += 1; return v
    def rd16(self):
        v = struct.unpack_from('<H', self.code, self.ip)[0]; self.ip += 2; return v
    def rds16(self):
        v = struct.unpack_from('<h', self.code, self.ip)[0]; self.ip += 2; return v
    def rd32(self):
        v = struct.unpack_from('<I', self.code, self.ip)[0]; self.ip += 4; return v

    def pop(self):
        return self.stack.pop()
    def push(self, v):
        self.stack.append(v)

    def is_str(self, v):
        return v in self.heap

    def run(self, max_steps=100000):
        steps = 0
        while not self.halted and steps < max_steps:
            steps += 1
            ip0 = self.ip
            op = self.rd8()
            if self.trace:
                nm = OP_NAMES.get(op, f'?{op:02X}')
                print(f"  {ip0:4d} {nm:10s} stack={self.stack[-4:]}")
            if op == 0x01: self.halted = True
            elif op == 0x02:
                off = self.rds16(); self.ip = self.ip + off
            elif op == 0x03:
                off = self.rds16(); c = self.pop()
                if c == 0: self.ip = self.ip + off
            elif op == 0x04:
                off = self.rds16(); c = self.pop()
                if c != 0: self.ip = self.ip + off
            elif op == 0x10: self.push(self.rd8())  # 零扩展
            elif op == 0x11: self.push(self.rd32())
            elif op == 0x12:
                idx = self.rd16()
                end = self.data.index(b'\0', idx)
                s = self.data[idx:end].decode('utf-8')
                self.push(self.alloc(s))
            elif op == 0x13: self.pop()
            elif op == 0x14: self.push(self.stack[-1])
            elif op == 0x20:
                a=self.pop(); b=self.pop()
                if self.is_str(a) or self.is_str(b):
                    sa = self.heap.get(a,str(a)); sb = self.heap.get(b,str(b))
                    self.push(self.alloc(sb+sa))
                else: self.push(b+a)
            elif op == 0x21: a=self.pop(); b=self.pop(); self.push(b-a)
            elif op == 0x22: a=self.pop(); b=self.pop(); self.push(b*a)
            elif op == 0x23: a=self.pop(); b=self.pop(); self.push(b//a if a else 0)
            elif op == 0x24: a=self.pop(); b=self.pop(); self.push(b%a if a else 0)
            elif op == 0x25: self.push(-self.pop())
            elif op == 0x30: a=self.pop(); b=self.pop(); self.push(1 if b==a else 0)
            elif op == 0x31: a=self.pop(); b=self.pop(); self.push(1 if b!=a else 0)
            elif op == 0x32: a=self.pop(); b=self.pop(); self.push(1 if b<a else 0)
            elif op == 0x33: a=self.pop(); b=self.pop(); self.push(1 if b>a else 0)
            elif op == 0x34: a=self.pop(); b=self.pop(); self.push(1 if b<=a else 0)
            elif op == 0x35: a=self.pop(); b=self.pop(); self.push(1 if b>=a else 0)
            elif op == 0x36: a=self.pop(); b=self.pop(); self.push(1 if (b and a) else 0)
            elif op == 0x37: a=self.pop(); b=self.pop(); self.push(1 if (b or a) else 0)
            elif op == 0x38: self.push(1 if not self.pop() else 0)
            elif op == 0x40: self.push(self.locals.get(self.rd8(), 0))
            elif op == 0x41: self.locals[self.rd8()] = self.pop()
            elif op == 0x50: self.out.append(self.heap.get(self.pop(),'?'))
            elif op == 0x51: self.out.append(str(self.pop()))
            elif op == 0x52: self.out.append('\n')
            elif op == 0x53: self.out.append(chr(self.pop()))
            elif op == 0x54:
                v = self.pop()
                self.out.append(self.heap.get(v, str(v)))
            elif op == 0x60: self.push(self.alloc(str(self.pop())))
            elif op == 0x61:
                v = self.pop()
                s = self.heap.get(v, str(v))
                try: self.push(int(s))
                except: self.push(0)
            elif op == 0x70:
                a=self.pop(); b=self.pop()
                self.push(self.alloc(self.heap.get(b,'')+self.heap.get(a,'')))
            elif 0x80 <= op <= 0x88:
                # GUI ops: just note them
                if op == 0x85: self.push(1024)
                elif op == 0x86: self.push(768)
                else: pass  # pop args as needed
                if op == 0x80: [self.pop() for _ in range(5)]
                elif op == 0x81: [self.pop() for _ in range(4)]
                elif op == 0x82: [self.pop() for _ in range(4)]
                elif op == 0x87: [self.pop() for _ in range(3)]
                elif op == 0x88: [self.pop() for _ in range(5)]
            elif op == 0x90: self.push(COLORS[self.rd8()])
            elif op == 0x91:  # SLEEP: pop ms
                ms = self.pop()
                if ms and ms > 0:
                    time.sleep(min(ms, 60000) / 1000.0)
            elif op == 0xA0:  # ASK <类型byte>; 栈顶=提示串（编译器先压提示再发操作码）
                typ = self.rd8()
                prompt = self.stack[-1] if self.stack else 0
                prompt_str = self.heap.get(prompt, '')
                self.ask_type = typ
                self.ask_prompt = prompt_str
                if not self.ask_printed:        # 进入询问先打印提示（只打一次）
                    self.out.append(prompt_str)
                    self.ask_printed = True
                if not self.input_queue:
                    # 无输入：暂停等待（回退 IP，下次重新执行本指令）
                    self.waiting_input = True
                    self.ip = ip0
                    return steps
                raw = self.input_queue.pop(0)
                if typ == 1:  # 数字
                    try:
                        ans = int(str(raw).strip())
                    except ValueError:
                        self.out.append("（回答类型错误，请重新输入）\n")
                        self.waiting_input = True
                        self.ip = ip0
                        return steps
                else:
                    ans = str(raw)
                self.answer = ans
                self.has_answer = True
                self.ask_printed = False
                self.stack.pop()          # 弹出提示串
                self.out.append(str(ans) + "\n")
                self.waiting_input = False
            elif op == 0xA1:  # GETINPUT: 压入最近一次答案
                if self.has_answer:
                    v = self.answer
                    if isinstance(v, int) and not isinstance(v, bool):
                        self.push(v)
                    else:
                        self.push(self.alloc(str(v)))
                else:
                    self.push(0)
            elif op == 0xA2:  # RAND: 弹出最大、最小，生成 [min,max] 随机整数
                mx = self.pop(); mn = self.pop()
                try:
                    self.push(random.randint(int(mn), int(mx)))
                except (ValueError, TypeError):
                    self.push(0)
            elif op == 0xA3:  # ABS: 取绝对值
                v = self.pop()
                self.push(abs(v) if isinstance(v, (int, float)) else 0)
            elif op == 0xA4:  # SQRT: 求平方根（取整）
                v = self.pop()
                try:
                    self.push(int(v ** 0.5))
                except (TypeError, ValueError):
                    self.push(0)
            elif op == 0xA5:  # STRLEN: 取字符串长度
                v = self.pop()
                s = self.heap.get(v, str(v))
                self.push(len(s))
            elif op == 0xA6:  # UPPER: 转大写
                v = self.pop()
                s = self.heap.get(v, str(v))
                self.push(self.alloc(s.upper()))
            elif op == 0xA7:  # LOWER: 转小写
                v = self.pop()
                s = self.heap.get(v, str(v))
                self.push(self.alloc(s.lower()))
            else:
                print(f"UNKNOWN OP {op:02X} at {ip0}"); self.halted = True
        if steps >= max_steps:
            print("!! MAX STEPS EXCEEDED (infinite loop?)")
        return steps

if __name__ == '__main__':
    path = sys.argv[1]
    vm = VM(path)
    print(f"=== {vm.name} (by {vm.author}) ===")
    steps = vm.run()
    print(f"--- {steps} steps ---")
    print(''.join(vm.out))
