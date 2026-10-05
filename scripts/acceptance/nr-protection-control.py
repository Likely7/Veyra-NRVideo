"""Task-specific immutable baseline and source-only scope guard."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'nr-strength-protection-20261005'
START = 'de18fc4f71843049dc7fd691898598c17af308f0'
BRANCH = 'codex/' + TASK
ARCHIVE = BASE / 'archives' / TASK
RECORD = ARCHIVE / 'start.json'
ALLOWED = {
    'AGENTS.md', 'README.md', 'README_CN.md', 'README_EN.md', 'THIRD_PARTY_NOTICES.md',
    'docs/WORKLOG.md', 'docs/NR_STRENGTH_PROTECTION_PLAN_2026-10-05.md',
    'docs/NR_STRENGTH_PROTECTION_EXECUTION_2026-10-05.md',
    'include/veyra/engine/EnhancementSettings.h', 'include/veyra/engine/GraphDescription.h',
    'include/veyra/engine/NrCorrectionCodec.h',
    'include/veyra/pipeline/NrTemporalPass.h', 'src/pipeline/NrTemporalPass.cpp',
    'src/pipeline/EnhanceGraph.cpp', 'src/engine/PresetStore.cpp', 'src/engine/PresetLibrary.cpp',
    'shaders/NrResidualComposite.hlsl', 'shaders/NrTemporal.hlsl', 'shaders/NrCorrection.hlsli',
    'src/ui/QmlPlayerBridge.cpp', 'qml/Veyra/NrLayerEditor.qml', 'i18n/catalog.json',
    'CMakeLists.txt', 'cmake/VeyraShaders.cmake',
    'tests/unit/PresetLibraryTests.cpp', 'tests/unit/RepairPresetTests.cpp',
    'tests/unit/EffectChainTests.cpp', 'tests/unit/GraphDescriptionTests.cpp',
    'tests/integration/RepairShaderTests.cpp', 'tests/integration/NrVideoQualityProbe.cpp',
    'tests/integration/NrCorrectionGpuTests.cpp', 'tests/integration/NrTemporalGpuTests.cpp',
    'tests/qml/quick/tst_components.qml',
}

def git(*args, root=ROOT):
    result=subprocess.run(['git', '-C', str(root), *args], encoding='utf-8',stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    if result.returncode:raise RuntimeError(result.stderr)
    return result.stdout.strip()

def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(1024 * 1024), b''): h.update(b)
    return h.hexdigest()

def worktrees():
    result = []
    for block in git('worktree', 'list', '--porcelain').split('\n\n'):
        lines = block.splitlines()
        if not lines: continue
        path = Path(lines[0].removeprefix('worktree '))
        if path.resolve() == ROOT.resolve(): continue
        result.append({'path': str(path), 'head': git('rev-parse', 'HEAD', root=path),
                       'status': git('status', '--porcelain', '--untracked-files=all', root=path)})
    return result

def guard():
    data = json.loads(RECORD.read_text(encoding='utf-8'))
    assert ROOT.resolve() == Path(data['root']).resolve()
    assert git('branch', '--show-current') == BRANCH
    subprocess.run(['git', '-C', str(ROOT), 'merge-base', '--is-ancestor', START, 'HEAD'], check=True)
    assert sha(ARCHIVE / 'source-before.bundle') == data['bundleSha256']
    assert worktrees() == data['otherWorktrees'], 'Other worktree HEAD/status changed: investigate; never reset it.'
    changed = set(git('diff', '--name-only', START).splitlines())
    changed.update(git('ls-files', '--others', '--exclude-standard').splitlines())
    assert all(f in ALLOWED or f.startswith('scripts/acceptance/nr-protection-') for f in changed), sorted(changed - ALLOWED)
    # SDK/runtime/media/build bytes may not enter the source checkout or Git.
    suffixes = {'.dll', '.lib', '.pdb', '.dxil', '.exe', '.zip', '.7z', '.onnx', '.safetensors', '.mp4'}
    assert not any(Path(f).suffix.lower() in suffixes for f in changed)
    print('NR protection guard PASS', len(changed), 'scoped source files; protected worktrees unchanged')

if __name__ == '__main__':
    if sys.argv[1:] == ['start']:
        assert git('branch', '--show-current') == BRANCH and git('rev-parse', 'HEAD') == START
        ARCHIVE.mkdir(parents=True, exist_ok=True)
        record = {'root': str(ROOT), 'branch': BRANCH, 'head': START,
                  'bundleSha256': sha(ARCHIVE / 'source-before.bundle'), 'otherWorktrees': worktrees()}
        with RECORD.open('x', encoding='utf-8') as f: json.dump(record, f, ensure_ascii=False, indent=2)
        print('Immutable baseline created', RECORD, sha(RECORD))
    else:
        assert sys.argv[1:] == ['guard']
        guard()
