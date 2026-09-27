"""SproutOS boot animation: 1024x768 logo (sprout in a pot) drawn progressively.

The logo is the brand mark from LOGO.png:
  - A circle at the bottom (the "pot")
  - A small leaf-shaped seed curl inside
  - A central stem growing up
  - Two side leaves branching out
  - A short antenna at the top with a small circle
  - Two side branches with circles at their tips
  - Everything in white line-art on black

This script is just for design/visualization. It does NOT generate C code.
"""
from PIL import Image, ImageDraw
import math

W, H = 1024, 768
SCALE = 1  # render at native resolution

img = Image.new("RGB", (W, H), (0, 0, 0))
d = ImageDraw.Draw(img)

WHITE = (255, 255, 255)


def line(x0, y0, x1, y1, color=WHITE, w=2):
    d.line([(x0, y0), (x1, y1)], fill=color, width=w)


def circle_outline(cx, cy, r, color=WHITE, thick=2):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=color, width=thick)


def disc(cx, cy, r, color=WHITE):
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color)


def ring(cx, cy, r, color=WHITE, thick=2):
    r2 = r - thick // 2
    r1 = r + thick // 2
    d.ellipse([cx - r1, cy - r1, cx + r1, cy + r1], fill=color)
    d.ellipse([cx - r2, cy - r2, cx + r2, cy + r2], fill=(0, 0, 0))


# --- LOGO GEOMETRY (in 1024x768 space) --------------------------------
CX = 512          # horizontal center
POT_CY = 565      # center of pot circle
POT_R = 118       # pot circle radius
STEM_TOP = 235    # where the central stem ends / antenna starts
POT_TOP = POT_CY - POT_R  # = 447

# Inner seed (small leaf curl inside the pot) — a vertical leaf with a sharp tip at top
SEED = [
    (495, 580),  # bottom of seed
    (485, 560),
    (482, 540),
    (488, 522),
    (500, 514),
    (510, 522),
    (508, 545),
    (500, 570),
    (498, 582),
]

# Central stem (from inside the pot to antenna-base)
STEM = [
    (CX, POT_TOP + 12),  # bottom of stem (just inside pot)
    (CX, 420),
    (CX, 380),
    (CX, 340),
    (CX, 305),
    (CX, 270),
    (CX, STEM_TOP),     # top of stem
]

# Top antenna (vertical line + small circle on top)
ANT_BASE = (CX, STEM_TOP - 2)
ANT_TIP  = (CX, STEM_TOP - 60)
ANT_DISC = (CX, STEM_TOP - 73)

# Left leaf (bigger teardrop, attaches to stem at two points)
LEFT_LEAF = [
    (CX - 2, 320),  # attaches to stem (upper)
    (CX - 35, 305),
    (CX - 80, 290),
    (CX - 130, 280),
    (CX - 175, 282),
    (CX - 200, 295),
    (CX - 195, 315),
    (CX - 165, 335),
    (CX - 120, 348),
    (CX - 70, 352),
    (CX - 25, 345),
    (CX - 2, 335),  # back to stem (lower)
]

# Right leaf (mirror of left)
RIGHT_LEAF = [(2 * CX - x, y) for (x, y) in LEFT_LEAF]

# Left side branch (short horizontal line ending in a circle)
L_BRANCH_FROM = (CX - 60, 408)
L_BRANCH_TO   = (CX - 170, 408)
L_DISC        = (CX - 185, 408)

# Right side branch
R_BRANCH_FROM = (CX + 60, 408)
R_BRANCH_TO   = (CX + 170, 408)
R_DISC        = (CX + 185, 408)

# --- DRAW EVERYTHING (preview) -----------------------------------------
def draw_polyline(pts, color=WHITE, w=3):
    for i in range(len(pts) - 1):
        line(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], color, w)

# Pot
ring(CX, POT_CY, POT_R, WHITE, thick=4)

# Inner seed
draw_polyline(SEED, WHITE, w=3)

# Main stem
draw_polyline(STEM, WHITE, w=3)

# Leaves
draw_polyline(LEFT_LEAF, WHITE, w=3)
draw_polyline(RIGHT_LEAF, WHITE, w=3)

# Top antenna
line(ANT_BASE[0], ANT_BASE[1], ANT_TIP[0], ANT_TIP[1], WHITE, w=3)
ring(ANT_DISC[0], ANT_DISC[1], 11, WHITE, thick=3)

# Side branches
line(L_BRANCH_FROM[0], L_BRANCH_FROM[1], L_BRANCH_TO[0], L_BRANCH_TO[1], WHITE, w=3)
ring(L_DISC[0], L_DISC[1], 11, WHITE, thick=3)
line(R_BRANCH_FROM[0], R_BRANCH_FROM[1], R_BRANCH_TO[0], R_BRANCH_TO[1], WHITE, w=3)
ring(R_DISC[0], R_DISC[1], 11, WHITE, thick=3)

# "SproutOS v1.0" text below the logo
d.text((420, 695), "SproutOS v1.0", fill=WHITE)

img.save("D:/SproutOS/scripts/boot_logo_preview.png")
print("preview saved:", img.size)
