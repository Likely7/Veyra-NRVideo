"""Builds the 2.0.0 beta 2 test package (with PC streaming) from the last validated package layout.

usage: make-beta.py <base-stage> <build-dir> <package-dir> <zip-path> [<notes.md> <package label>]

<base-stage>   a staged 2.0.0 app with its runtime (the 2026-09-30 candidate): Qt, FFmpeg, runtime/, shaders ...
<build-dir>    the VEYRA_ENABLE_MOONLIGHT build: veyra_qml_ui.exe and the qml folder come from here
Test executables, test QML and logs are left out; every runtime is copied for real (no junctions).
"""
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import zipfile
from pathlib import Path

base, build, pkg, zpath = (Path(a) for a in sys.argv[1:5])
notes = sys.argv[5] if len(sys.argv) > 5 else 'docs/TEST_PACKAGE_2.0.0beta2_moonlight.md'
label = sys.argv[6] if len(sys.argv) > 6 else 'Veyra 2.0.0beta2 (PC streaming)'
repo = Path(__file__).resolve().parents[2]
if pkg.exists():
    raise SystemExit(f'refusing to reuse {pkg}')
skip_files = {'veyra_qml_data_tests.exe', 'veyra_qml_easing_tests.exe', 'veyra_qml_quick_tests.exe', 'LOCAL_FIELD_FIXES.md'}
skip_dirs = {'qml-tests', 'logs', 'data'}
pkg.mkdir(parents=True)
for item in base.iterdir():
    if item.name in skip_files or item.name in skip_dirs:
        continue
    if item.is_dir():
        shutil.copytree(item, pkg / item.name)
    else:
        shutil.copy2(item, pkg / item.name)

# The new player and its QML (the base's Qt QML modules stay; only the Veyra module is replaced).
shutil.copy2(build / 'veyra_qml_ui.exe', pkg / 'veyra_qml_ui.exe')
shutil.rmtree(pkg / 'qml/Veyra')
shutil.copytree(build / 'qml/Veyra', pkg / 'qml/Veyra')

# Documents and licences.
for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md'):
    shutil.copy2(repo / name, pkg / name)
(pkg / 'docs').mkdir(exist_ok=True)
for name in ('MOONLIGHT_EXECUTION_2026-10-01.md', 'STREAMING_PLAN_MOONLIGHT_XBOX_2026-10-01.md', 'XBOX_EXECUTION_2026-10-01.md', 'STREAM_FIELD_FIXES_2026-10-01.md', 'MAGEWELL_LOW_LATENCY_2026-10-01.md'):
    if (repo / 'docs' / name).exists():
        shutil.copy2(repo / 'docs' / name, pkg / 'docs' / name)
shutil.copy2(repo / notes, pkg / '测试说明.md')
(pkg / 'licenses').mkdir(exist_ok=True)
for sub in ('moonlight', 'xbox', 'magewell'):
    if (repo / 'licenses' / sub).exists():
        shutil.copytree(repo / 'licenses' / sub, pkg / 'licenses' / sub, dirs_exist_ok=True)

# Magewell Pro Capture low-latency mode: the SDK library, unmodified (see licenses/magewell).
magewell = Path('E:/项目/Veyra/deps/magewell/3.3.1.1596/bin/x64/LibMWCapture.dll')
if magewell.exists():
    (pkg / 'runtime/magewell').mkdir(parents=True, exist_ok=True)
    shutil.copy2(magewell, pkg / 'runtime/magewell/LibMWCapture.dll')

mf = pkg / 'runtime/experimental/release-runtime-manifest.json'
if mf.exists():
    manifest = json.loads(mf.read_text(encoding='utf-8-sig'))
    manifest['package'] = label
    mf.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')

# Checks: no reparse points, no test executables, the player is the new one.
problems = []
files = []
for root, dirs, names in os.walk(pkg):
    for d in dirs:
        if os.stat(Path(root) / d, follow_symlinks=False).st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            problems.append(f'reparse point: {Path(root) / d}')
    for n in names:
        p = Path(root) / n
        files.append({'path': p.relative_to(pkg).as_posix(), 'size': p.stat().st_size,
                      'sha256': hashlib.sha256(p.read_bytes()).hexdigest().upper()})
names = {f['path'] for f in files}
for bad in skip_files:
    if bad in names:
        problems.append(f'test file left in: {bad}')
new_hash = hashlib.sha256((build / 'veyra_qml_ui.exe').read_bytes()).hexdigest().upper()
if next(f for f in files if f['path'] == 'veyra_qml_ui.exe')['sha256'] != new_hash:
    problems.append('player hash mismatch')
commit = subprocess.run(['git', '-C', str(repo), 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
(pkg / 'package-manifest.json').write_text(json.dumps({
    'package': label, 'commit': commit, 'player_sha256': new_hash,
    'files': files}, ensure_ascii=False, indent=1), encoding='utf-8')
if problems:
    print('\n'.join(problems))
    raise SystemExit(1)

with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for f in files + [{'path': 'package-manifest.json'}]:
        z.write(pkg / f['path'], Path(pkg.name) / f['path'])
print('package', pkg, len(files), 'files; player sha256', new_hash)
print('zip', zpath, zpath.stat().st_size)
