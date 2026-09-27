"""Generate a side-by-side preview: old grid vs new vertical CN layout."""
from PIL import Image

old = Image.open("D:/SproutOS/scripts/old_layout.png").convert("RGB") if __import__("os").path.exists("D:/SproutOS/scripts/old_layout.png") else None
new = Image.open("D:/SproutOS/scripts/desktop_preview.png").convert("RGB")

# Recreate the "old" layout for comparison (matches the screenshot the user showed)
W, H = 1024, 768
old_img = Image.new("RGB", (W, H), (15, 23, 42))
from PIL import ImageDraw, ImageFont
d = ImageDraw.Draw(old_img)
for x in range(0, W, 60):
    d.rectangle([x, 0, x + 30, H], fill=(20, 30, 55))
for y in range(0, H, 60):
    d.rectangle([0, y, W, y + 30], fill=(20, 30, 55))
cols = [(59,130,246),(34,197,94),(168,85,247),(239,68,68),(249,115,22),(14,165,233),(236,72,153),(100,116,139)]
font_ascii = ImageFont.truetype(r"C:\Windows\Fonts\simhei.ttf", 16)
for i in range(8):
    ax = 30 + (i % 4) * 90
    ay = 30 + (i // 4) * 100
    d.rectangle([ax, ay, ax + 70, ay + 70], fill=(255, 255, 255))
    d.rectangle([ax + 8, ay + 8, ax + 62, ay + 46], fill=cols[i])
    d.text((ax + 30, ay + 50), str(i + 1), font=font_ascii, fill=(15, 23, 42))
old_img.save("D:/SproutOS/scripts/old_layout.png")

# Compose side by side
side = Image.new("RGB", (W * 2 + 8, H), (0, 0, 0))
side.paste(old_img, (0, 0))
side.paste(new, (W + 8, 0))
side.save("D:/SproutOS/scripts/side_by_side.png")
print("side-by-side saved", side.size)