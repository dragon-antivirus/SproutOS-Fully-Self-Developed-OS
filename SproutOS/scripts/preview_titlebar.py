from PIL import Image, ImageDraw

W, H = 420, 300
img = Image.new("RGB", (W, H), (248, 250, 252))
d = ImageDraw.Draw(img)

# window frame
d.rectangle([0, 0, W-1, H-1], outline=(100, 116, 139))
# title bar (title_color ~ blue rgb(59,130,246))
d.rectangle([0, 0, W-1, 21], fill=(59, 130, 246))

def outline(x, y, w, h, c):
    d.rectangle([x, y, x+w-1, y], fill=c)
    d.rectangle([x, y+h-1, x+w-1, y+h-1], fill=c)
    d.rectangle([x, y, x, y+h-1], fill=c)
    d.rectangle([x+w-1, y, x+w-1, y+h-1], fill=c)

my0 = 3
rx = W
# minimize
bx_min = rx - 56
d.rectangle([bx_min, my0, bx_min+15, my0+15], fill=(226, 232, 240))
d.rectangle([bx_min+3, my0+12, bx_min+12, my0+13], fill=(15, 23, 42))
# maximize
bx_max = rx - 38
d.rectangle([bx_max, my0, bx_max+15, my0+15], fill=(226, 232, 240))
d.rectangle([bx_max+3, my0+3, bx_max+12, my0+12], fill=(241, 245, 249))
outline(bx_max+3, my0+3, 10, 10, (15, 23, 42))
# close
bx_cls = rx - 20
d.rectangle([bx_cls, my0, bx_cls+15, my0+15], fill=(239, 68, 68))
# X glyph (diagonals)
for k in range(9):
    d.point((bx_cls+4+k, my0+2+k), fill=(255, 255, 255))
    d.point((bx_cls+12-k, my0+2+k), fill=(255, 255, 255))

# title text placeholder
d.text((8, 7), "Files", fill=(255, 255, 255))

# taskbar
ty = H - 28
d.rectangle([0, ty, W-1, H-1], fill=(30, 41, 59))
d.rectangle([110, ty+4, 183, ty+23], fill=(34, 197, 94))  # focused task button
d.text((116, ty+9), "Files", fill=(255, 255, 255))
d.rectangle([190, ty+4, 263, ty+23], fill=(51, 65, 85))
d.text((196, ty+9), "Editor", fill=(255, 255, 255))

img.save("D:/SproutOS/scripts/titlebar_preview.png")
print("ok", img.size)
