from PIL import Image
import struct, os

W, H = 1024, 768
img = Image.new("RGB", (W, H))

# 斜向渐变 + 顶部柔光，作为 SproutOS 默认壁纸示例
for y in range(H):
    for x in range(0, W, 4):
        t = (x / W + y / H) / 2.0
        r = int(15 + t * 60)
        g = int(40 + (1 - t) * 120)
        b = int(90 + t * 130)
        for dx in range(4):
            if x + dx < W:
                img.putpixel((x + dx, y), (r, g, b))

# 中心一个淡色圆（logo 占位）
cx, cy, rad = W // 2, H // 2, 140
for y in range(max(0, cy - rad), min(H, cy + rad)):
    for x in range(max(0, cx - rad), min(W, cx + rad)):
        if (x - cx) ** 2 + (y - cy) ** 2 <= rad * rad:
            r, g, b = img.getpixel((x, y))
            img.putpixel((x, y), (min(255, r + 40), min(255, g + 40), min(255, b + 40)))

# 保存为 24 位未压缩 BMP（与内核加载器要求一致：1024x768, 24/32bpp, 无压缩）
out = "D:/SproutOS/wallpaper/background.bmp"
img.save(out, "BMP")
print("saved", out, os.path.getsize(out), "bytes")

# 验证 BMP 头解析（复刻内核 loader 的解析逻辑）
with open(out, "rb") as f:
    raw = f.read()
assert raw[0:2] == b"BM", "not BMP"
off = struct.unpack("<I", raw[10:14])[0]
bw, bh = struct.unpack("<ii", raw[18:26])
bpp = struct.unpack("<H", raw[28:30])[0]
comp = struct.unpack("<I", raw[30:34])[0]
print("off=%d w=%d h=%d bpp=%d comp=%d" % (off, bw, bh, bpp, comp))
assert comp == 0 and bw == 1024 and bh == 768 and bpp == 24, "header mismatch"
print("BMP header OK -> kernel loader should accept this file")
