"""Build a tile preview of the boot animation phase progression."""
from PIL import Image, ImageDraw

frames = [
    ("t=0  black",        "boot_frame_black.png"),
    ("t=0.5s  pot",       "boot_frame_pot_done.png"),
    ("t=0.75s  stem",     "boot_frame_stem_done.png"),
    ("t=1.0s  leaves",    "boot_frame_leaves_done.png"),
    ("t=1.1s  antenna",   "boot_frame_ant_done.png"),
    ("t=1.25s branches",  "boot_frame_branch_done.png"),
    ("t=1.35s seed",      "boot_frame_seed_done.png"),
    ("t=2.3s  final",     "boot_final.png"),
]

# Each frame is 1024x768. Downscale to 320x240 for a tile grid.
TILE_W, TILE_H = 384, 288
COLS = 4
ROWS = 2
PAD = 12
LABEL_H = 28

grid_w = COLS * TILE_W + (COLS + 1) * PAD
grid_h = ROWS * (TILE_H + LABEL_H) + (ROWS + 1) * PAD
grid = Image.new("RGB", (grid_w, grid_h), (24, 28, 44))
d = ImageDraw.Draw(grid)

for i, (label, fname) in enumerate(frames):
    src = Image.open(f"D:/SproutOS/scripts/{fname}").resize((TILE_W, TILE_H), Image.LANCZOS)
    col = i % COLS
    row = i // COLS
    x = PAD + col * (TILE_W + PAD)
    y = PAD + row * (TILE_H + LABEL_H + PAD)
    d.text((x, y), label, fill=(220, 220, 220))
    grid.paste(src, (x, y + LABEL_H))

grid.save("D:/SproutOS/scripts/boot_animation_tiles.png")
print(f"saved boot_animation_tiles.png ({grid.size})")
