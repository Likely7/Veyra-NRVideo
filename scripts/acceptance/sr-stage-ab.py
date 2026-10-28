"""Three-step SR picture A/B with RTX Video SR (claude-sr-stage-20261028).

Every picture goes through the real graph (veyra_sr_quality_tests stage-image in the
task's staged copy, video-file semantics: a 4K source at a 4K target gets the existing
native-size restore). Lossless PNG in and out.

  --mode blurry --image X   no reference: X is scaled to 3840x2160 (bicubic) as a soft
                            4K, then single step vs three-step. Sharpness proxy only.
  --mode native --image X   X is a native 3840x2160 reference. Two degraded sources are
                            made from it (soft 4K: area 1920x1080, JPEG q70, bicubic back
                            to 4K; native 2K: Lanczos 2560x1440) and every result is
                            compared with the reference (PSNR / SSIM on luma).
Crops (100 %) of the busiest regions go side by side into one PNG per region.
usage: py -3.11 -X utf8 -B -I scripts/acceptance/sr-stage-ab.py --mode blurry|native --image PATH [--tag NAME]
"""
from pathlib import Path
import argparse, io, json, os, re, subprocess
import numpy as np
from PIL import Image, ImageDraw

B = Path('E:/项目/Veyra'); T = 'claude-sr-stage-20261028'
A = B/'tests'/T/'app'; TMP = B/'tmp'/T/'ab'
ap = argparse.ArgumentParser(); ap.add_argument('--mode', choices=['blurry', 'native'], required=True)
ap.add_argument('--image', required=True); ap.add_argument('--tag', default='')
args = ap.parse_args()
tag = args.tag or args.mode
OUT = B/'reports'/T/'ab'/tag; WORK = B/'tests'/T/'ab'/tag
for p in (OUT, WORK, TMP): p.mkdir(parents=True, exist_ok=True)
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
env.update(TEMP=str(TMP), TMP=str(TMP))


def render(src, name, quality, prescale='off', target='4k', output='target'):
    """One picture through the graph; returns (array, gpu ms of the SR stage, sizes)."""
    run = subprocess.run([str(A/'veyra_sr_quality_tests.exe'), str(WORK/'gpu'), 'stage-image', str(src), name, str(quality), prescale, target, output, 'video'],
                         cwd=A, env=env, capture_output=True, text=True, timeout=120)
    line = next((l for l in run.stdout.splitlines() if l.startswith('STAGE_IMAGE')), '')
    assert run.returncode == 0 and 'passed=1' in line, (name, run.returncode, line, run.stdout[-500:])
    fields = dict(kv.split('=', 1) for kv in line.split()[1:])
    return np.asarray(Image.open(WORK/'gpu'/(name + '.png')).convert('RGB')), float(fields['srMs']), fields


def luma(a):
    a = a.astype(np.float64) / 255.0
    return 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]


def psnr(a, b):
    mse = np.mean((luma(a) - luma(b)) ** 2)
    return float('inf') if mse == 0 else 10 * np.log10(1.0 / mse)


def box(x, r=4):
    # Mean over (2r+1)^2 windows via an integral image (SSIM's local statistics).
    c = np.pad(x, r + 1, mode='edge').cumsum(0).cumsum(1); n = 2 * r + 1
    return (c[n:, n:] - c[:-n, n:] - c[n:, :-n] + c[:-n, :-n])[:x.shape[0], :x.shape[1]] / (n * n)


def ssim(a, b):
    x, y = luma(a), luma(b); c1, c2 = 0.01 ** 2, 0.03 ** 2
    mx, my = box(x), box(y); vx, vy, cxy = box(x * x) - mx * mx, box(y * y) - my * my, box(x * y) - mx * my
    return float(np.mean(((2 * mx * my + c1) * (2 * cxy + c2)) / ((mx * mx + my * my + c1) * (vx + vy + c2))))


def sharpness(a):
    # Mean luma gradient magnitude (no-reference detail proxy; noise and halos raise it too).
    y = luma(a); return float(np.mean(np.hypot(np.diff(y, axis=1)[:-1], np.diff(y, axis=0)[:, :-1])))


def crops(images, labels, reference=None, size=480, count=3, prefix='crop'):
    """The `count` busiest non-overlapping windows of the first image, all images side by side."""
    base = luma(images[0]); g = np.hypot(*np.gradient(base)); h, w = base.shape; picks = []
    energy = box(g, size // 2)
    for _ in range(count):
        y, x = np.unravel_index(np.argmax(energy), energy.shape)
        y0, x0 = int(np.clip(y - size // 2, 0, h - size)), int(np.clip(x - size // 2, 0, w - size))
        picks.append((x0, y0)); energy[max(0, y0 - size):y0 + 2 * size, max(0, x0 - size):x0 + 2 * size] = -1
    out = []
    for i, (x0, y0) in enumerate(picks):
        tiles = [(reference, 'reference')] if reference is not None else []
        tiles += list(zip(images, labels))
        sheet = Image.new('RGB', (size * len(tiles), size + 24), (24, 24, 24)); draw = ImageDraw.Draw(sheet)
        for k, (img, label) in enumerate(tiles):
            sheet.paste(Image.fromarray(img[y0:y0 + size, x0:x0 + size]), (k * size, 24)); draw.text((k * size + 6, 5), label, fill=(240, 240, 240))
        path = OUT/f'{prefix}-{i + 1}-x{x0}-y{y0}.png'; sheet.save(path); out.append(str(path))
    return out


source = Image.open(args.image).convert('RGB')
report = dict(mode=args.mode, image=str(args.image), imageSize=list(source.size), cases={})
if args.mode == 'blurry':
    soft = source.resize((3840, 2160), Image.BICUBIC) if source.size != (3840, 2160) else source
    soft_path = WORK/'soft-4k.png'; soft.save(soft_path)
    cases = [('input', 0, 'off', '4k', 'target'), ('vsr4-native', 4, 'off', '4k', 'target'),
             ('vsr4-1440-4k', 4, '1440', '4k', 'target'), ('vsr4-1080-4k', 4, '1080', '4k', 'target'),
             ('vsr4-1080-8k-4k', 4, '1080', '8k', '4k'), ('vsr2-1080-4k', 2, '1080', '4k', 'target')]
    images, labels = [], []
    for name, q, pre, tgt, outp in cases:
        img, ms, fields = render(soft_path, name, q, pre, tgt, outp)
        report['cases'][name] = dict(srMs=ms, srInput=fields['srInput'], srOutput=fields['srOutput'], output=fields['output'], sharpness=sharpness(img))
        images.append(img); labels.append(f'{name}  {ms:.1f} ms' if ms > 0 else name)
    report['crops'] = crops(images, labels)
else:
    assert source.size == (3840, 2160), 'native mode needs a 3840x2160 reference'
    ref = np.asarray(source)
    buf = io.BytesIO(); source.resize((1920, 1080), Image.BOX).save(buf, 'JPEG', quality=70)
    soft = Image.open(io.BytesIO(buf.getvalue())).convert('RGB').resize((3840, 2160), Image.BICUBIC)
    soft_path = WORK/'soft-4k.png'; soft.save(soft_path)
    qhd_path = WORK/'native-2k.png'; source.resize((2560, 1440), Image.LANCZOS).save(qhd_path)
    groups = {'soft-4k': (soft_path, [('input', 0, 'off', '4k', 'target'), ('vsr4-native', 4, 'off', '4k', 'target'),
                                      ('vsr4-1440-4k', 4, '1440', '4k', 'target'), ('vsr4-1080-4k', 4, '1080', '4k', 'target'),
                                      ('vsr4-1080-8k-4k', 4, '1080', '8k', '4k')]),
              'native-2k': (qhd_path, [('vsr4-2k-4k', 4, 'off', '4k', 'target'), ('vsr4-2k-8k-4k', 4, 'off', '8k', '4k'),
                                       ('vsr4-1080-4k', 4, '1080', '4k', 'target')])}
    report['crops'] = {}
    for group, (src, cases) in groups.items():
        images, labels = [], []
        if group == 'native-2k':
            up = np.asarray(Image.open(src).resize((3840, 2160), Image.BICUBIC))
            report['cases'][group + '/bicubic'] = dict(psnr=psnr(up, ref), ssim=ssim(up, ref), sharpness=sharpness(up))
            images.append(up); labels.append('bicubic')
        for name, q, pre, tgt, outp in cases:
            img, ms, fields = render(src, f'{group}-{name}', q, pre, tgt, outp)
            assert img.shape == ref.shape, (name, img.shape)
            report['cases'][group + '/' + name] = dict(srMs=ms, srInput=fields['srInput'], srOutput=fields['srOutput'], output=fields['output'],
                                                       psnr=psnr(img, ref), ssim=ssim(img, ref), sharpness=sharpness(img))
            images.append(img); labels.append(f'{name}  {ms:.1f} ms' if ms > 0 else name)
        report['crops'][group] = crops(images, labels, reference=ref, prefix=group)
    report['referenceSharpness'] = sharpness(ref)
(OUT/'ab.json').write_text(json.dumps(report, ensure_ascii=False, indent=1), 'utf8')
print(json.dumps(report, ensure_ascii=False, indent=1))
