"""Create a vcpkg overlay port: the stock vcpkg ffmpeg 9.0.1#1 port plus
Veyra's two FFmpeg source patches, each pinned by SHA-256.

usage: make-ffmpeg-overlay.py <vcpkg-root> <veyra-root> <overlay-dir>
"""
import hashlib
import shutil
import sys
from pathlib import Path

PATCHES = {
    # 32 -> 256 H.264 slices (PS5 Remote Play streams)
    'ps5-h264-slices.patch': 'e7d138926873faf479ebdb17b59cecfd654086f5062220ba45ab08ccee9fae26',
    # D3D12 deinterlace filter: per-slot allocators/fences, PTS fix
    'deinterlace-d3d12-async.patch': 'e93c2c40877a19d2da0319056ff19cf70c1ca5fc5386b06b3cc48cfe9fb6f461',
}
ANCHOR = '        0052-fix-disable-unstable-swscale-link.patch\n'

vcpkg, veyra, overlay = (Path(a) for a in sys.argv[1:4])
port = vcpkg / 'ports' / 'ffmpeg'
manifest = (port / 'vcpkg.json').read_text(encoding='utf-8')
if '"version": "9.0.1"' not in manifest or '"port-version": 1' not in manifest:
    sys.exit('unexpected vcpkg ffmpeg port version; expected 9.0.1#1')

dest = overlay / 'ffmpeg'
if dest.exists():
    shutil.rmtree(dest)
shutil.copytree(port, dest)

for name, expected in PATCHES.items():
    data = (veyra / 'scripts' / 'ffmpeg' / name).read_bytes()
    got = hashlib.sha256(data).hexdigest()
    if got != expected:
        sys.exit(f'{name}: sha256 {got} != pinned {expected}')
    (dest / name).write_bytes(data)

portfile = dest / 'portfile.cmake'
text = portfile.read_text(encoding='utf-8')
if text.count(ANCHOR) != 1:
    sys.exit('portfile PATCHES anchor not found exactly once')
text = text.replace(ANCHOR, ANCHOR + ''.join(f'        {n}\n' for n in PATCHES))
portfile.write_text(text, encoding='utf-8', newline='\n')
print('overlay port written to', dest)
