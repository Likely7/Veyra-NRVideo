"""Scope check for the local RTSS restart-loop repair; no binary/SDK additions."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = '354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff'
allowed = {'apps/veyra-qml/main.cpp', 'src/ui/QmlPlayerBridge.cpp',
           'include/veyra/ui/QmlPlayerBridge.h', 'include/veyra/gfx/PresentationHooks.h',
           'qml/Veyra/Main.qml', 'qml/Veyra/SettingsPage.qml', 'i18n/catalog.json',
           'docs/WORKLOG.md', 'docs/CURRENT_STATUS.md',
           'docs/RTSS_COMPAT_2026-10-03.md',
           'docs/FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md',
           'docs/RELEASE_2.0.3_ACCEPTANCE_2026-10-04.md',
           'docs/RTSS_RESTART_LOOP_PLAN_2026-10-04.md'}
paths = set(subprocess.check_output(['git', 'diff', '--name-only', BASE], cwd=ROOT).decode().splitlines())
paths.update(subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard'], cwd=ROOT).decode().splitlines())
assert not [p for p in paths if p not in allowed and not p.startswith('scripts/acceptance/rtss-restart-')], paths
assert not [p for p in paths if Path(p).suffix.lower() in {'.exe', '.dll', '.lib', '.pdb', '.ptx', '.hsaco', '.onnx', '.f16', '.bin'}], paths
assert subprocess.check_output(['git', 'rev-parse', 'main'], cwd=ROOT).decode().strip() == BASE
subprocess.run(['git', 'diff', '--check'], cwd=ROOT, check=True)
print('RTSS repair scope PASS', len(paths), 'source paths; main unchanged')
