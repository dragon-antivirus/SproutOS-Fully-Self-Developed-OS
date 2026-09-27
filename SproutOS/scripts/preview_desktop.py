"""Render a mock of the new SproutOS desktop to verify vertical layout + CN labels."""
import os
from PIL import Image, ImageFont, ImageDraw

W, H = 1024, 768
BG = (15, 23, 42)
BG2 = (20, 30, 55)
TILE_BG = (241, 245, 249)
TILE_BORDER = (255, 255, 255)
LABEL_BG = (15, 23, 42)
TASKBAR_BG = (30, 41, 59)
LABEL_FG = (255, 255, 255)
WHITE = (255, 255, 255)
TEXT_LIGHT = (226, 232, 240)
TEXT_GREY = (148, 163, 184)
GREEN = (34, 197, 94)
SLATE = (100, 116, 139)

APPS = [
    ("Files",    "文件管理", (59,130,246)),
    ("Editor",   "编辑器",   (34,197,94)),
    ("Term",     "终端",     (168,85,247)),
    ("Calc",     "计算器",   (239,68,68)),
    ("Settings", "设置",     (249,115,22)),
    ("Paint",    "画图",     (14,165,233)),
    ("Clock",    "时钟",     (236,72,153)),
    ("About",    "关于",     (100,116,139)),
]

img = Image.new("RGB", (W, H), BG)
d = ImageDraw.Draw(img)

# Subtle vertical stripes background
for x in range(0, W, 60):
    d.rectangle([x, 0, x + 30, H], fill=BG2)
for y in range(0, H, 60):
    d.rectangle([0, y, W, y + 30], fill=BG2)

# Vertical icon column
for i, (en, cn, col) in enumerate(APPS):
    ax, ay = 16, 16 + i * 88
    d.rectangle([ax, ay, ax + 100, ay + 72], fill=TILE_BG)
    d.rectangle([ax + 1, ay + 1, ax + 99, ay + 71], fill=TILE_BORDER)
    d.rectangle([ax + 8, ay + 6, ax + 92, ay + 44], fill=col)
    d.rectangle([ax, ay + 54, ax + 100, ay + 72], fill=LABEL_BG)

# Render CN labels using SimHei
font_cn = ImageFont.truetype(r"C:\Windows\Fonts\simhei.ttf", 14)
for i, (en, cn, col) in enumerate(APPS):
    ax, ay = 16, 16 + i * 88
    # Center the label horizontally inside the tile width
    bbox = d.textbbox((0, 0), cn, font=font_cn)
    tw = bbox[2] - bbox[0]
    tx = ax + (100 - tw) // 2
    d.text((tx, ay + 57), cn, font=font_cn, fill=LABEL_FG)

# Taskbar
ty = H - 28
d.rectangle([0, ty, W, H], fill=TASKBAR_BG)
d.rectangle([0, ty, W, ty + 1], fill=(71, 85, 105))
font_ascii = ImageFont.truetype(r"C:\Windows\Fonts\simhei.ttf", 14)
d.text((22, ty + 9), "SproutOS", font=font_ascii, fill=TEXT_LIGHT)

img.save("D:/SproutOS/scripts/desktop_preview.png")
print("preview saved", img.size)