# Generates qml/Veyra/backdrop-dusk.png and backdrop-dusk-strong.png, the window
# background of the 日落 (dusk) palette (设置 → 外观 → 界面配色).
#
# A twilight sky kept dark enough for the interface on top of it: deep periwinkle
# indigo at the top, violet in the middle, plum towards the bottom, with a soft
# peach-coral sunset glow low on the right, a magenta haze low on the left and a
# faint cool light at the top left. Colours are our own, chosen after sunset skies
# (periwinkle -> lilac -> pink -> peach); no artwork is used.
#
# Same method as make-backdrop.py: computed in float, one triangular grain dither,
# fixed seed, so the dark ramps do not band and the files are reproducible. The
# gradient is relative to the box, so one 1280x800 image stretched to the window
# keeps its shape. "strong" (背景渐变强度 = 中) lifts the glows and the ramp.
import sys
from pathlib import Path
import numpy as np
from PIL import Image

W, H = 1280, 800
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('qml/Veyra')
# Colourfulness of the result, keeping each pixel's luma (user, 2026-11-14: a little
# less saturated than the first version, which was 1.0).
SATURATION = 0.72

# Vertical sky ramp (position from the top, sRGB).
SKY = [(0.00, (0x15, 0x16, 0x35)),
       (0.42, (0x1B, 0x14, 0x35)),
       (0.78, (0x24, 0x13, 0x32)),
       (1.00, (0x2A, 0x13, 0x30))]
# Soft glows: centre (x, y) and radii as fractions of the box, colour, strength.
GLOWS = [((0.74, 1.04), (0.70, 0.46), (0xFF, 0x8E, 0x6E), 0.21),   # sunset, low right
         ((0.22, 1.00), (0.62, 0.34), (0xE0, 0x5C, 0xA8), 0.13),   # magenta haze, low left
         ((0.08, -0.06), (0.70, 0.52), (0x70, 0x88, 0xD8), 0.09)]  # cool sky light, top left


def render(strength):
    y, x = np.mgrid[0:H, 0:W].astype(np.float64) + 0.5
    t = y / H
    img = np.zeros((H, W, 3))
    for c in range(3):
        img[..., c] = np.interp(t, [s[0] for s in SKY], [s[1][c] for s in SKY])
    # The ramp itself is lifted a little in the strong variant.
    img = img * (1.0 + 0.18 * (strength - 1.0))
    for (cx, cy), (rx, ry), colour, amount in GLOWS:
        d2 = ((x / W - cx) / rx) ** 2 + ((y / H - cy) / ry) ** 2
        weight = np.exp(-2.2 * d2) * amount * strength
        for c in range(3):
            img[..., c] += (colour[c] - img[..., c]) * weight
    return img


for name, strength in (('backdrop-dusk.png', 1.0), ('backdrop-dusk-strong.png', 1.7)):
    rng = np.random.default_rng(20261114)
    img = render(strength)
    luma = img @ np.array([0.2126, 0.7152, 0.0722])
    img = luma[..., None] + (img - luma[..., None]) * SATURATION
    grain = rng.random((H, W)) + rng.random((H, W)) - 1.0   # triangular, -1..1
    img += 0.55 * grain[..., None]
    out = np.clip(np.round(img), 0, 255).astype(np.uint8)
    Image.fromarray(out, 'RGB').save(OUT / name, optimize=True)
    print(name, 'mean', out.reshape(-1, 3).mean(0).round(1), 'min', out.min(), 'max', out.reshape(-1, 3).max(0))
