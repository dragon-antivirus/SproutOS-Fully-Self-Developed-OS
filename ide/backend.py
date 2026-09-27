#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SproutOS 原生 IDE 后端
复用仓库 scripts/ 里的 terrac.py (Terra 中文编译器) 与 seedvm_test.py (SeedVM 主机模拟器)，
作为单一事实来源。本文件只做"粘合"：动态加载这两个模块，并提供编译 / 运行 / 示例 / 工作区接口。

无任何第三方依赖（仅标准库）。
"""

import os
import sys
import io
import re
import struct
import shutil
import tempfile
import subprocess
import importlib.util
import contextlib

# ===== 路径解析（兼容源码运行 与 PyInstaller 打包后运行）=====
if getattr(sys, "frozen", False):
    # PyInstaller 单文件：资源解压到临时目录 sys._MEIPASS
    _MEI = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(sys.executable)))
    APP_DIR = _MEI
    REPO_DIR = _MEI
    SCRIPTS_DIR = os.path.join(_MEI, "scripts")
    # 打包后 ide/ 目录只读，工作区放到用户目录
    WORKSPACE = os.path.join(os.path.expanduser("~"), "SproutIDE_Workspace")
else:
    APP_DIR = os.path.dirname(os.path.abspath(__file__))
    REPO_DIR = os.path.dirname(APP_DIR)               # ide/ 在 SproutOS 仓库内
    SCRIPTS_DIR = os.path.join(REPO_DIR, "SproutOS", "scripts")
    WORKSPACE = os.path.join(APP_DIR, "workspace")
EXAMPLE_DIR = os.path.join(SCRIPTS_DIR, "seed-examples")
os.makedirs(WORKSPACE, exist_ok=True)
# 让 scripts/ 内的模块可被 importlib 正常加载（同目录相对 import 兜底）
if SCRIPTS_DIR not in sys.path:
    sys.path.insert(0, SCRIPTS_DIR)


def set_workspace(path):
    """动态设置工作空间目录。返回 (ok, message)。"""
    global WORKSPACE
    try:
        os.makedirs(path, exist_ok=True)
        WORKSPACE = path
        return True, f"工作空间已切换到: {path}"
    except OSError as e:
        return False, f"设置工作空间失败: {e}"


SEED_MAGIC = 0x44454553


# ===== 动态加载编译器与模拟器（单一事实来源）=====
def _load_module(path, name):
    if not os.path.isfile(path):
        # frozen 模式下脚本解压失败时给个友好提示
        if getattr(sys, "frozen", False):
            raise FileNotFoundError(
                f"打包后的 SproutIDE.exe 缺失后端文件：{os.path.basename(path)}\n"
                f"  查找路径: {path}\n"
                f"  这通常说明打包时 scripts/ 没被正确包含。\n"
                f"  请用 PyInstaller 重新打包（参考 ide/build_exe.bat），"
                f"或重新运行 pyinstaller --clean SproutIDE.spec"
            )
        raise FileNotFoundError(path)
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


_terrac = _load_module(os.path.join(SCRIPTS_DIR, "terrac.py"), "terrac")
_sim = _load_module(os.path.join(SCRIPTS_DIR, "seedvm_test.py"), "seedvm_test")


# ===== 构造 .seed 二进制包（与 terrac.write_seed 完全一致的 88 字节头）=====
def build_seed(name, author, code, data, has_gui):
    nb = name.encode("utf-8")[:31]
    ab = author.encode("utf-8")[:31]
    hdr = bytearray(88)
    struct.pack_into("<I", hdr, 0, SEED_MAGIC)
    struct.pack_into("<H", hdr, 4, 1)                       # version
    struct.pack_into("<H", hdr, 6, 1 if has_gui else 0)     # flags: has_gui
    hdr[8:8 + len(nb)] = nb
    hdr[40:40 + len(ab)] = ab
    struct.pack_into("<I", hdr, 72, len(code))
    struct.pack_into("<I", hdr, 76, len(data))
    struct.pack_into("<I", hdr, 80, 0)                      # entry
    struct.pack_into("<I", hdr, 84, 0)                      # reserved
    return bytes(hdr) + bytes(code) + bytes(data)


# ===== 编译 =====
def compile_source(src, name="main", author="SproutOS"):
    """编译 Terra 源码为 .seed 字节。返回 dict。"""
    try:
        toks = _terrac.tokenize(src)
        c = _terrac.Compiler()
        code, data = c.compile(toks)
    except SyntaxError as e:
        import re
        msg = str(e)
        ln = None
        m = re.search(r"第(\d+)行", msg)
        if m:
            ln = int(m.group(1))
        return {"ok": False, "error": msg, "line": ln,
                "token_count": None, "code_size": 0, "data_size": 0,
                "has_gui": False, "seed": None}
    blob = build_seed(name, author, code, data, c.has_gui)
    # 变量名 → 槽位（剔除非用户变量 __lv/__le/__ls），供变量监视面板使用
    user_vars = {nm: idx for nm, idx in c.vars.items() if not nm.startswith("__")}
    return {"ok": True, "error": None, "line": None,
            "token_count": len(toks), "code_size": len(code),
            "data_size": len(data), "has_gui": c.has_gui,
            "vars": user_vars, "seed": blob}


# ===== 运行（一次性，非交互）=====
def run_seed(blob, trace=False, var_names=None, max_steps=200000):
    tmp = tempfile.NamedTemporaryFile(suffix=".seed", delete=False)
    try:
        tmp.write(blob); tmp.flush(); tmp.close()
        vm = _sim.VM(tmp.name)
        vm.trace = trace
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            steps = vm.run(max_steps=max_steps)
        output = "".join(vm.out)
        log = buf.getvalue()
        if var_names:
            watch = {nm: vm.locals.get(idx, 0) for nm, idx in var_names.items()}
        else:
            watch = {f"L{k}": v for k, v in vm.locals.items()}
        return {"ok": True, "output": output, "steps": steps,
                "log": log, "watch": watch,
                "halted": vm.halted, "max_steps": steps >= max_steps}
    except Exception as e:  # noqa
        return {"ok": False, "output": "", "steps": 0,
                "log": "运行期错误: " + str(e), "watch": {},
                "halted": False, "max_steps": False}
    finally:
        try:
            os.unlink(tmp.name)
        except OSError:
            pass


# ===== 交互式运行会话（支持库：交互_输出 / 交互_取终端反馈）=====
def create_run_session(blob, trace, var_names, max_steps=200000):
    """从 .seed 字节创建可恢复 VM（临时文件读完即删，VM 内存持有 code/data）。"""
    tmp = tempfile.NamedTemporaryFile(suffix=".seed", delete=False)
    try:
        tmp.write(blob); tmp.flush(); tmp.close()
        vm = _sim.VM(tmp.name)
        vm.trace = trace
    finally:
        try:
            os.unlink(tmp.name)
        except OSError:
            pass
    return {"vm": vm, "vars": var_names, "log_buf": io.StringIO(),
            "max_steps": max_steps}


def advance_session(sess, inp=None, cancel=False):
    """推进一次会话中的 VM，返回统一响应（done / waiting / cancelled）。"""
    vm = sess["vm"]
    if cancel:
        vm.halted = True
        return {"status": "cancelled", "output": "".join(vm.out)}
    if inp is not None:
        vm.input_queue.append(str(inp))
    buf = sess["log_buf"]
    with contextlib.redirect_stdout(buf):
        steps = vm.run(max_steps=sess["max_steps"])
    log = buf.getvalue()
    if vm.waiting_input:
        return {"status": "waiting", "prompt": vm.ask_prompt,
                "type": "数字" if vm.ask_type == 1 else "文本",
                "output": "".join(vm.out), "steps": steps, "log": log}
    vars_map = sess.get("vars") or {}
    watch = {nm: vm.locals.get(idx, 0) for nm, idx in vars_map.items()} if vars_map \
        else {f"L{k}": v for k, v in vm.locals.items()}
    return {"status": "done", "output": "".join(vm.out), "steps": steps, "log": log,
            "watch": watch, "halted": vm.halted,
            "max_steps": steps >= sess["max_steps"]}


# ===== 示例 / 工作区 =====
def load_examples():
    items = []
    if os.path.isdir(EXAMPLE_DIR):
        for fn in sorted(os.listdir(EXAMPLE_DIR)):
            if fn.endswith(".terra"):
                try:
                    with open(os.path.join(EXAMPLE_DIR, fn), "r", encoding="utf-8") as f:
                        src = f.read()
                except OSError:
                    src = ""
                items.append({"name": fn[:-6], "source": src})
    return items


def list_workspace():
    return [fn[:-6] for fn in sorted(os.listdir(WORKSPACE)) if fn.endswith(".terra")]


def load_workspace_file(name):
    if not name.endswith(".terra"):
        name += ".terra"
    fp = os.path.join(WORKSPACE, name)
    if not os.path.isfile(fp):
        return None
    with open(fp, "r", encoding="utf-8") as f:
        return f.read()


def save_workspace_file(name, content):
    if not name:
        raise ValueError("文件名不能为空")
    if not name.endswith(".terra"):
        name += ".terra"
    fp = os.path.join(WORKSPACE, name)
    with open(fp, "w", encoding="utf-8") as f:
        f.write(content)
    return fp


def detect_language(path):
    ext = os.path.splitext(path)[1].lower()
    return {
        ".terra": "terra",
        ".seed": "seed",
        ".c": "c", ".h": "c",
        ".py": "python",
        ".md": "markdown", ".txt": "text",
        ".json": "json",
        ".bat": "text", ".sh": "text",
        ".html": "text", ".css": "text",
    }.get(ext, "text")


# ===== disk.img 部署（mtools，与 make_disk.sh 同一套）=====
def project_dir():
    """用于定位 disk.img / 运行 make 的项目根目录。打包后退回当前工作目录。"""
    if getattr(sys, "frozen", False):
        return os.getcwd()
    return REPO_DIR


def default_disk_img():
    """默认 disk.img 位置：项目根优先，其次当前目录。"""
    cands = []
    if not getattr(sys, "frozen", False):
        cands.append(os.path.join(REPO_DIR, "disk.img"))
    cands.append(os.path.join(os.getcwd(), "disk.img"))
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


def find_mtools():
    return bool(shutil.which("mcopy") and shutil.which("mmd"))


def deploy_seed_to_disk(img_path, seed_name, blob,
                        dest_dirs=("/APPS", "/GREENHOU")):
    """用 mtools 把 .seed 写入 FAT32 镜像指定目录。返回 (ok, message)。

    文件名用 8.3 大写（如 HELLO.SEED），与 make_disk.sh 一致，
    SproutOS 内核 fat32.c 对该格式兼容（也可识别 LFN）。
    """
    if not os.path.isfile(img_path):
        return False, ("找不到 disk.img：%s\n\n"
                       "请先生成镜像：\n"
                       "  make disk         （等价于 bash ./make_disk.sh）\n"
                       "或在「部署 → 设置 disk.img 路径」中指定已有镜像。"
                       % img_path)
    if not find_mtools():
        return False, ("未检测到 mtools（mcopy / mmd）。\n\n"
                       "Ubuntu:   sudo apt install mtools\n"
                       "Windows:  用 Chocolatey 安装 mtools 并加入 PATH，\n"
                       "          或在本机用 WSL / MSYS2 运行本 IDE。")
    # 8.3 短名：去除非字母数字、截断 8 字符、转大写（与 make_disk.sh 同规则）
    base = re.sub(r'[^A-Za-z0-9]', '', seed_name)[:8].upper() or "MAIN"
    short = base + ".SEED"
    tmp = tempfile.NamedTemporaryFile(suffix=".seed", delete=False)
    try:
        tmp.write(blob)
        tmp.flush()
        tmp.close()
        env = dict(os.environ)
        env["MTOOLS_NO_VFAT"] = "1"      # 写 8.3 短名（避免内核匹配不到别名）
        lines = []
        for d in dest_dirs:
            # 目录不存在则创建（已存在忽略错误）
            subprocess.run(["mmd", "-i", img_path, "::" + d],
                           env=env, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
            r = subprocess.run(
                ["mcopy", "-i", img_path, "-o", tmp.name,
                 "::" + d + "/" + short],
                env=env, capture_output=True, text=True)
            if r.returncode != 0:
                return False, "写入 %s/%s 失败：\n%s" % (d, short, r.stderr.strip())
            lines.append("  ✅ %s/%s" % (d, short))
        msg = ("已将 .seed 部署到 disk.img：\n" + "\n".join(lines) +
               "\n\n回到 SproutOS 项目目录执行  make run （或 make run-uefi），\n"
               "在桌面「花园(Garden)」/ 文件管理器 /APPS 中即可看到并运行。")
        return True, msg
    finally:
        try:
            os.unlink(tmp.name)
        except OSError:
            pass


if __name__ == "__main__":
    # 自测：编译 hello 并运行
    ex = load_examples()
    hello = next((e for e in ex if e["name"] == "hello"), None)
    if hello:
        r = compile_source(hello["source"], "hello")
        print("compile:", r["ok"], "code=", r["code_size"], "data=", r["data_size"],
              "gui=", r["has_gui"], "vars=", r["vars"])
        if r["ok"]:
            o = run_seed(r["seed"], trace=False)
            print("run steps:", o["steps"])
            print("output:", repr(o["output"]))
