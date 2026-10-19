"""Reference outputs of the YuNet face detector (OpenCV) for the GPU port.

For every image and detector size this writes, under <out>/<case>/:
  input.png      the picture at the detector size (INTER_AREA), 8-bit sRGB
  blob.bin       the network input OpenCV builds from it: float32 NCHW, BGR 0..255,
                 zero padded right / bottom to a multiple of 32
  <name>.bin     the twelve raw network outputs (cls/obj/bbox/kps x stride 8/16/32), float32
  faces.json     FaceDetectorYN detections (threshold 0.6, NMS 0.3) on input.png
  faces.txt      the same, one face per line (x y w h, 5 landmarks, score) for the C++ test
  meta.json      sizes, model hash, OpenCV version (meta.txt: width height paddedWidth paddedHeight)

Only public test pictures are used and nothing leaves the output folder; the
detector C++ test (veyra_beauty_detector_tests) compares against these files.

usage: yunet-reference.py <model.onnx> <out-dir> <image> [<image> ...]
"""
from pathlib import Path
import hashlib, json, sys

import cv2
import numpy as np

OUTPUTS = [f'{k}_{s}' for k in ('cls', 'obj', 'bbox', 'kps') for s in (8, 16, 32)]
SIZES = (320, 512, 640)


def read_image(path):
    # cv2.imread does not take non-ASCII paths on Windows.
    data = np.fromfile(str(path), dtype=np.uint8)
    image = cv2.imdecode(data, cv2.IMREAD_COLOR)
    if image is None:
        raise SystemExit(f'cannot decode {path}')
    return image


def fit(width, height, longest):
    scale = longest / max(width, height)
    return max(32, round(width * scale)), max(32, round(height * scale))


def main():
    model_path, out_dir, images = Path(sys.argv[1]), Path(sys.argv[2]), [Path(p) for p in sys.argv[3:]]
    model = model_path.read_bytes()
    model_sha = hashlib.sha256(model).hexdigest()
    net = cv2.dnn.readNetFromONNX(np.frombuffer(model, dtype=np.uint8))
    summary = []
    for image_path in images:
        original = read_image(image_path)
        h0, w0 = original.shape[:2]
        for longest in SIZES:
            w, h = fit(w0, h0, longest)
            case = f'{image_path.stem}-{w}x{h}'
            folder = out_dir / case
            folder.mkdir(parents=True, exist_ok=True)
            picture = cv2.resize(original, (w, h), interpolation=cv2.INTER_AREA)
            ok, encoded = cv2.imencode('.png', picture)
            assert ok
            encoded.tofile(str(folder / 'input.png'))
            pw, ph = (w + 31) // 32 * 32, (h + 31) // 32 * 32
            padded = np.zeros((ph, pw, 3), np.float32)
            padded[:h, :w] = picture.astype(np.float32)
            blob = np.ascontiguousarray(padded.transpose(2, 0, 1)[None])   # BGR, no normalisation
            blob.astype('<f4').tofile(str(folder / 'blob.bin'))
            net.setInput(blob)
            values = net.forward(OUTPUTS)
            shapes = {}
            for name, value in zip(OUTPUTS, values):
                value = np.asarray(value, np.float32)
                value.astype('<f4').tofile(str(folder / (name + '.bin')))
                shapes[name] = list(value.shape)
            detector = cv2.FaceDetectorYN.create('onnx', model, np.empty(0, np.uint8), (w, h), 0.6, 0.3, 5000)
            detector.setInputSize((w, h))
            _, faces = detector.detect(picture)
            rows = [] if faces is None else [[float(v) for v in row] for row in faces]
            (folder / 'faces.json').write_text(json.dumps(rows, indent=1), encoding='utf8')
            (folder / 'faces.txt').write_text(''.join(' '.join(f'{v:.6f}' for v in r) + '\n' for r in rows), encoding='utf8')
            (folder / 'meta.txt').write_text(f'{w} {h} {pw} {ph}\n', encoding='utf8')
            meta = dict(image=image_path.name, source=[w0, h0], size=[w, h], padded=[pw, ph], outputs=shapes,
                        model=model_path.name, modelSha256=model_sha, opencv=cv2.__version__, faces=len(rows))
            (folder / 'meta.json').write_text(json.dumps(meta, indent=1), encoding='utf8')
            summary.append(dict(case=case, faces=len(rows), best=max((r[14] for r in rows), default=0.0)))
            print(f'{case}: {len(rows)} faces', flush=True)
    (out_dir / 'cases.json').write_text(json.dumps(summary, indent=1), encoding='utf8')


if __name__ == '__main__':
    main()
