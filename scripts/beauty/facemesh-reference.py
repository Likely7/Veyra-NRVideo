"""Reference outputs of the MediaPipe face landmark model (468 points) for the GPU port.

For every face of the YuNet references (scripts/beauty/yunet-reference.py) this crops
the original picture the way FaceMeshGpu does (centre of the face box, a square
`--scale` times the box's long side, rotated so the eye line is level), runs the
model and writes, under <out>/<case>-<face>/:
  input.bin      the 192 x 192 x 3 network input, float32 NHWC, RGB 0..1 (sRGB values)
  output.bin     the 1404 raw landmark values (x, y, z in crop pixels) and the face flag logit, float32
  crop.txt       centre x y, side, cos sin of the crop in original-picture pixels
  landmarks.txt  the 468 points mapped back to the picture, x y per line
  overlay.png    the picture with the face contour, eyes, brows and lips drawn (for a look)
The network runs twice: OpenCV's TFLite importer (NCHW) and a small numpy executor
that follows TensorFlow's padding rules exactly; the script stops if they disagree.
Only public test pictures are used; nothing leaves the output folder.

usage: facemesh-reference.py <face_landmark.tflite> <yunet-ref-dir> <out-dir> [--scale 1.44] [stem ...]
(1.44 = FaceMeshGpu::kCropScale; the C++ tests read <yunet-ref-dir>/mesh)
"""
from pathlib import Path
import hashlib, json, math, struct, sys
import cv2
import numpy as np

SIZE = 192
# MediaPipe face_mesh_connections.py (v0.8.9, Apache-2.0): closed outlines, in order.
FACE_OVAL = [10, 338, 297, 332, 284, 251, 389, 356, 454, 323, 361, 288, 397, 365, 379, 378, 400, 377, 152, 148, 176, 149,
             150, 136, 172, 58, 132, 93, 234, 127, 162, 21, 54, 103, 67, 109]
LIPS = [61, 146, 91, 181, 84, 17, 314, 405, 321, 375, 291, 409, 270, 269, 267, 0, 37, 39, 40, 185]
LEFT_EYE = [263, 249, 390, 373, 374, 380, 381, 382, 362, 398, 384, 385, 386, 387, 388, 466]
RIGHT_EYE = [33, 7, 163, 144, 145, 153, 154, 155, 133, 173, 157, 158, 159, 160, 161, 246]
LEFT_BROW = [276, 283, 282, 295, 285, 336, 296, 334, 293, 300]
RIGHT_BROW = [46, 53, 52, 65, 55, 107, 66, 105, 63, 70]


class Tflite:
    """Just enough of the TFLite flatbuffer to run this model."""
    OPS = {0: 'ADD', 3: 'CONV_2D', 4: 'DEPTHWISE_CONV_2D', 6: 'DEQUANTIZE', 17: 'MAX_POOL_2D', 34: 'PAD', 54: 'PRELU'}

    def __init__(self, data):
        self.d = data
        root = self.table(self.ref(0))
        codes = []
        v, n = self.vec(root(1))
        for k in range(n):
            oc = self.table(self.ref(v + 4 * k))
            dep = self.d[oc(0)] if oc(0) else 0
            codes.append(self.i32(oc(3)) if oc(3) else dep)
        bv, _ = self.vec(root(4))
        sg = self.table(self.ref(self.vec(root(2))[0]))
        tv, tn = self.vec(sg(0))
        self.tensors = []
        for k in range(tn):
            t = self.table(self.ref(tv + 4 * k))
            shape = self.ints(t(0))
            kind = self.d[t(1)] if t(1) else 0
            buf = self.u32(t(2)) if t(2) else 0
            b = self.table(self.ref(bv + 4 * buf))
            raw = b''
            if b(0):
                s, ln = self.vec(b(0)); raw = self.d[s:s + ln]
            name = self.string(t(3)) if t(3) else ''
            self.tensors.append(dict(shape=shape, kind=kind, raw=raw, name=name))
        self.inputs, self.outputs = self.ints(sg(1)), self.ints(sg(2))
        self.ops = []
        ov, on = self.vec(sg(3))
        for k in range(on):
            op = self.table(self.ref(ov + 4 * k))
            name = self.OPS.get(codes[self.u32(op(0)) if op(0) else 0])
            if name is None:
                raise SystemExit('unsupported operator')
            opts = {}
            if op(4):
                bo = self.table(self.ref(op(4)))
                b8 = lambda i: self.d[bo(i)] if bo(i) else 0
                b32 = lambda i: self.i32(bo(i)) if bo(i) else 0
                if name == 'CONV_2D': opts = dict(same=b8(0) == 0, sw=b32(1), sh=b32(2), act=b8(3))
                elif name == 'DEPTHWISE_CONV_2D': opts = dict(same=b8(0) == 0, sw=b32(1), sh=b32(2), mult=b32(3), act=b8(4))
                elif name == 'MAX_POOL_2D': opts = dict(same=b8(0) == 0, sw=b32(1), sh=b32(2), fw=b32(3), fh=b32(4), act=b8(5))
                elif name == 'ADD': opts = dict(act=b8(0))
            self.ops.append(dict(op=name, inputs=self.ints(op(1)), outputs=self.ints(op(2)), **opts))

    def u32(self, o): return struct.unpack_from('<I', self.d, o)[0]
    def i32(self, o): return struct.unpack_from('<i', self.d, o)[0]
    def ref(self, o): return o + self.u32(o)
    def vec(self, o):
        v = self.ref(o); return v + 4, self.u32(v)
    def ints(self, o):
        if not o: return []
        s, n = self.vec(o); return [self.i32(s + 4 * j) for j in range(n)]
    def string(self, o):
        s, n = self.vec(o); return self.d[s:s + n].decode()
    def table(self, o):
        vt = o - self.i32(o)
        vlen = struct.unpack_from('<H', self.d, vt)[0]
        def field(i):
            off = 4 + 2 * i
            if off >= vlen: return None
            f = struct.unpack_from('<H', self.d, vt + off)[0]
            return o + f if f else None
        return field

    def constant(self, i):
        t = self.tensors[i]
        dtype = {0: '<f4', 1: '<f2', 2: '<i4'}[t['kind']]
        return np.frombuffer(t['raw'], dtype).astype(np.float32 if t['kind'] != 2 else np.int32).reshape(t['shape'])

    def run(self, x):
        values = {self.inputs[0]: x.astype(np.float32)}
        for i, t in enumerate(self.tensors):
            if t['raw']: values[i] = self.constant(i)
        for op in self.ops:
            a = [values[i] for i in op['inputs']]
            kind = op['op']
            if kind == 'DEQUANTIZE': y = a[0].astype(np.float32)
            elif kind == 'CONV_2D': y = conv(a[0], a[1], a[2], op['sh'], op['sw'], op['same'])
            elif kind == 'DEPTHWISE_CONV_2D': y = depthwise(a[0], a[1], a[2], op['sh'], op['sw'], op['same'])
            elif kind == 'PRELU': y = np.where(a[0] >= 0, a[0], a[0] * a[1].reshape(1, 1, 1, -1))
            elif kind == 'ADD': y = a[0] + a[1]
            elif kind == 'MAX_POOL_2D':
                n, h, w, c = a[0].shape
                y = a[0][:, :h // 2 * 2, :w // 2 * 2].reshape(n, h // 2, 2, w // 2, 2, c).max(axis=(2, 4))
            elif kind == 'PAD': y = np.pad(a[0], [tuple(p) for p in a[1]])
            if op.get('act'):
                raise SystemExit('fused activation not supported')
            values[op['outputs'][0]] = y.astype(np.float32)
        return [values[i] for i in self.outputs]


def same_pads(size, k, s):
    out = (size + s - 1) // s
    total = max((out - 1) * s + k - size, 0)
    return out, total // 2, total - total // 2


def conv(x, w, b, sh, sw, same):
    n, h, wd, c = x.shape
    o, kh, kw, _ = w.shape
    if same:
        oh, pt, pb = same_pads(h, kh, sh); ow, pl, pr = same_pads(wd, kw, sw)
    else:
        oh, ow, pt, pb, pl, pr = (h - kh) // sh + 1, (wd - kw) // sw + 1, 0, 0, 0, 0
    xp = np.pad(x, ((0, 0), (pt, pb), (pl, pr), (0, 0)))
    y = np.zeros((n, oh, ow, o), np.float64)
    for i in range(kh):
        for j in range(kw):
            patch = xp[:, i:i + sh * (oh - 1) + 1:sh, j:j + sw * (ow - 1) + 1:sw, :]
            y += np.tensordot(patch, w[:, i, j, :].astype(np.float64), axes=([3], [1]))
    return (y + b).astype(np.float32)


def depthwise(x, w, b, sh, sw, same):
    n, h, wd, c = x.shape
    _, kh, kw, _ = w.shape
    if same:
        oh, pt, pb = same_pads(h, kh, sh); ow, pl, pr = same_pads(wd, kw, sw)
    else:
        oh, ow, pt, pb, pl, pr = (h - kh) // sh + 1, (wd - kw) // sw + 1, 0, 0, 0, 0
    xp = np.pad(x, ((0, 0), (pt, pb), (pl, pr), (0, 0)))
    y = np.zeros((n, oh, ow, c), np.float64)
    for i in range(kh):
        for j in range(kw):
            y += xp[:, i:i + sh * (oh - 1) + 1:sh, j:j + sw * (ow - 1) + 1:sw, :] * w[0, i, j, :]
    return (y + b).astype(np.float32)


def read_image(path):
    data = np.fromfile(str(path), dtype=np.uint8)   # cv2.imread does not take non-ASCII paths on Windows
    image = cv2.imdecode(data, cv2.IMREAD_COLOR)
    if image is None:
        raise SystemExit(f'cannot decode {path}')
    return image


def crop_of(face, scale):
    """Crop centre, side and eye-line direction from a YuNet face (x y w h, right eye, left eye, ...)."""
    x, y, w, h = face[:4]
    ex, ey = face[6] - face[4], face[7] - face[5]
    if ex < 0: ex, ey = -ex, -ey
    length = math.hypot(ex, ey) or 1.0
    return x + w / 2, y + h / 2, scale * max(w, h), ex / length, ey / length


def crop_matrix(cx, cy, side, c, s):
    """Maps crop pixel indices (u, v) to picture pixel indices (OpenCV: pixel centres at integers)."""
    k = side / SIZE
    # continuous crop (u + 0.5) -> centred (u + 0.5 - 96) * k -> rotate -> + centre; minus 0.5 back to indices
    a, b = c * k, -s * k
    d, e = s * k, c * k
    tx = cx + a * (0.5 - SIZE / 2) + b * (0.5 - SIZE / 2) - 0.5
    ty = cy + d * (0.5 - SIZE / 2) + e * (0.5 - SIZE / 2) - 0.5
    return np.array([[a, b, tx], [d, e, ty]], np.float64)


def main():
    args = sys.argv[1:]
    scale = 1.44
    if '--scale' in args:
        at = args.index('--scale'); scale = float(args[at + 1]); del args[at:at + 2]
    model_path, refs, out = Path(args[0]), Path(args[1]), Path(args[2])
    stems = args[3:] or ['lena', 'astronaut', 'messi5', 'largest_selfie', 'gta-f028', 'gta-f040', 'gta-f044', 'gta-f052']
    model = model_path.read_bytes()
    net = Tflite(model)
    cvnet = cv2.dnn.readNetFromTFLite(np.frombuffer(model, np.uint8))
    out.mkdir(parents=True, exist_ok=True)
    summary = []
    for stem in stems:
        original_path = next((p for p in (refs / 'originals').glob(stem + '.*')), None)
        if original_path is None:
            raise SystemExit(f'no original for {stem}')
        picture = read_image(original_path)
        h0, w0 = picture.shape[:2]
        # The 640-long-side detection, scaled back to the original picture.
        case = next(p for p in refs.iterdir() if p.is_dir() and p.name.startswith(stem + '-') and max(map(int, p.name.rsplit('-', 1)[1].split('x'))) == 640)
        dw, dh = map(int, case.name.rsplit('-', 1)[1].split('x'))
        faces = json.loads((case / 'faces.json').read_text(encoding='utf8'))
        rgb = cv2.cvtColor(picture, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
        overlay = picture.copy()
        for index, face in enumerate(faces):
            f = [v * (w0 / dw if k % 2 == 0 else h0 / dh) for k, v in enumerate(face[:14])] + [face[14]]
            cx, cy, side, c, s = crop_of(f, scale)
            m = crop_matrix(cx, cy, side, c, s)
            tensor = cv2.warpAffine(rgb, m, (SIZE, SIZE), flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP, borderMode=cv2.BORDER_REPLICATE)
            tensor = np.ascontiguousarray(tensor, np.float32)
            landmarks, flag = net.run(tensor[None])
            cvnet.setInput(np.ascontiguousarray(tensor[None].transpose(0, 3, 1, 2)))
            cv_landmarks = cvnet.forward('conv2d_21').reshape(-1)
            cvnet.setInput(np.ascontiguousarray(tensor[None].transpose(0, 3, 1, 2)))
            cv_flag = float(cvnet.forward('conv2d_31').reshape(-1)[0])
            landmarks = landmarks.reshape(-1); flag = float(flag.reshape(-1)[0])
            agree = float(np.abs(landmarks - cv_landmarks).max())
            if agree > 1e-2 or abs(flag - cv_flag) > 1e-2:
                raise SystemExit(f'{stem} face {index}: numpy and OpenCV disagree ({agree}, {flag} vs {cv_flag})')
            folder = out / f'{stem}-{index}'
            folder.mkdir(exist_ok=True)
            tensor.astype('<f4').tofile(str(folder / 'input.bin'))
            np.concatenate([landmarks, [flag]]).astype('<f4').tofile(str(folder / 'output.bin'))
            (folder / 'crop.txt').write_text(f'{cx:.6f} {cy:.6f} {side:.6f} {c:.8f} {s:.8f}\n', encoding='utf8')
            points = landmarks.reshape(468, 3)[:, :2]
            k = side / SIZE
            centred = (points - SIZE / 2) * k
            image_points = np.stack([cx + c * centred[:, 0] - s * centred[:, 1], cy + s * centred[:, 0] + c * centred[:, 1]], 1)
            (folder / 'landmarks.txt').write_text(''.join(f'{x:.4f} {y:.4f}\n' for x, y in image_points), encoding='utf8')
            for loop, colour in ((FACE_OVAL, (0, 255, 0)), (LIPS, (0, 0, 255)), (LEFT_EYE, (255, 0, 0)), (RIGHT_EYE, (255, 0, 0)),
                                 (LEFT_BROW, (255, 255, 0)), (RIGHT_BROW, (255, 255, 0))):
                cv2.polylines(overlay, [np.round(image_points[loop] * 4).astype(np.int32)], True, colour, 1, cv2.LINE_AA, shift=2)
            # Where MediaPipe's tracking would put the next crop: the landmarks' box, 1.5x, square.
            lo, hi = image_points.min(0), image_points.max(0)
            tracked = 1.5 * max(hi - lo)
            summary.append(dict(case=f'{stem}-{index}', flag=flag, presence=1 / (1 + math.exp(-flag)), side=float(side),
                                landmarkSide=float(tracked), ratio=float(tracked / side), numpyVsOpencv=agree,
                                yunetBox=[float(f[2]), float(f[3])]))
            print(f'{stem}-{index}: flag {flag:+.2f} side {side:.1f} landmark-roi {tracked:.1f} ratio {tracked / side:.3f} agree {agree:.2e}', flush=True)
        ok, encoded = cv2.imencode('.png', overlay)
        encoded.tofile(str(out / f'{stem}-overlay.png'))
    meta = dict(model=model_path.name, modelSha256=hashlib.sha256(model).hexdigest(), opencv=cv2.__version__, scale=scale, faces=summary)
    (out / 'cases.json').write_text(json.dumps(meta, indent=1), encoding='utf8')


if __name__ == '__main__':
    main()
