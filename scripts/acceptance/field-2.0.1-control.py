"""Guard this field repair against an immutable, external source baseline."""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASELINE = Path('E:/项目/Veyra/archives/field-2.0.1-20261002/scope-baseline.json')
ALLOWED = {
    'src/xbox/StreamApi.cpp', 'src/xbox/WebRtcSession.cpp',
    'include/veyra/xbox/StreamApi.h', 'tests/xbox/XboxTests.cpp',
    'src/source/XboxSessionSource.cpp',
    'src/engine/EngineController.cpp', 'include/veyra/engine/EngineController.h',
    'src/gfx/PresentSink.cpp', 'include/veyra/gfx/PresentSink.h',
    'src/ui/QmlPlayerBridge.cpp', 'include/veyra/ui/QmlPlayerBridge.h',
    'src/ui/PlayerUiFacade.cpp',
    'apps/veyra-qml/main.cpp', 'qml/Veyra/Main.qml',
    'i18n/catalog.json', 'tests/unit/RepairContractTests.cpp',
    'docs/WORKLOG.md', 'docs/CURRENT_STATUS.md', 'docs/THIRD_PARTY_NOTICES.md',
    'THIRD_PARTY_NOTICES.md', 'docs/RELEASE_NOTES_2.0.1.md',
    'docs/RELEASE_2.0.1_2026-10-02.md', 'docs/FIELD_2.0.1_REPAIR_PLAN_2026-10-02.md',
    'scripts/acceptance/field-2.0.1-control.py', 'scripts/acceptance/field-2.0.1-build.py',
    'scripts/acceptance/field-2.0.1-tests.py', 'scripts/ui-check/field-2.0.1-vram.qml',
}
baseline = json.loads(BASELINE.read_text(encoding='utf8'))
tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
new = subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=ROOT).decode().split('\0')
current = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest()
           for p in tracked + new if p and (ROOT / p).is_file()}
changed = {p for p in current.keys() | baseline['files'].keys()
           if current.get(p) != baseline['files'].get(p)}
unexpected = changed - ALLOWED
print('FIELD SCOPE', 'FAIL' if unexpected else 'PASS', len(changed), 'changed files')
if unexpected:
    print('\n'.join(sorted(unexpected)))
raise SystemExit(bool(unexpected))
