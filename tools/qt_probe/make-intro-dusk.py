# Generates the light sources of the 日落 (dusk) startup intro (DuskIntro.qml), 2026-11-15.
# All procedural, nothing copied. The software scene graph has no shader effects, so
# every soft edge and glow is baked here and only moved, scaled and faded in QML.
#
#   intro-sun.png        a disc, pale gold -> peach -> coral -> magenta from top to bottom
#   intro-sun-glow.png   the halo around it; the canvas spans 3 sun radii (1.5 diameters)
#   intro-horizon.png    the horizon: a 2 px line with a pink halo, fading out at both ends
#   intro-grain.png      film grain, 256 px tiled: white and black specks of low alpha, so it
#                        lightens and darkens without a blend mode (the scene graph has none)
#   intro-vignette.png   the corners darkened, stretched over the window
#
# Colours are the sunset of the user's wordmark with their chroma x0.85 (OKLCH), so the
# light sits with the wordmark (whose chroma is x0.8) and the palette.
import sys
from pathlib import Path
import numpy as np
from PIL import Image

OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('qml/Veyra')
CHROMA = 0.85

M1 = np.array([[0.4122214708, 0.5363325363, 0.0514459929],
               [0.2119034982, 0.6806995451, 0.1073969566],
               [0.0883024619, 0.2817188376, 0.6299787005]])
M2 = np.array([[0.2104542553, 0.7936177850, -0.0040720468],
               [1.9779984951, -2.4285922050, 0.4505937099],
               [0.0259040371, 0.7827717662, -0.8086757660]])


def to_linear(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def to_srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * c ** (1 / 2.4) - 0.055)


def colour(hex_code):
    """Linear RGB of a hex colour after the chroma reduction."""
    srgb = np.array([int(hex_code[i:i + 2], 16) for i in (1, 3, 5)]) / 255
    lab = np.cbrt(to_linear(srgb) @ M1.T) @ M2.T
    lab[1:] *= CHROMA
    return np.clip(((lab @ np.linalg.inv(M2).T) ** 3) @ np.linalg.inv(M1).T, 0, 1)


def save(name, premultiplied):
    """premultiplied: H x W x 4 in linear light; stored unpremultiplied in sRGB."""
    a = np.clip(premultiplied[..., 3], 0, 1)
    rgb = premultiplied[..., :3] / np.maximum(a, 1e-6)[..., None]
    data = np.dstack([to_srgb(rgb), a])
    Image.fromarray(np.round(data * 255).astype(np.uint8), 'RGBA').save(OUT / name, optimize=True)


def layer(rgb, alpha):
    return np.dstack([alpha[..., None] * rgb, alpha])


# --- the sun ------------------------------------------------------------------
size, radius = 1024, 508
y, x = np.mgrid[0:size, 0:size] + 0.5
r = np.hypot(x - size / 2, y - size / 2)
edge = np.clip(radius - r + 0.5, 0, 1)
v = np.clip((y - (size / 2 - radius)) / (2 * radius), 0, 1)
stops = [(0.0, colour('#FFE9B0')), (0.35, colour('#FDC27E')), (0.68, colour('#F48A8F')), (1.0, colour('#D45C9E'))]
rgb = np.zeros((size, size, 3))
for (v0, c0), (v1, c1) in zip(stops, stops[1:]):
    t = np.clip((v - v0) / (v1 - v0), 0, 1)[..., None]
    inside = ((v >= v0) & (v <= v1))[..., None]
    rgb = np.where(inside, c0 + (c1 - c0) * t, rgb)
save('intro-sun.png', layer(rgb, edge))

# --- its glow: radius in sun radii; the canvas spans -1.5 .. 1.5 --------------
size = 512
y, x = np.mgrid[0:size, 0:size] + 0.5
r = np.hypot(x - size / 2, y - size / 2) / (size / 2) * 1.5
a = 0.62 * np.exp(-np.maximum(r - 0.9, 0) ** 2 / (2 * 0.18 ** 2)) + 0.16 * np.exp(-r ** 2 / (2 * 0.6 ** 2))
vert = np.clip((y / size)[..., None], 0, 1)
rgb = colour('#FBC08A') * (1 - vert) + colour('#EF7FA2') * vert
save('intro-sun-glow.png', layer(rgb, np.clip(a, 0, 1)))

# --- the horizon ----------------------------------------------------------------
w, h = 2048, 128
y, x = np.mgrid[0:h, 0:w] + 0.5
dy = y - h / 2
u = x / w
ends = np.sin(np.pi * u) ** 1.4
centre = 0.55 + 0.45 * np.sin(np.pi * u) ** 6
core = np.exp(-(dy / 1.1) ** 2) * ends * centre
halo = (0.38 * np.exp(-(dy / 9) ** 2) + 0.12 * np.exp(-(dy / 26) ** 2)) * ends
save('intro-horizon.png', layer(colour('#FFF1E2'), core) + layer(colour('#F49AB4'), halo))

# --- film grain: a fixed seed, so every build is the same -------------------------------
rng = np.random.default_rng(20261115)
n = rng.normal(0, 1, (256, 256))
a = np.clip(np.abs(n) * 0.22, 0, 0.6)
white = (n > 0)[..., None]
grain = np.dstack([np.where(white, 1.0, 0.0).repeat(3, axis=2), a])
Image.fromarray(np.round(grain * 255).astype(np.uint8), 'RGBA').save(OUT / 'intro-grain.png', optimize=True)

# --- vignette ------------------------------------------------------------------------------------
size = 512
y, x = np.mgrid[0:size, 0:size] + 0.5
r = np.hypot((x - size / 2) / (size / 2), (y - size / 2) / (size / 2)) / np.sqrt(2)
a = 0.62 * np.clip((r - 0.35) / 0.65, 0, 1) ** 1.6
Image.fromarray(np.round(np.dstack([np.zeros((size, size, 3)), a]) * 255).astype(np.uint8), 'RGBA').save(OUT / 'intro-vignette.png', optimize=True)

print('intro-sun.png, intro-sun-glow.png, intro-horizon.png, intro-grain.png, intro-vignette.png ->', OUT)
