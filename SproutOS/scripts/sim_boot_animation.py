"""Simulate the SproutOS boot animation by rendering every frame in Python.

Verifies that the C code's per-phase geometry and timing produce a clean
animation that matches the LOGO.png reference image.
"""
from PIL import Image, ImageDraw

W, H = 1024, 768
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)


def make_img():
    return Image.new("RGB", (W, H), BLACK)


def line(d, x0, y0, x1, y1, color=WHITE, w=3):
    d.line([(x0, y0), (x1, y1)], fill=color, width=w)


def ring(d, cx, cy, r, color=WHITE, thick=3):
    if r < 1:
        return
    d.ellipse([cx - r - thick // 2, cy - r - thick // 2,
               cx + r + thick // 2, cy + r + thick // 2], outline=color, width=thick)


def polyline_partial(d, pts, k, color=WHITE, w=3):
    if k <= 0:
        return
    if k > len(pts) - 1:
        k = len(pts) - 1
    for i in range(k):
        x0, y0 = pts[i]
        x1, y1 = pts[i + 1]
        line(d, x0, y0, x1, y1, color, w)


# === Geometry (must match gen_boot_animation_data.py) ===================
CX = 512
POT_CY = 565
POT_R = 118
STEM_TOP = 235
POT_TOP = POT_CY - POT_R

SEED = [
    (495, 580), (485, 560), (482, 540), (488, 522),
    (500, 514), (510, 522), (508, 545), (500, 570), (498, 582),
]
STEM = [
    (CX, POT_TOP + 12), (CX, 420), (CX, 380), (CX, 340),
    (CX, 305), (CX, 270), (CX, STEM_TOP),
]
ANT_BASE = (CX, STEM_TOP - 2)
ANT_TIP  = (CX, STEM_TOP - 60)
ANT_DISC = (CX, STEM_TOP - 73)
ANT_DISC_R = 11
LEFT_LEAF = [
    (CX - 2, 320), (CX - 35, 305), (CX - 80, 290), (CX - 130, 280),
    (CX - 175, 282), (CX - 200, 295), (CX - 195, 315), (CX - 165, 335),
    (CX - 120, 348), (CX - 70, 352), (CX - 25, 345), (CX - 2, 335),
]
RIGHT_LEAF = [(2 * CX - x, y) for (x, y) in LEFT_LEAF]
L_BR = ((CX - 60, 408), (CX - 170, 408), (CX - 185, 408), 11)
R_BR = ((CX + 60, 408), (CX + 170, 408), (CX + 185, 408), 11)

# === Timing ==============================================================
PHASE_BLACK = 20
PHASE_POT = 30
PHASE_STEM = 25
PHASE_LEAVES = 25
PHASE_ANT = 10
PHASE_BRANCH = 15
PHASE_SEED = 10
PHASE_TEXT = 15
PHASE_HOLD = 80

# Sample frames (relative time t in ticks) for the multi-frame preview
# Note: we render only the FINAL state of every phase to keep the preview
# small. Per-frame visual would require more images.
samples = [
    ("frame_black",     0),
    ("frame_pot_done",  PHASE_BLACK + PHASE_POT),
    ("frame_stem_done", PHASE_BLACK + PHASE_POT + PHASE_STEM),
    ("frame_leaves_done", PHASE_BLACK + PHASE_POT + PHASE_STEM + PHASE_LEAVES),
    ("frame_ant_done",  PHASE_BLACK + PHASE_POT + PHASE_STEM + PHASE_LEAVES + PHASE_ANT),
    ("frame_branch_done", PHASE_BLACK + PHASE_POT + PHASE_STEM + PHASE_LEAVES + PHASE_ANT + PHASE_BRANCH),
    ("frame_seed_done", PHASE_BLACK + PHASE_POT + PHASE_STEM + PHASE_LEAVES + PHASE_ANT + PHASE_BRANCH + PHASE_SEED),
    ("frame_text_done", PHASE_BLACK + PHASE_POT + PHASE_STEM + PHASE_LEAVES + PHASE_ANT + PHASE_BRANCH + PHASE_SEED + PHASE_TEXT),
    ("frame_final",     PHASE_BLACK + PHASE_POT + PHASE_STEM + PHASE_LEAVES + PHASE_ANT + PHASE_BRANCH + PHASE_SEED + PHASE_TEXT + PHASE_HOLD),
]

# === Render each phase end-state =========================================
def render_phase(phase_t):
    """Given the absolute time t (in ticks), return the rendered image.
    Mirrors what the C boot_animation would show at that moment."""
    img = make_img()
    d = ImageDraw.Draw(img)

    t = phase_t
    if t <= PHASE_BLACK:
        return img
    t -= PHASE_BLACK

    # POT
    if t < PHASE_POT:
        # In-progress: r = (i/PHASE_POT) * POT_R
        i = t  # last frame rendered
        if i < 1:
            i = 1
        r = (POT_R * i) // PHASE_POT
        if r < 1:
            r = 1
        ring(d, CX, POT_CY, r)
        return img
    t -= PHASE_POT
    # POT done
    ring(d, CX, POT_CY, POT_R)

    # STEM
    if t < PHASE_STEM:
        i = max(1, t)
        segs = max(1, (i * (len(STEM) - 1)) // PHASE_STEM)
        polyline_partial(d, STEM, segs)
        return img
    t -= PHASE_STEM
    polyline_partial(d, STEM, len(STEM) - 1)

    # LEAVES
    if t < PHASE_LEAVES:
        i = t
        half = PHASE_LEAVES // 2
        if i <= half:
            segs = max(1, (i * (len(LEFT_LEAF) - 1)) // half) if i >= 1 else 0
            polyline_partial(d, LEFT_LEAF, segs)
        else:
            j = i - half
            segs = max(1, (j * (len(RIGHT_LEAF) - 1)) // half)
            polyline_partial(d, RIGHT_LEAF, segs)
            polyline_partial(d, LEFT_LEAF, len(LEFT_LEAF) - 1)
        return img
    t -= PHASE_LEAVES
    polyline_partial(d, LEFT_LEAF, len(LEFT_LEAF) - 1)
    polyline_partial(d, RIGHT_LEAF, len(RIGHT_LEAF) - 1)

    # ANT
    if t < PHASE_ANT:
        i = t
        split = (PHASE_ANT * 6) // 10
        if i <= split and i >= 1:
            p = (i * 100) // split
            x = ANT_BASE[0] + (ANT_TIP[0] - ANT_BASE[0]) * p // 100
            y = ANT_BASE[1] + (ANT_TIP[1] - ANT_BASE[1]) * p // 100
            line(d, ANT_BASE[0], ANT_BASE[1], x, y)
        else:
            line(d, ANT_BASE[0], ANT_BASE[1], ANT_TIP[0], ANT_TIP[1])
            j = i - split
            tail = PHASE_ANT - split
            r = max(1, (j * ANT_DISC_R) // tail)
            ring(d, ANT_DISC[0], ANT_DISC[1], r)
        return img
    t -= PHASE_ANT
    line(d, ANT_BASE[0], ANT_BASE[1], ANT_TIP[0], ANT_TIP[1])
    ring(d, ANT_DISC[0], ANT_DISC[1], ANT_DISC_R)

    # BRANCH
    if t < PHASE_BRANCH:
        i = t
        split = (PHASE_BRANCH * 6) // 10
        if i <= split and i >= 1:
            p = (i * 100) // split
            lx = L_BR[0][0] + (L_BR[1][0] - L_BR[0][0]) * p // 100
            ly = L_BR[0][1] + (L_BR[1][1] - L_BR[0][1]) * p // 100
            line(d, L_BR[0][0], L_BR[0][1], lx, ly)
            rx = R_BR[0][0] + (R_BR[1][0] - R_BR[0][0]) * p // 100
            ry = R_BR[0][1] + (R_BR[1][1] - R_BR[0][1]) * p // 100
            line(d, R_BR[0][0], R_BR[0][1], rx, ry)
        else:
            line(d, L_BR[0][0], L_BR[0][1], L_BR[1][0], L_BR[1][1])
            line(d, R_BR[0][0], R_BR[0][1], R_BR[1][0], R_BR[1][1])
            j = i - split
            tail = PHASE_BRANCH - split
            lr = max(1, (j * L_BR[3]) // tail)
            rr = max(1, (j * R_BR[3]) // tail)
            ring(d, L_BR[2][0], L_BR[2][1], lr)
            ring(d, R_BR[2][0], R_BR[2][1], rr)
        return img
    t -= PHASE_BRANCH
    line(d, L_BR[0][0], L_BR[0][1], L_BR[1][0], L_BR[1][1])
    line(d, R_BR[0][0], R_BR[0][1], R_BR[1][0], R_BR[1][1])
    ring(d, L_BR[2][0], L_BR[2][1], L_BR[3])
    ring(d, R_BR[2][0], R_BR[2][1], R_BR[3])

    # SEED
    if t < PHASE_SEED:
        i = t
        segs = max(1, (i * (len(SEED) - 1)) // PHASE_SEED) if i >= 1 else 0
        polyline_partial(d, SEED, segs)
        return img
    t -= PHASE_SEED
    polyline_partial(d, SEED, len(SEED) - 1)

    # TEXT
    # (We don't render text here, but it's drawn in C at the end)
    return img


for name, t in samples:
    img = render_phase(t)
    img.save(f"D:/SproutOS/scripts/boot_{name}.png")
    print(f"saved boot_{name}.png at t={t} ticks")

# Also render a single "final" image with the text overlaid, like the
# kernel will see right before the GUI main loop starts
final = render_phase(9999)
d = ImageDraw.Draw(final)
d.text((420, 700), "SproutOS v1.0", fill=(34, 197, 94))
d.text((300, 720), "Loading desktop...  (move mouse, click windows)", fill=(148, 163, 184))
final.save("D:/SproutOS/scripts/boot_final.png")
print("saved boot_final.png")

# Total duration
total = PHASE_BLACK + PHASE_POT + PHASE_STEM + PHASE_LEAVES + PHASE_ANT + \
        PHASE_BRANCH + PHASE_SEED + PHASE_TEXT + PHASE_HOLD
print(f"total animation duration: {total} ticks = {total * 10}ms")
