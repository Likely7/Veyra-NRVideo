"""Verify authorized icon and OBS UI changes against an immutable main baseline."""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = 'aafdeb061e45ae8cdaebfaf68e16c3d589771c05'
# 2026-10-02: user requested icons, then accepted OBS repair and requested restart UI + package.
ICON_SCOPE = {'README.md', 'README_EN.md', 'assets/veyra.ico', 'assets/veyra-app-icon.png', 'apps/veyra-qml/main.cpp', 'include/veyra/ui/QmlPlayerBridge.h', 'src/ui/QmlPlayerBridge.cpp', 'qml/Veyra/SettingsPage.qml', 'i18n/catalog.json'}
ARCHIVE = Path('E:/项目/Veyra/archives/release-2.0.0-20261002')

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args]).decode('utf8').strip()

def main():
    assert git('rev-parse', 'HEAD') == BASE and git('rev-parse', 'main') == BASE
    assert git('branch', '--show-current') == 'codex/release-2.0.0-20261002'
    failures = []
    records = json.loads((ARCHIVE / 'source-baseline.json').read_text(encoding='utf8'))
    for record in records:
        name = record['path']
        if name in ICON_SCOPE or name.startswith(('docs/', 'scripts/')):
            continue
        path = ROOT / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != record['sha256']:
            failures.append(name)
    for name in filter(None, git('ls-files', '--others', '--exclude-standard', '-z').split('\0')):
        if (name not in ICON_SCOPE and not name.startswith(('docs/', 'scripts/'))) or Path(name).suffix.lower() in ('.dll', '.exe', '.lib', '.zip', '.ptx', '.onnx', '.pth'):
            failures.append(name)
    print(json.dumps(dict(status='fail' if failures else 'pass', base=BASE,
                         failures=failures, baselineFiles=len(records)), indent=2))
    raise SystemExit(bool(failures))

if __name__ == '__main__':
    main()
