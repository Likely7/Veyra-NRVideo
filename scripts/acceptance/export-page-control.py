"""Scope protection for the isolated export task. Never refresh an existing baseline."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra/archives/export-page-20261002-start')
BASELINE = BASE / 'scope-baseline.json'
BRANCH = 'codex/export-page-20261002'
ALLOW = {
    'AGENTS.md', 'CMakeLists.txt', 'docs/WORKLOG.md',
    'docs/EXPORT_PAGE_OPTIMIZATION_PLAN_2026-10-02.md',
    'docs/EXPORT_PAGE_EXECUTION_2026-10-02.md',
    'qml/Veyra/ExportPage.qml', 'qml/Veyra/Main.qml', 'qml/Veyra/ExportLayout.js',
    'include/veyra/engine/ExportOptions.h', 'include/veyra/engine/EngineController.h',
    'include/veyra/engine/ExportJobManager.h', 'src/engine/ExportJobManager.cpp',
    'src/engine/VideoExportJob.cpp', 'include/veyra/engine/ExportStreams.h', 'src/engine/ExportStreams.cpp',
    'include/veyra/engine/ExportQueue.h', 'src/engine/ExportQueue.cpp',
    'include/veyra/ui/QmlExportQueueModel.h', 'src/ui/QmlExportQueueModel.cpp',
    'include/veyra/ui/QmlPlayerBridge.h', 'src/ui/QmlPlayerBridge.cpp',
    'tests/integration/ExportWorkflowTests.cpp', 'tests/unit/ExportQueueTests.cpp',
    'tests/qml/QuickSmokeTests.cpp', 'tests/qml/quick/tst_export.qml',
    'scripts/acceptance/export-page-build.py', 'scripts/acceptance/export-page-tests.py',
    'scripts/acceptance/export-page-ui.qml',
}

def git(*args):
    return subprocess.check_output(['git', '--no-optional-locks', *args], cwd=ROOT).decode('utf-8').strip()

def files():
    raw = subprocess.check_output(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=ROOT)
    return sorted(set(raw.decode('utf-8').rstrip('\0').split('\0')))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None

if git('branch', '--show-current') != BRANCH:
    raise SystemExit('FAIL: wrong branch')
if ROOT != Path('E:/项目/Veyra/worktrees/export-page-20261002'):
    raise SystemExit('FAIL: wrong workspace')
if len(sys.argv) > 1 and sys.argv[1] == 'init':
    if BASELINE.exists():
        raise SystemExit('Refusing to replace existing scope baseline')
    BASE.mkdir(parents=True, exist_ok=True)
    paths = files()
    baseline = dict(head=git('rev-parse', 'HEAD'), branch=BRANCH,
                    allow=sorted(ALLOW), files={f: sha(ROOT / f) for f in paths})
    BASELINE.write_text(json.dumps(baseline, ensure_ascii=False, indent=2), encoding='utf-8')
    (BASE / 'working-tree.patch').write_bytes(subprocess.check_output(['git', 'diff', '--binary', 'HEAD'], cwd=ROOT))
    for f in paths:
        if f in ALLOW and (ROOT / f).is_file():
            out = BASE / 'source' / f
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes((ROOT / f).read_bytes())
    print('BASELINE', sha(BASELINE), 'files', len(paths))
else:
    b = json.loads(BASELINE.read_text(encoding='utf-8'))
    errors = []
    if b['head'] != git('rev-parse', 'HEAD'):
        errors.append('HEAD changed')
    if b['allow'] != sorted(ALLOW):
        errors.append('Allow list changed')
    for f in set(files()) | set(b['files']):
        if f not in ALLOW and sha(ROOT / f) != b['files'].get(f):
            errors.append(f)
    print(json.dumps(dict(status='FAIL' if errors else 'PASS', checked=len(b['files']), errors=errors), ensure_ascii=False))
    raise SystemExit(bool(errors))
