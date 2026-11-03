"""Build only the open-source RX7000 NVAPI LUID repair at a pinned ZLUDA commit.

Prepare a private checkout of RedDukeDev/ZLUDA at PIN outside the Veyra source
tree. This script applies the checked-in patch, uses E-drive Cargo/TEMP/target
directories, and emits zluda_nvapi.dll plus identity/build receipts. It never
installs to an app, loads CUDA/HIP, or touches a driver/model/cache.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

PIN = 'b135e233bb7a6ff7f8690e49701f87f4944adcee'
BASE = Path('E:/项目/Veyra').resolve()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True)
    args = parser.parse_args()
    source, out = args.source.resolve(), args.output_root.resolve()
    assert source.is_relative_to(BASE / 'deps'), 'Use a task checkout under E:/项目/Veyra/deps'
    assert out.is_relative_to(BASE / 'build'), 'Build output must be under E:/项目/Veyra/build'
    patch = Path(__file__).resolve().parents[2] / 'third_party/redduke/zluda-current-device-luid.patch'
    def git(*command):
        return subprocess.check_output(['git', *command], cwd=source)
    assert git('rev-parse', 'HEAD').decode().strip() == PIN, 'Wrong ZLUDA revision'
    assert (source / 'Cargo.lock').read_bytes().replace(b'\r\n', b'\n') == git('show', PIN + ':Cargo.lock'), 'Unexpected dependency lock edits'
    target = source / 'nvapi/src/lib.rs'
    # Only the exact upstream file or the exact expected patched file is admitted.
    original = git('show', PIN + ':nvapi/src/lib.rs')
    current = target.read_bytes().replace(b'\r\n', b'\n')
    if current == original:
        subprocess.run(['git', 'apply', '--check', str(patch)], cwd=source, check=True)
        subprocess.run(['git', 'apply', str(patch)], cwd=source, check=True)
    # Canonical diff equality also protects a resumed private build from an
    # unrelated edit being passed off as this small dependency repair.
    assert git('diff', '--', 'nvapi/src/lib.rs').replace(b'\r\n', b'\n') == patch.read_bytes().replace(b'\r\n', b'\n'), 'Unexpected NVAPI edits'
    out.mkdir(parents=True, exist_ok=True)
    tmp = BASE / 'tmp' / out.name
    cargo = source.parent / 'cargo'
    tmp.mkdir(parents=True, exist_ok=True)
    cargo.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['CARGO_HOME'] = str(cargo)
    env['CARGO_TARGET_DIR'] = str(out)
    env['TEMP'] = env['TMP'] = str(tmp)
    env['CARGO_BUILD_JOBS'] = '2'
    command = tmp / 'nvapi-build.cmd'
    command.write_text('@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\ncargo build --release -p nvapi --locked -j 2\n', encoding='ascii')
    start = time.monotonic()
    with (out / 'nvapi-build.log').open('wb') as log:
        result = subprocess.run(['cmd.exe', '/d', '/c', str(command)], cwd=source, env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=880)
    receipt = {'upstream': 'RedDukeDev/ZLUDA', 'commit': PIN, 'license': 'MIT OR Apache-2.0',
               'patch_sha256': sha(patch.read_bytes()), 'original_source_sha256': sha(original),
               'patched_source_sha256': sha(target.read_bytes()), 'cargo_lock_sha256': sha((source / 'Cargo.lock').read_bytes()),
               'command': str(command), 'code': result.returncode, 'seconds': time.monotonic() - start}
    if result.returncode == 0:
        compiled = out / 'release/nvapi64.dll'
        data = compiled.read_bytes()
        (out / 'zluda_nvapi.dll').write_bytes(data)
        receipt['file'] = {'name': 'zluda_nvapi.dll', 'size': len(data), 'sha256': sha(data)}
    (out / 'nvapi-build.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(json.dumps(receipt, indent=2))
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
