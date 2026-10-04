"""Scope check for the five fixes authorized on 2026-10-03; no release actions."""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BRANCH = 'codex/field-upgrade-20261003'
BASE = '66cd3e50590766e5a654528ab61e491dc4d30c83'
ALLOWED = {
    'AGENTS.md', 'CMakeLists.txt', 'THIRD_PARTY_NOTICES.md', 'README.md', 'i18n/catalog.json',
    'apps/veyra-qml/main.cpp', 'qml/Veyra/ExportPage.qml', 'qml/Veyra/VBackdrop.qml',
    'qml/Veyra/NrLayerEditor.qml',
    'qml/Veyra/Main.qml', 'src/ui/QmlPlayerBridge.cpp', 'include/veyra/ui/QmlPlayerBridge.h',
    'include/veyra/gfx/PresentationHooks.h', 'src/source/XboxSessionSource.cpp',
    'include/veyra/source/XboxSessionSource.h', 'src/xbox/WebRtcSession.cpp',
    'include/veyra/xbox/DisconnectPolicy.h', 'src/engine/EngineController.cpp',
    'include/veyra/engine/EnhancementSettings.h', 'src/engine/SettingsJson.cpp',
    'src/pipeline/EnhanceGraph.cpp', 'include/veyra/pipeline/EnhanceGraph.h',
    'include/veyra/pipeline/NrInstance.h', 'src/pipeline/NrInstance.cpp',
    'include/veyra/pipeline/LmxxfNrBackend.h', 'src/pipeline/LmxxfNrBackend.cpp',
    'tests/xbox/XboxTests.cpp', 'tests/integration/CaptureAudioTests.cpp',
    'tests/qml/quick/tst_export.qml', 'tests/qml/quick/tst_backdrop.qml',
    'tests/unit/LmxxfNrTests.cpp', 'tests/unit/LmxxfNrFakeRuntime.cpp',
    'tests/unit/PresetLibraryTests.cpp',
    'docs/WORKLOG.md', 'docs/CURRENT_STATUS.md', 'docs/FIELD_UPGRADE_PLAN_2026-10-03.md',
    'docs/FIELD_UPGRADE_ACCEPTANCE_2026-10-03.md', 'docs/AMD_NR_INTEGRATION_2026-10-03.md',
    'scripts/acceptance/field-upgrade-control.py', 'scripts/acceptance/field-upgrade-build.py',
    'scripts/acceptance/field-upgrade-tests.py',
    'scripts/acceptance/field-upgrade-ui.py', 'scripts/acceptance/field-upgrade-ui.qml',
    'scripts/acceptance/field-upgrade-package.py',
    'scripts/acceptance/field-upgrade-clean-smoke.py',
}
def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT).decode('utf8').strip()
if git('branch', '--show-current') != BRANCH or git('rev-parse', 'checkpoint/pre-field-upgrade-20261003^{commit}') != BASE:
    raise SystemExit('FIELD SCOPE FAIL: branch or archive tag changed')
changed = set(filter(None, git('diff', '--name-only', BASE).splitlines()))
changed.update(filter(None, git('ls-files', '--others', '--exclude-standard').splitlines()))
unexpected = {p for p in changed if p not in ALLOWED and not p.startswith(('third_party/lmxxf/','scripts/amd-nr/'))}
binary = {p for p in changed if Path(p).suffix.lower() in {'.dll', '.exe', '.lib', '.zip', '.hsaco', '.bin', '.onnx', '.pdb', '.f16', '.f32', '.i32', '.cso', '.addon64'}}
if unexpected or binary:
    raise SystemExit('FIELD SCOPE FAIL\n' + '\n'.join(sorted(unexpected | binary)))
print('FIELD SCOPE PASS:', len(changed), 'authorized source/document files; archive tag unchanged')
