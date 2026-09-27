"""验证 SproutIDE.spec 的 datas 收集逻辑（PyInstaller 不可用时的 dry-run）。"""
import os
from glob import glob

IDE_DIR = os.path.abspath(os.path.dirname(__file__))
REPO_DIR = os.path.dirname(IDE_DIR)
SCRIPTS_DIR = os.path.join(REPO_DIR, "scripts")
SEED_EX_DIR = os.path.join(SCRIPTS_DIR, "seed-examples")

datas = []
for src in glob(os.path.join(SCRIPTS_DIR, "*.py")):
    datas.append((src, "scripts"))
for src in glob(os.path.join(SEED_EX_DIR, "*.terra")):
    datas.append((src, "scripts/seed-examples"))

print("IDE_DIR   =", IDE_DIR)
print("REPO_DIR  =", REPO_DIR)
print("SCRIPTS   =", SCRIPTS_DIR, " exists=", os.path.isdir(SCRIPTS_DIR))
print("SEED_EX   =", SEED_EX_DIR, " exists=", os.path.isdir(SEED_EX_DIR))
print(f"收集到的 datas ({len(datas)} 项):")
for s, d in datas:
    print(f"  ({s} , {d})")
print()
print("PyInstaller frozen 解压目录将含:")
print("  <_MEIPASS>\\scripts\\terrac.py        <- 后端编译器")
print("  <_MEIPASS>\\scripts\\seedvm_test.py   <- 后端模拟器")
print("  <_MEIPASS>\\scripts\\seed-examples\\*.terra   <- 示例")