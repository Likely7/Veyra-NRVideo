"""Build FFmpeg 9.0.2 with the pinned vcpkg 9.0.1#1 port's build recipe
and Veyra's three pinned patches.

usage: make-ffmpeg-overlay.py <vcpkg-root> <veyra-root> <overlay-dir>
"""
import hashlib
import re
import shutil
import sys
from pathlib import Path

PATCHES = {
    # 32 -> 256 H.264 slices (PS5 Remote Play streams)
    'ps5-h264-slices.patch': 'e7d138926873faf479ebdb17b59cecfd654086f5062220ba45ab08ccee9fae26',
    # D3D12 deinterlace filter: per-slot allocators/fences, PTS fix
    'deinterlace-d3d12-async.patch': 'e93c2c40877a19d2da0319056ff19cf70c1ca5fc5386b06b3cc48cfe9fb6f461',
    # D3D12VA reference-only resource slots (PS5 H.264/HEVC on affected drivers).
    'd3d12va-reference-only-pool.patch': '85c3ba4a730b10cd2d217224a7fda6d2bd0b705c5bb515250dbbc09ade3289ed',
}
FFMPEG_VERSION = '9.0.2'
FFMPEG_SHA512 = '21bf3fbcdfd2f41ea6edeab40433fecfe362b09ecaa177a652471b54f9357011b4f74dab18245ab1c26f1a473b94205490baecb42289afd2effd603cd0b22b59'
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
(dest / 'vcpkg.json').write_text(
    manifest.replace('"version": "9.0.1"', f'"version": "{FFMPEG_VERSION}"')
            .replace('  "port-version": 1,\n', ''),
    encoding='utf-8', newline='\n')

for name, expected in PATCHES.items():
    data = (veyra / 'scripts' / 'ffmpeg' / name).read_text(encoding='utf-8').encode('utf-8')
    got = hashlib.sha256(data).hexdigest()
    if got != expected:
        sys.exit(f'{name}: sha256 {got} != pinned {expected}')
    (dest / name).write_bytes(data)

portfile = dest / 'portfile.cmake'
text = portfile.read_text(encoding='utf-8')
if text.count(ANCHOR) != 1:
    sys.exit('portfile PATCHES anchor not found exactly once')
text, hash_matches = re.subn(r'^    SHA512 [a-f0-9]{128}$', f'    SHA512 {FFMPEG_SHA512}', text, count=1, flags=re.M)
if hash_matches != 1:
    sys.exit('portfile upstream archive hash not found')
text = text.replace(ANCHOR, ANCHOR + ''.join(f'        {n}\n' for n in PATCHES))
portfile.write_text(text, encoding='utf-8', newline='\n')
print('overlay port written to', dest)
