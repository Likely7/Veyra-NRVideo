"""Limit release integration changes to the user-authorised 2.0.2 scope."""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASELINE = Path('E:/项目/Veyra/archives/release-2.0.2-20261003/scope-baseline.json')
ALLOWED = {
    'CMakeLists.txt', 'README.md', 'README_EN.md', 'README_CN.md', 'THIRD_PARTY_NOTICES.md',
    'include/veyra/engine/EnhancementSettings.h', 'src/pipeline/EnhanceGraph.cpp',
    'src/ngx/DlssNrRuntimeAdapter.cpp', 'tests/unit/UiContractTests.cpp',
    'src/engine/PresetLibrary.cpp', 'src/ui/QmlPlayerBridge.cpp', 'qml/Veyra/NrLayerEditor.qml',
    'i18n/catalog.json', 'tests/unit/RepairPresetTests.cpp', 'tests/unit/PresetLibraryTests.cpp',
    'tests/integration/NrRuntimeSwitchTests.cpp', 'scripts/package-qml-release.py',
    'scripts/acceptance/release-2.0.2-control.py', 'scripts/acceptance/release-2.0.2-build.py',
    'scripts/acceptance/release-2.0.2-tests.py', 'scripts/acceptance/release-2.0.2-nr.qml',
    'docs/WORKLOG.md', 'docs/CURRENT_STATUS.md', 'docs/RELEASE_2.0.2_PLAN_2026-10-03.md',
    'docs/RELEASE_2.0.2_RUNTIME_LOCK.json', 'docs/RELEASE_NOTES_2.0.2.md',
    'docs/BUILD_2.0.2.md', 'docs/RUNTIME_COMPONENTS_2.0.2.md', 'docs/GROUP_ANNOUNCEMENT_2.0.2.md',
}
baseline = json.loads(BASELINE.read_text(encoding='utf8'))
names = subprocess.check_output(['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT).decode('utf8').split('\0')
# Baseline hashes are Git blobs. Windows checkout may have CRLF; compare the
# same representation without rewriting either files or the immutable baseline.
lf = {line.split('\t', 1)[1] for line in subprocess.check_output(
    ['git', 'ls-files', '--eol', '-z'], cwd=ROOT).decode('utf8').split('\0') if line.startswith('i/lf ')}
current = {}
for p in names:
    if p and (ROOT/p).is_file():
        data=(ROOT/p).read_bytes()
        if p in lf:
            data=data.replace(b'\r\n', b'\n')
        current[p]=hashlib.sha256(data).hexdigest()
changed = {p for p in current.keys() | baseline['files'].keys() if current.get(p) != baseline['files'].get(p)}
unexpected = changed - ALLOWED
print('RELEASE 2.0.2 SCOPE', 'FAIL' if unexpected else 'PASS', len(changed), 'changed files')
if unexpected:
    print('\n'.join(sorted(unexpected)))
raise SystemExit(bool(unexpected))
