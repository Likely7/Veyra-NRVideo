"""Anti-aliasing comparison of the capture-review screenshots (claude-capture-review-20261026).

Decodes each JXR (scRGB half float) with WIC through PowerShell/WPF, then compares
luma in the perceptual domain the anti-aliasing passes use (reference white 203 nits):
the whole picture, edge pixels only, and the edge high-frequency energy that jaggies
add. A still game picture is required; nr-aa-off-repeat measures what NR's
temporal history alone changes between two identical configurations.
Shots are named from the run log ("[capture-review] shot <name> file=<path>"); the
engine swaps the extension to .jxr for HDR output, so files are matched by stem.
nrEnabled switches the selected NR layer only: the user's chain keeps a second NR
layer on, so the "nonr" steps still ran one NR layer, and the node-mode step loaded
node mode's own (empty) chain. Both are reported as such, not as AA-before-NR or
AA-without-NR evidence.
usage: py -3.11 -X utf8 -B -I scripts/acceptance/capture-review-shots.py
"""
from pathlib import Path
import json, re, subprocess
import numpy as np
from PIL import Image

B = Path('E:/项目/Veyra'); T = 'claude-capture-review-20261026'
SHOTS = B/'tests'/T/'shots'; OUT = B/'reports'/T; RAW = B/'tmp'/T/'shots-raw'; LOG = B/'logs'/T/'capture'/'engine.log'
RAW.mkdir(parents=True, exist_ok=True)
DECODE = r'''param([string]$In,[string]$Out)
Add-Type -AssemblyName PresentationCore,WindowsBase
$s=[IO.File]::OpenRead($In)
try{
$d=New-Object System.Windows.Media.Imaging.WmpBitmapDecoder($s,[System.Windows.Media.Imaging.BitmapCreateOptions]::PreservePixelFormat,[System.Windows.Media.Imaging.BitmapCacheOption]::OnLoad)
$f=$d.Frames[0]
$c=New-Object System.Windows.Media.Imaging.FormatConvertedBitmap($f,[System.Windows.Media.PixelFormats]::Rgba128Float,$null,0)
$w=$c.PixelWidth;$h=$c.PixelHeight;$stride=$w*16;$buf=New-Object byte[] ($stride*$h)
$c.CopyPixels($buf,$stride,0);[IO.File]::WriteAllBytes($Out,$buf);Write-Output "$w $h $($f.Format)"
}finally{$s.Close()}
'''
script = RAW/'decode.ps1'; script.write_text(DECODE, 'utf-8-sig')


def decode(path):
    raw = RAW/(path.stem + '.f32')
    out = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script), str(path), str(raw)],
                         capture_output=True, text=True, timeout=120)
    w, h, fmt = out.stdout.split()[:3]
    rgba = np.fromfile(raw, dtype=np.float32).reshape(int(h), int(w), 4)
    return rgba[..., :3], fmt


def perceptual_luma(rgb):
    # AntiAlias.hlsli: linear working colour, 1.0 = 80 nits; reference white 203 nits.
    y = np.maximum(rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32), 0) * (80.0 / 203.0)
    low = np.where(y <= 0.0031308, 12.92 * y, 1.055 * np.power(np.minimum(y, 1.0), 1 / 2.4) - 0.055)
    return np.where(y > 1.0, 1.0 + 0.30469 * np.log2(np.maximum(y, 1e-6)), low)


def gradient(l):
    gx = np.zeros_like(l); gy = np.zeros_like(l)
    gx[:, 1:-1] = l[:, 2:] - l[:, :-2]; gy[1:-1, :] = l[2:, :] - l[:-2, :]
    return np.hypot(gx, gy)


def laplacian(l):
    out = np.zeros_like(l)
    out[1:-1, 1:-1] = 4 * l[1:-1, 1:-1] - l[:-2, 1:-1] - l[2:, 1:-1] - l[1:-1, :-2] - l[1:-1, 2:]
    return np.abs(out)


requested = {}
for line in LOG.read_text('utf8', errors='replace').splitlines():
    if m := re.search(r'\[capture-review\] shot (\S+) file=(\S+) \| (.*)', line): requested[m.group(1)] = (Path(m.group(2)).stem, m.group(3))
files = {p.stem: p for p in SHOTS.iterdir() if p.suffix.lower() in ('.jxr', '.png')}
named = {}
for name, (stem, state) in requested.items():
    path = files[stem]
    rgb, fmt = decode(path) if path.suffix.lower() == '.jxr' else (np.asarray(Image.open(path).convert('RGB'), np.float32) / 255.0, 'png')
    named[name] = dict(path=path, state=state, format=fmt,
                       luma=perceptual_luma(rgb) if fmt != 'png' else rgb @ np.array([0.2126, 0.7152, 0.0722], np.float32))
base = named['nr-aa-off']['luma']
edges = gradient(base) > 0.08          # edge pixels of the reference picture
energy = lambda l: float(laplacian(l)[edges].mean())


def compare(a, b):
    la, lb = named[a]['luma'], named[b]['luma']
    d = np.abs(la - lb)
    return dict(meanAbs=float(d.mean()), edgeMeanAbs=float(d[edges].mean()), flatMeanAbs=float(d[~edges].mean()),
                changedPixels=float((d > 2 / 255).mean()), edgeEnergyA=energy(la), edgeEnergyB=energy(lb))


pairs = {'NR history noise, two NR layers (same config twice)': ('nr-aa-off', 'nr-aa-off-repeat'),
         'AA after two NR layers (new placement)': ('nr-aa-off', 'nr-aa-after'),
         'AA after one NR layer (new placement)': ('nonr-aa-off', 'nonr-aa-on')}
results = {k: compare(*v) for k, v in pairs.items()}
# Visual: the 96x96 window where AA after NR changed edges most, 4x nearest.
d = np.abs(named['nr-aa-off']['luma'] - named['nr-aa-after']['luma']) * edges
k = 96; best = None
for y in range(0, d.shape[0] - k, k // 2):
    for x in range(0, d.shape[1] - k, k // 2):
        s = float(d[y:y + k, x:x + k].sum())
        if best is None or s > best[0]: best = (s, y, x)
_, y, x = best
tiles = []
crop_order = ['nr-aa-off', 'nr-aa-after', 'nr-aa-off-repeat', 'nonr-aa-off', 'nonr-aa-on']
for n in crop_order:
    crop = np.clip(named[n]['luma'][y:y + k, x:x + k], 0, 1.4) / 1.4
    tiles.append(np.kron((crop * 255).astype(np.uint8), np.ones((4, 4), np.uint8)))
strip = np.concatenate([np.pad(t, ((0, 0), (0, 8)), constant_values=255) for t in tiles], axis=1)
Image.fromarray(strip).save(OUT/'aa-crops.png')
report = dict(shots={n: str(v['path']) for n, v in named.items()}, formats={n: v['format'] for n, v in named.items()},
              states={n: v['state'] for n, v in named.items()}, edgeFraction=float(edges.mean()), comparisons=results,
              excluded={'nr-aa-before-node': 'node mode loaded its own chain (plan steps=0 nr=0): no NR, no AA, so not an AA-before-NR picture'},
              note='nrEnabled switches the selected NR layer; the copied chain keeps another NR layer on, so the nonr-* steps ran one NR layer (plan nr=1)',
              crop=dict(x=x, y=y, size=k, order=' | '.join(crop_order), file=str(OUT/'aa-crops.png')))
(OUT/'aa-shots.json').write_text(json.dumps(report, ensure_ascii=False, indent=1), 'utf-8')
print(json.dumps({k: report[k] for k in ['formats', 'edgeFraction', 'comparisons', 'crop']}, ensure_ascii=False, indent=1))
