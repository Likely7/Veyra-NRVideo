"""Exact unchanged dependency sources plus the new MIT runtimes/build materials.

No proprietary SDK, runtime, model, private profile or test media is introduced.
The legacy archive remains nested with its verified identity for reproducibility.
"""
import hashlib, json, subprocess, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
BASE = Path('E:/项目/Veyra'); OUT = BASE / 'releases/release-2.0.3-20261004'
OLD = BASE / 'releases/2.0.0-final-20261002/Veyra-2.0.0-dependency-source.zip'
LMXXF = BASE / 'deps/lmxxf-nr-20261003'
BAD = {'.dll', '.exe', '.lib', '.pdb', '.obj', '.o', '.a', '.so', '.dylib', '.onnx', '.pth', '.pt', '.f16', '.f32',
       '.i32', '.fp8', '.bin', '.hsaco', '.co', '.spv', '.whl', '.log', '.err', '.dmp', '.mp4', '.mkv', '.avi', '.addon64', '.zip', '.7z',
       '.png', '.jpg', '.jpeg', '.webp', '.csv', '.gz'}

def digest(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()

def git(p, *args): return subprocess.check_output(['git', '-C', str(p), *args]).decode().strip()
assert digest(OLD) == 'a293552434381a71169c1c27658f07202d67243ca82e748936b9db15077ce0de'
assert git(LMXXF, 'rev-parse', 'HEAD') == '78f548749e74824327b8458c57be31a1df78376a'
assert not git(LMXXF, 'diff', '--name-only', 'HEAD')
OUT.mkdir(parents=True, exist_ok=True)
target = OUT / 'Veyra-2.0.3-dependency-source.zip'; records = []

def put(z, name, data):
    z.writestr(name, data)
    records.append(dict(path=name, size=len(data), sha256=hashlib.sha256(data).hexdigest()))

with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    # Already compressed archive: retain byte identity and avoid recompression.
    z.write(OLD, 'unchanged/' + OLD.name, compress_type=zipfile.ZIP_STORED)
    records.append(dict(path='unchanged/' + OLD.name, size=OLD.stat().st_size, sha256=digest(OLD)))
    for name in filter(None, git(LMXXF, 'ls-files', '-z').split('\0')):
        f = LMXXF / name
        if f.is_file() and f.suffix.lower() not in BAD and Path(name).parts[0] != 'conversation' and '_cubin' not in f.name:
            put(z, 'lmxxf-78f5487/' + name, f.read_bytes())
    put(z, 'lmxxf-78f5487/SOURCE_COMMIT.txt', (git(LMXXF, 'rev-parse', 'HEAD') + '\nUnmodified tracked source files; model weights, compiled HIP modules, captured images, measurement CSVs, archives and conversation transcripts excluded.\n').encode())
    samples = BASE / 'deps/vfg-samples-20261003'
    sample_audit = json.loads((BASE / 'logs/vfg-research-20261003/official-samples.json').read_text(encoding='utf8'))
    assert sample_audit['commit'] == '52011f89c1741d06b40ea312af1f20be8be9ec62'
    for name in ('LICENSE', 'apps/VideoFrameGenerationEffectApp/VideoFrameGenerationEffectApp.cpp', 'apps/VideoFrameGenerationEffectApp/README.md'):
        expected = next(r for r in sample_audit['files'] if Path(r['path']) == samples / name)
        assert (samples / name).stat().st_size == expected['bytes'] and digest(samples / name) == expected['sha256'].lower()
        put(z, 'vfg-mit-reference/' + name, (samples / name).read_bytes())
    for name in ('scripts/amd-nr/build-lmxxf-runtime.py', 'scripts/amd-nr/msvc-compat.h', 'scripts/amd-nr/prepare-local-runtime.py',
                 'third_party/lmxxf/VEYRA_ORIGIN.md', 'third_party/lmxxf/LICENSE', 'docs/BUILD_2.0.3.md',
                 'docs/RUNTIME_COMPONENTS_2.0.3.md', 'scripts/package-vendor-release.py'):
        put(z, 'veyra/' + name, (ROOT / name).read_bytes())
    put(z, 'README.txt', b'Veyra 2.0.3 corresponding open-source dependencies. Extract unchanged/Veyra-2.0.0-dependency-source.zip for the exact existing Qt 6.8.3, patched FFmpeg, Chiaki, Moonlight, Xbox/WebRTC, OpenSSL and FidelityFX2.3.0 sources. New unmodified lmxxf commit 78f5487, actual MSVC compatibility/rebuild recipe and MIT VFG sample reference are included here. Use the v2.0.3 application tag for the exact Veyra integration, shaders, SoundTouch and patches. Models and proprietary NVIDIA SDKs/runtimes remain separate; obtain development SDKs from their official authors. Source-only packaging is not an independent legal audit.\n')
    z.writestr('source-manifest.json', json.dumps(records, indent=2))
with zipfile.ZipFile(target) as z:
    for row in records:
        with z.open(row['path']) as f: assert hashlib.file_digest(f, 'sha256').hexdigest() == row['sha256'], row['path']
sha = digest(target)
target.with_suffix('.zip.sha256').write_text(sha + '  ' + target.name + '\n', encoding='ascii')
print(json.dumps(dict(path=str(target), files=len(records), bytes=target.stat().st_size, sha256=sha)), flush=True)
