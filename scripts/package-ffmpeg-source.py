"""Package the installed LGPL FFmpeg's source, patches and reported configuration."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--prefix', type=Path, required=True)
parser.add_argument('--vcpkg', type=Path, required=True)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--version', required=True)
parser.add_argument('--dav1d-source', type=Path)
parser.add_argument('--zlib-source', type=Path)
args = parser.parse_args()
if args.output.exists():
    raise SystemExit('Output exists; choose a new candidate')
spdx_path = args.prefix / 'share/ffmpeg/vcpkg.spdx.json'
spdx = json.loads(spdx_path.read_text(encoding='utf-8'))
port = args.vcpkg / 'ports/ffmpeg'
for item in spdx['files']:
    if not item['SPDXID'].startswith('SPDXRef-port-file-'):
        continue
    path = port / item['fileName']
    expected = next(c['checksumValue'] for c in item['checksums'] if c['algorithm'] == 'SHA256')
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected.lower():
        raise SystemExit(f'Port provenance mismatch: {path.name}')
records = []
local_build_path = args.prefix / 'share/ffmpeg/veyra-local-build.json'
local_build = json.loads(local_build_path.read_text(encoding='utf-8-sig')) if local_build_path.exists() else None
patches = Path(__file__).resolve().parent / 'ffmpeg'
if local_build:
    for item in local_build['files']:
        if Path(item['name']).name != item['name']:
            raise SystemExit('Invalid FFmpeg build manifest path')
        if hashlib.sha256((args.prefix / 'bin' / item['name']).read_bytes()).hexdigest() != item['sha256'].lower():
            raise SystemExit('FFmpeg binary does not match the local build record')
    if hashlib.sha256((args.source / 'libavcodec/h264dec.h').read_bytes()).hexdigest() != local_build['h264HeaderSha256'].lower():
        raise SystemExit('Select the matching Veyra-patched FFmpeg source tree')
    if hashlib.sha256((patches / 'ps5-h264-slices.patch').read_bytes()).hexdigest() != local_build['patchSha256'].lower():
        raise SystemExit('FFmpeg patch identity mismatch')
    for item in local_build.get('additionalPatches', []):
        name = Path(item['name'])
        source = Path(item['source'])
        if name.name != item['name'] or name.suffix != '.patch' or source.anchor or '..' in source.parts:
            raise SystemExit('Invalid additional FFmpeg patch path')
        patch = (patches / name).read_text(encoding='utf-8').encode('utf-8')
        if hashlib.sha256(patch).hexdigest() != item['sha256'].lower():
            raise SystemExit(f'FFmpeg patch identity mismatch: {name}')
        if hashlib.sha256((args.source / source).read_bytes()).hexdigest() != item['sourceSha256'].lower():
            raise SystemExit(f'FFmpeg patched source mismatch: {source}')
with os.add_dll_directory(str((args.prefix / 'bin').resolve())):
    lib = ctypes.CDLL(str((args.prefix / 'bin/avcodec-63.dll').resolve()))
    lib.avcodec_configuration.restype = ctypes.c_char_p
    lib.avcodec_license.restype = ctypes.c_char_p
    configuration = lib.avcodec_configuration().decode()
    license_name = lib.avcodec_license().decode()
if license_name != 'LGPL version 2.1 or later':
    raise SystemExit(f'Unexpected library license: {license_name}')
extra_sources = []
if '--enable-zlib' in configuration:
    if not args.zlib_source or not (args.zlib_source / 'LICENSE').is_file():
        raise SystemExit('The statically linked zlib requires its matching source and notices')
    notice = args.prefix / 'share/zlib'
    if not (notice / 'vcpkg.spdx.json').is_file():
        raise SystemExit('The zlib build provenance must accompany the corresponding source')
    extra_sources += [(args.zlib_source, 'zlib-source'), (notice, 'zlib-notices')]
if '--enable-libdav1d' in configuration:
    if not args.dav1d_source or not (args.dav1d_source / 'COPYING').is_file():
        raise SystemExit('The linked dav1d requires its matching source and notices')
    dav1d_port = args.vcpkg / 'ports/dav1d'
    dav1d_spdx = json.loads((args.prefix / 'share/dav1d/vcpkg.spdx.json').read_text(encoding='utf-8'))
    for item in dav1d_spdx['files']:
        if item['SPDXID'].startswith('SPDXRef-port-file-'):
            expected = next(c['checksumValue'] for c in item['checksums'] if c['algorithm'] == 'SHA256')
            if hashlib.sha256((dav1d_port / item['fileName']).read_bytes()).hexdigest() != expected.lower():
                raise SystemExit('dav1d port provenance mismatch')
    extra_sources += [(args.dav1d_source, 'dav1d-source'), (dav1d_port, 'dav1d-vcpkg-port'),
                     (args.prefix / 'share/dav1d', 'dav1d-notices')]
args.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=7) as archive:
    for base, prefix in [(args.source, 'ffmpeg-patched'), (port, 'vcpkg-port'), *extra_sources]:
        for path in sorted(base.rglob('*')):
            if path.is_file():
                relative = path.relative_to(base)
                if path.suffix.lower() in {'.dll', '.exe', '.pdb', '.obj', '.lib'} or '.git' in relative.parts:
                    raise SystemExit(f'Unexpected source payload: {relative}')
                name = f'{prefix}/{relative.as_posix()}'
                archive.write(path, name)
                records.append({'path': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    archive.write(spdx_path, 'FFMPEG-SPDX.json')
    if local_build:
        archive.write(local_build_path, 'FFMPEG-VEYRA-BUILD.json')
        for path in sorted(patches.iterdir()):
            if path.is_file():
                archive.write(path, 'veyra-patches/' + path.name)
    archive.write(args.vcpkg / 'LICENSE.txt', 'VCPKG-LICENSE.txt')
    archive.writestr('binary-configuration.txt', license_name + '\n\n' + configuration + '\n')
    archive.writestr('source-manifest.json', json.dumps(records, indent=2))
    archive.writestr('README.txt',
        f'FFmpeg 9.0.1#1 corresponding-source material for Veyra {args.version}.\n'
        'Not needed to run the application. No NVIDIA SDK or runtime is included.\n'
        'ffmpeg-patched is the matching patched n9.0.1 source tree.\n'
        'FFMPEG-VEYRA-BUILD.json lists the Veyra patches included in ffmpeg-patched.\n'
        'When starting from an unpatched copy, see veyra-patches/README.md for rebuild steps.\n'
        'vcpkg-port contains the build recipe and patches, verified against the shipped SPDX.\n'
        'binary-configuration.txt is queried from the actual distributed avcodec DLL,\n'
        'not copied from a potentially newer build cache.\n'
        'Build using Visual Studio x64 tools and vcpkg FFmpeg 9.0.1#1, selecting features\n'
        'to match the recorded configuration (shared LGPL libraries, swscale/swresample,\n'
        'external libraries exactly as listed in binary-configuration.txt, no CLI programs).\n'
        'When dav1d is enabled, dav1d-source and dav1d-vcpkg-port contain its source/recipe;\n'
        'build and install it first, then configure FFmpeg with --enable-libdav1d.\n'
        'When zlib is enabled, zlib-source/zlib-notices contain its matching source/provenance.\n'
        'Build the static zlib library with the dynamic MSVC CRT (/MD), install its pkg-config file,\n'
        'then add --enable-zlib using prepare-slice-build.py --zlib-prefix. No zlib DLL is needed.\n'
        'See portfile.cmake and build.sh.in.\n'
        'The recorded configuration includes original local build paths; adapt paths locally.\n'
        'Veyra links dynamically; compatible rebuilt libraries may replace the shipped DLLs.\n'
        'Upstream: https://github.com/FFmpeg/FFmpeg/tree/n9.0.1\n'
        'Port: https://github.com/microsoft/vcpkg/tree/55cd8b8a4f19d8e6ba2ad114c8acacc4af5915a0/ports/ffmpeg\n')
digest = hashlib.sha256(args.output.read_bytes()).hexdigest().upper()
args.output.with_suffix(args.output.suffix + '.sha256').write_text(
    f'{digest}  {args.output.name}\n', encoding='ascii')
print(json.dumps({'archive': str(args.output), 'sha256': digest, 'files': len(records),
                  'license': license_name, 'size': args.output.stat().st_size}))
