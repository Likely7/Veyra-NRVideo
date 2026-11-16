# Generates qml/Veyra/logo-dusk.png, the in-app logo of the 日落 (dusk) palette, from
# the art in assets/veyra-logo-dusk.webp (a pixel gamepad, lavender -> pink -> peach,
# with its neon glow already in the alpha channel).
#
# A drop-in for logo.png: the same 3:2 canvas, with the gamepad's body in the same box
# the old shape fills (75 % of the width, centred), so every place that shows the logo
# keeps its layout; the glow fits in the margins. 720x480 = 2x logo.png for sharp
# downscaling. Resized with premultiplied alpha, so the glow gets no dark fringe.
import sys
from pathlib import Path
import numpy as np
from PIL import Image

SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('assets/veyra-logo-dusk.webp')
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else Path('qml/Veyra/logo-dusk.png')
OLD = Path('qml/Veyra/logo.png')

art = Image.open(SRC).convert('RGBA')
alpha = np.asarray(art)[..., 3]
ys, xs = np.where(alpha >= 240)                      # the solid body, not the glow
body = (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)

old = np.asarray(Image.open(OLD).convert('RGBA'))[..., 3]
oys, oxs = np.where(old > 128)
ow, oh = old.shape[1], old.shape[0]
share = (oxs.max() + 1 - oxs.min()) / ow             # the old shape's share of the canvas width
centre_y = (oys.min() + oys.max() + 1) / 2 / oh

width = (body[2] - body[0]) / share
height = width * oh / ow
cx = (body[0] + body[2]) / 2
cy = (body[1] + body[3]) / 2 + (0.5 - centre_y) * height
box = tuple(round(v) for v in (cx - width / 2, cy - height / 2, cx + width / 2, cy + height / 2))

canvas = Image.new('RGBa', (box[2] - box[0], box[3] - box[1]), (0, 0, 0, 0))
canvas.paste(art.convert('RGBa'), (-box[0], -box[1]))
out = canvas.resize((ow * 2, oh * 2), Image.LANCZOS).convert('RGBA')
out.save(OUT, optimize=True)
lost = alpha.astype(np.int64).sum() - np.asarray(art.crop(box))[..., 3].astype(np.int64).sum()
print(OUT, out.size, 'crop', box, 'body', body, 'alpha outside the crop', round(lost / alpha.astype(np.int64).sum() * 100, 2), '%')
