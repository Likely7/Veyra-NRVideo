"""RTX Video SR picture A/B: one SR pass, supersampling (SR to 8K, then an exact area
shrink to 4K) and double SR (base SR -> reduce -> second SR).

Every picture goes through the real graph (veyra_sr_quality_tests sr-image in the
shared staged copy, video-file semantics: a 4K source at a 4K target gets the
existing native-size restore). Lossless PNG in and out; RTX Video SR quality 4 for
every pass.

  --mode blurry --image X   no reference: X is scaled to 3840x2160 (bicubic) as a soft
                            4K; sharpness proxy only.
  --mode native --image X   X is a native 3840x2160 reference. Three sources are made
                            from it (soft 4K: area 1920x1080, JPEG q70, bicubic back to
                            4K; native 2K: Lanczos 2560x1440; native 1080p: Lanczos
                            1920x1080) and every 4K result is compared with the reference
                            (PSNR / SSIM on luma, gradient sharpness proxy).
Crops (100 %) of the busiest regions go side by side into one PNG per group.
usage: py -3.11 -X utf8 -B -I scripts/acceptance/sr-ab.py --mode blurry|native --image PATH [--tag NAME] [--task NAME]
"""
from pathlib import Path
import argparse, io, json, os, subprocess
import numpy as np
from PIL import Image, ImageDraw

ap = argparse.ArgumentParser(); ap.add_argument('--mode', choices=['blurry', 'native'], required=True)
ap.add_argument('--image', required=True); ap.add_argument('--tag', default=''); ap.add_argument('--task', default='claude-sr-supersample-20261028')
args = ap.parse_args()
assert args.task.startswith(('codex-', 'claude-')) and '/' not in args.task and '\\' not in args.task and '..' not in args.task
B = Path('E:/项目/Veyra'); T = args.task; tag = args.tag or args.mode
A = B/'tests/claude-sr-stage-20261028/app'; TMP = B/'tmp'/T/'ab'
OUT = B/'reports'/T/'ab'/tag; WORK = B/'tests'/T/'ab'/tag
for p in (OUT, WORK, TMP): p.mkdir(parents=True, exist_ok=True)
env = {k: v for k, v in os.environ.items() if not k.upper().startswith(('VEYRA_', 'QT_', 'QSG_', 'QML_'))}
env.update(TEMP=str(TMP), TMP=str(TMP))
# (name, base quality 0..4, base target, supersample shrink or 'off', reduction or 'off', second target)
SINGLE = ('single-4k', 4, '4k', 'off', 'off', '4k')
SUPERSAMPLE = ('ss-8k-4k', 4, '8k', '4k', 'off', '4k')
DUAL_1080 = ('dual-4k-1080-4k', 4, '4k', 'off', '1080', '4k')
DUAL_1440 = ('dual-4k-1440-4k', 4, '4k', 'off', '1440', '4k')


def render(src, name, quality, target, shrink, reduce, second):
    """One picture through the graph; returns (array, GPU ms of the SR stage, sizes)."""
    run = subprocess.run([str(A/'veyra_sr_quality_tests.exe'), str(WORK/'gpu'), 'sr-image', str(src), name, str(quality), target, shrink, reduce, second, '4', 'video'],
                         cwd=A, env=env, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    line = next((l for l in run.stdout.splitlines() if l.startswith('SR_IMAGE')), '')
    assert run.returncode == 0 and 'passed=1' in line, (name, run.returncode, line, run.stdout[-500:])
    fields = dict(kv.split('=', 1) for kv in line.split()[1:])
    if reduce != 'off': assert fields['first'] != '0x0' and int(fields['evaluations']) == 12, ('double SR did not run twice', name, line)
    if shrink != 'off': assert fields['srOutput'] != fields['output'] and int(fields['evaluations']) == 6, ('supersample did not shrink one pass', name, line)
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


def crops(images, labels, prefix, reference=None, size=480, count=3):
    """The `count` busiest non-overlapping windows of the first image, all images side by side."""
    base = luma(images[0]); g = np.hypot(*np.gradient(base)); h, w = base.shape; picks = []
    energy = box(g, size // 2)
    for _ in range(count):
        y, x = np.unravel_index(np.argmax(energy), energy.shape)
        y0, x0 = int(np.clip(y - size // 2, 0, h - size)), int(np.clip(x - size // 2, 0, w - size))
        picks.append((x0, y0)); energy[max(0, y0 - size):y0 + 2 * size, max(0, x0 - size):x0 + 2 * size] = -1
    out = []
    for i, (x0, y0) in enumerate(picks):
        tiles = ([(reference, 'reference')] if reference is not None else []) + list(zip(images, labels))
        sheet = Image.new('RGB', (size * len(tiles), size + 24), (24, 24, 24)); draw = ImageDraw.Draw(sheet)
        for k, (img, label) in enumerate(tiles):
            sheet.paste(Image.fromarray(img[y0:y0 + size, x0:x0 + size]), (k * size, 24)); draw.text((k * size + 6, 5), label, fill=(240, 240, 240))
        path = OUT/f'{prefix}-{i + 1}-x{x0}-y{y0}.png'; sheet.save(path); out.append(str(path))
    return out


def group(name, src, cases, reference=None, upscale=False):
    images, labels = [], []
    if upscale:
        up = np.asarray(Image.open(src).resize((3840, 2160), Image.BICUBIC))
        entry = dict(sharpness=sharpness(up))
        if reference is not None: entry.update(psnr=psnr(up, reference), ssim=ssim(up, reference))
        report['cases'][name + '/bicubic'] = entry; images.append(up); labels.append('bicubic')
    for case, quality, target, shrink, reduce, second in cases:
        img, ms, fields = render(src, f'{name}-{case}', quality, target, shrink, reduce, second)
        assert img.shape == (2160, 3840, 3), (case, img.shape)
        entry = dict(srMs=ms, first=fields['first'], srInput=fields['srInput'], srOutput=fields['srOutput'], output=fields['output'],
                     evaluations=int(fields['evaluations']), sharpness=sharpness(img))
        if reference is not None: entry.update(psnr=psnr(img, reference), ssim=ssim(img, reference))
        report['cases'][name + '/' + case] = entry; images.append(img); labels.append(f'{case}  {ms:.1f} ms' if ms > 0 else case)
    report['crops'][name] = crops(images, labels, name, reference)


source = Image.open(args.image).convert('RGB')
report = dict(mode=args.mode, image=str(args.image), imageSize=list(source.size), quality=4, cases={}, crops={})
if args.mode == 'blurry':
    soft = source.resize((3840, 2160), Image.BICUBIC) if source.size != (3840, 2160) else source
    soft.save(WORK/'soft-4k.png')
    group('soft-4k', WORK/'soft-4k.png', [('input', 0, '4k', 'off', 'off', '4k'), SINGLE, SUPERSAMPLE, DUAL_1080])
else:
    assert source.size == (3840, 2160), 'native mode needs a 3840x2160 reference'
    ref = np.asarray(source)
    buf = io.BytesIO(); source.resize((1920, 1080), Image.BOX).save(buf, 'JPEG', quality=70)
    Image.open(io.BytesIO(buf.getvalue())).convert('RGB').resize((3840, 2160), Image.BICUBIC).save(WORK/'soft-4k.png')
    source.resize((2560, 1440), Image.LANCZOS).save(WORK/'native-2k.png')
    source.resize((1920, 1080), Image.LANCZOS).save(WORK/'native-1080p.png')
    group('soft-4k', WORK/'soft-4k.png', [('input', 0, '4k', 'off', 'off', '4k'), SINGLE, SUPERSAMPLE, DUAL_1080], ref)
    group('native-2k', WORK/'native-2k.png', [SINGLE, SUPERSAMPLE, DUAL_1080, DUAL_1440], ref, upscale=True)
    group('native-1080p', WORK/'native-1080p.png', [SINGLE, SUPERSAMPLE, DUAL_1080, DUAL_1440], ref, upscale=True)
    report['referenceSharpness'] = sharpness(ref)
(OUT/'ab.json').write_text(json.dumps(report, ensure_ascii=False, indent=1), 'utf8')
print(json.dumps(report, ensure_ascii=False, indent=1))
