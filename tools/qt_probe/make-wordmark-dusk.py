# Generates the 日落 (dusk) home-page wordmark and the images that go with it, from the
# user's art in assets/veyra-wordmark-dusk.webp ("Veyra NR", the letters filled with a
# palm-tree sunset; transparent background, sRGB). 2026-11-15.
#
#   wordmark-dusk.png        the art with its chroma x0.8 in OKLCH, hue and lightness kept:
#                            the user asked for a little less saturation to suit the palette,
#                            the same factor the accent colours got. Cropped to the letters.
#   wordmark-dusk-glow.png   a soft glow of the same colours (premultiplied blur in linear
#                            light), for the intro's bloom and the home page's breathing glow.
#                            The software scene graph has no MultiEffect, so the blur is baked
#                            here. Its canvas is the wordmark's box grown by GLOW_PAD x the
#                            wordmark's width on every side, at half size.
#   wordmark-dusk-veyra.png, wordmark-dusk-n.png, wordmark-dusk-r.png
#                            the art as three paper layers for the intro's parallax (the user
#                            asked for the letters to move apart, 2026-11-15), on the wordmark's
#                            canvas: stacked N, R, Veyra in one place they give the wordmark back.
#                            The word "Veyra" ends at y 520 (of the 1254 px art), where it butts
#                            onto N and R, except its y's tail, which lies over both. The tail is
#                            found by flooding its smooth colour inside a hand-placed outline up to
#                            the image's edges; what it hid on N and R is filled per paper piece
#                            (a harmonic fill from that piece's own pixels: the purple band at the
#                            top of N, the palm scene below it, R). N and R meet at a vertical seam.
#
# The cut-out left a faint haze in the background (alpha 1/255 over the whole canvas and a
# few brighter specks above the letters): kept within 6 px of the letters, where it is their
# anti-aliasing and soft shadow, dropped elsewhere.
import sys
from pathlib import Path
import io
import numpy as np
from PIL import Image, ImageCms, ImageDraw
from scipy import ndimage, sparse
from scipy.sparse.linalg import spsolve

SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('assets/veyra-wordmark-dusk.webp')
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else Path('qml/Veyra')
CHROMA = 0.8
GLOW_PAD = 0.16
GLOW_SIGMA = 0.03     # of the wordmark width
GLOW_LIFT = 0.45      # the glow's lightness is lifted this share of the way to white
# The layers, in the 1254 px art's pixels.
ROW_CUT = 520.5       # "Veyra" above, N and R below
SEAM = 635.5          # N left, R right
BAND_BOTTOM = 660     # on N, the purple band ends here and the palm scene starts
TAIL = [(604, 492), (582.5, 530), (545, 600), (525, 635), (505, 670), (486.5, 716), (510, 712.5), (545, 707.5),
        (580, 700), (600, 690), (615, 675), (625, 660), (635, 640), (660, 600), (680, 565), (700, 520), (712, 492)]
TAIL_SEED = (640, 560)  # y, x

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


def lab_from_linear(lin):
    return np.cbrt(lin @ M1.T) @ M2.T


def linear_from_lab(lab):
    return ((lab @ np.linalg.inv(M2).T) ** 3) @ np.linalg.inv(M1).T


def save(path, srgb, alpha):
    a = np.round(np.clip(alpha, 0, 1) * 255)
    rgb = np.where((a > 0)[..., None], np.round(np.clip(srgb, 0, 1) * 255), 0)   # nothing kept under alpha 0
    data = np.dstack([rgb, a]).astype(np.uint8)
    Image.fromarray(data, 'RGBA').save(path, optimize=True)


def harmonic_fill(values, unknown, region):
    """Fills `unknown` pixels of `values` (H x W x C) by Laplace's equation, using only
    neighbours in the same `region` (labels): a piece's colour never leaks into another."""
    idx = -np.ones(unknown.shape, np.int64)
    ys, xs = np.nonzero(unknown)
    idx[ys, xs] = np.arange(len(ys))
    rows_, cols_, data = [], [], []
    rhs = np.zeros((len(ys), values.shape[2]))
    diag = np.zeros(len(ys))
    for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        ny, nx = ys + dy, xs + dx
        ok = (ny >= 0) & (ny < unknown.shape[0]) & (nx >= 0) & (nx < unknown.shape[1])
        ok[ok] &= region[ny[ok], nx[ok]] == region[ys[ok], xs[ok]]
        diag += ok
        inner = ok.copy()
        inner[ok] &= unknown[ny[ok], nx[ok]]
        rows_ += list(np.nonzero(inner)[0])
        cols_ += list(idx[ny[inner], nx[inner]])
        data += [-1.0] * int(inner.sum())
        known = ok & ~inner
        rhs[known] += values[ny[known], nx[known]]
    assert diag.min() > 0
    a = sparse.csr_matrix((data + list(diag), (rows_ + list(range(len(ys))), cols_ + list(range(len(ys))))), shape=(len(ys), len(ys)))
    out = values.copy()
    for c in range(values.shape[2]):
        out[ys, xs, c] = spsolve(a.tocsc(), rhs[:, c])
    return out


art = Image.open(SRC)
profile = art.info.get('icc_profile')
if profile:
    name = ImageCms.getProfileDescription(ImageCms.ImageCmsProfile(io.BytesIO(profile))).strip()
    assert 'sRGB' in name, 'expected an sRGB image, got ' + name
rgba = np.asarray(art.convert('RGBA')).astype(np.float64) / 255
alpha = rgba[..., 3].copy()

far = ndimage.distance_transform_edt(alpha < 0.5) > 6
haze = far & (alpha > 0) & (alpha < 16 / 255)
alpha[haze] = 0

lab = lab_from_linear(to_linear(rgba[..., :3]))
lab[..., 1:] *= CHROMA
linear = np.clip(linear_from_lab(lab), 0, 1)

ys, xs = np.nonzero(alpha > 0)
x0, y0, x1, y1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
crop = (slice(y0, y1), slice(x0, x1))
w, h = x1 - x0, y1 - y0
srgb = to_srgb(linear)
save(OUT / 'wordmark-dusk.png', srgb[crop], alpha[crop])

# --- the glow: blur the premultiplied colour and the coverage, divide, lift the lightness ---
pad = round(GLOW_PAD * w)
premul = np.zeros((h + 2 * pad, w + 2 * pad, 4))
premul[pad:pad + h, pad:pad + w, :3] = linear[crop] * alpha[crop][..., None]
premul[pad:pad + h, pad:pad + w, 3] = alpha[crop]
blurred = np.dstack([ndimage.gaussian_filter(premul[..., c], GLOW_SIGMA * w) for c in range(4)])
cover = blurred[..., 3]
glab = lab_from_linear(np.clip(blurred[..., :3] / np.maximum(cover, 1e-6)[..., None], 0, 1))
glab[..., 0] = glab[..., 0] + (1 - glab[..., 0]) * GLOW_LIFT
glow = np.dstack([to_srgb(np.clip(linear_from_lab(glab), 0, 1)), np.clip(cover * 1.15, 0, 1)])
glow_image = Image.fromarray(np.round(glow * 255).astype(np.uint8), 'RGBA')
# Halve with premultiplied alpha so the soft edge gets no dark fringe.
glow_image = glow_image.convert('RGBa').resize((glow_image.width // 2, glow_image.height // 2), Image.LANCZOS).convert('RGBA')
glow_image.save(OUT / 'wordmark-dusk-glow.png', optimize=True)

# --- the parallax layers ------------------------------------------------------------------
full_h, full_w = alpha.shape
rows = np.arange(full_h)[:, None]
cols = np.arange(full_w)[None, :]
outline = Image.new('L', (full_w, full_h), 0)
ImageDraw.Draw(outline).polygon(TAIL, fill=255)
outline = np.asarray(outline) > 0
smooth = ndimage.gaussian_filter(rgba[..., :3] * 255, (1.0, 1.0, 0))
edges = np.sqrt(sum(ndimage.sobel(smooth[..., c], ax) ** 2 for c in range(3) for ax in (0, 1)))
free, _ = ndimage.label(ndimage.binary_dilation(outline, iterations=8) & (rows > 505) & (edges < 60))
tail = ndimage.binary_fill_holes(free == free[TAIL_SEED])
# 4 px more: its light rim and the contact shadow at its edge go with it, so the fill below
# starts from the pieces' clean colour.
tail = ndimage.binary_dilation(tail, iterations=4) & (rows > ROW_CUT)
assert 19000 < tail.sum() < 28000, int(tail.sum())
rim = tail & ~ndimage.binary_erosion(tail)

below = (rows > ROW_CUT) & np.ones((1, full_w), bool)
# Paper pieces of the bottom row: 1 the purple band on N, 2 the palm scene on N, 3 R.
piece = np.where(cols < SEAM, np.where(rows < BAND_BOTTOM, 1, 2), 3) * below
hidden = tail & (piece > 0)
filled = harmonic_fill(np.dstack([srgb, alpha[..., None]]), hidden, piece)
bottom_rgb, bottom_a = filled[..., :3], np.clip(filled[..., 3], 0, 1)

veyra_a = np.where(rows <= ROW_CUT, alpha, 0)
veyra_a = np.where(tail, np.where(rim, 0.5, 1.0) * alpha, veyra_a)
n_a = np.where(below & (cols < SEAM), bottom_a, 0)
r_a = np.where(below & (cols >= SEAM), bottom_a, 0)
save(OUT / 'wordmark-dusk-n.png', bottom_rgb[crop], n_a[crop])
save(OUT / 'wordmark-dusk-r.png', bottom_rgb[crop], r_a[crop])
save(OUT / 'wordmark-dusk-veyra.png', srgb[crop], veyra_a[crop])


# Stacked in place the layers must give the wordmark back (only the tail's rim may differ).
def over(top_rgb, top_a, under_rgb, under_a):
    out_a = top_a + under_a * (1 - top_a)
    out = (top_rgb * top_a[..., None] + under_rgb * (under_a * (1 - top_a))[..., None]) / np.maximum(out_a, 1e-6)[..., None]
    return out, out_a


def stored(v):
    return np.round(np.clip(v, 0, 1) * 255) / 255


stack, stack_a = over(stored(srgb), stored(veyra_a), *over(stored(bottom_rgb), stored(r_a), stored(bottom_rgb), stored(n_a)))
diff = np.abs(stack * stack_a[..., None] - stored(srgb) * stored(alpha)[..., None]).max(axis=2) * 255
print(f'wordmark {w}x{h} (crop {x0},{y0},{x1},{y1}; aspect {w / h:.4f}), haze pixels dropped {int(haze.sum())}, '
      f'glow {glow_image.width}x{glow_image.height} pad {pad} px = {pad / w:.3f} of the width; '
      f'layers: tail {int(tail.sum())} px, refilled {int(hidden.sum())} px, '
      f'stacked vs wordmark max {diff.max():.1f}/255 (off the rim {diff[~rim].max():.1f}), mean {diff.mean():.4f}')
