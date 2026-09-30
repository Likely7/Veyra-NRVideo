# Generates qml/Veyra/backdrop.png, the window background (G1.2), from app.css .vy:
#   background: radial-gradient(120% 90% at 12% 0%, #17171C 0%, #0C0C10 45%, #070709 100%)
#   .vy::after: fractal noise at opacity .045, mix-blend-mode overlay
# The gradient is relative to the box (ellipse radii 1.2 w and 0.9 h, centre 0.12 w, 0),
# so one image stretched to the window keeps the design's shape at any size.
# Qt's own gradients quantise to 8 bits and show rings on a 7..28 ramp; here the
# gradient is computed in float and dithered once, which is also what the grain does
# in the design (measured there: flat areas std 0.3..0.66 level, mean ~+0.9).
# Fixed seed, reproducible.
import sys
import numpy as np
from PIL import Image
W, H = 1280, 800
stops = [(0.0, (0x17, 0x17, 0x1C)), (0.45, (0x0C, 0x0C, 0x10)), (1.0, (0x07, 0x07, 0x09))]
y, x = np.mgrid[0:H, 0:W].astype(np.float64) + 0.5
r = np.sqrt(((x - 0.12 * W) / (1.2 * W)) ** 2 + (y / (0.9 * H)) ** 2)
r = np.clip(r, 0, 1)
img = np.zeros((H, W, 3))
for c in range(3):
    img[..., c] = np.interp(r, [s[0] for s in stops], [s[1][c] for s in stops])
rng = np.random.default_rng(20260926)
grain = rng.random((H, W)) + rng.random((H, W)) - 1.0      # triangular, -1..1
img += 0.9 + 0.55 * grain[..., None]
out = np.clip(np.round(img), 0, 255).astype(np.uint8)
Image.fromarray(out, 'RGB').save(sys.argv[1] if len(sys.argv) > 1 else 'qml/Veyra/backdrop.png', optimize=True)
