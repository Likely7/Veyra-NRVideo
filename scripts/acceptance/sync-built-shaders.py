"""Check/sync a local candidate's shaders against its actual build outputs.

Old release packages are runtime donors only. Their shaders must not silently
stand in for the current source build. Copies use replace(), never in-place
writes, because donor/stage resources may be hard linked.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--app-dir', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    build, app = args.build_dir.resolve(strict=True), args.app_dir.resolve(strict=True)
    sources = sorted((build/'shaders').rglob('*.dxil'))
    if not sources:
        raise SystemExit('No built shaders; build the product first')
    if args.apply and sys.platform == 'win32':
        command = "[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false); Get-CimInstance Win32_Process -Filter \"Name='veyra_qml_ui.exe'\" | Select-Object -ExpandProperty ExecutablePath | ConvertTo-Json -Compress"
        result = subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', command],
                                check=True, stdout=subprocess.PIPE, encoding='utf-8',
                                creationflags=subprocess.CREATE_NO_WINDOW)
        running = json.loads(result.stdout) if result.stdout.strip() else []
        if isinstance(running, str):
            running = [running]
        if any(Path(path).resolve() == app/'veyra_qml_ui.exe' for path in running if path):
            raise SystemExit('Target app is running; leave it untouched and retry after it exits')
    records = []
    for source in sources:
        relative = source.relative_to(build/'shaders')
        target = app/'shaders'/relative
        expected = digest(source)
        actual = digest(target) if target.exists() else None
        row = dict(path=relative.as_posix(), bytes=source.stat().st_size,
                   expected_sha256=expected, before_sha256=actual)
        if actual != expected and args.apply:
            target.parent.mkdir(parents=True, exist_ok=True)
            if not target.resolve().is_relative_to(app):
                raise SystemExit('Shader target escapes app directory: ' + str(target))
            temporary = target.with_name(target.name+'.new')
            shutil.copy2(source, temporary)
            os.replace(temporary, target)
            actual = digest(target)
        row['matches'] = actual == expected
        row['copied'] = row['before_sha256'] != expected and args.apply
        records.append(row)
    report = dict(build=str(build), app=str(app), shaders=records,
                  count=len(records), copied=sum(row['copied'] for row in records),
                  mismatches=[row['path'] for row in records if not row['matches']])
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'shaders'}, ensure_ascii=False))
    return bool(report['mismatches'])


if __name__ == '__main__':
    raise SystemExit(main())
