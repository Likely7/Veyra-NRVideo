"""PR integration scope guard; the opening inventory is immutable."""
from pathlib import Path
import hashlib, json, subprocess, sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'pr19-pr20-20261006'
START = BASE / 'archives' / TASK / 'start.json'
assert hashlib.sha256(START.read_bytes()).hexdigest() == 'f3d49a940e731ef7412d83666525cc37f08464f7a48ad226b9f9a286e947c771'
s = json.loads(START.read_text(encoding='utf8'))

def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', *args], cwd=cwd)

assert ROOT.resolve() == Path(s['worktree']).resolve()
assert git('branch', '--show-current').decode().strip() == s['branch']
assert subprocess.run(['git', 'merge-base', '--is-ancestor', s['sourceBase'], 'HEAD'], cwd=ROOT).returncode == 0
receipt_path = BASE / 'logs' / TASK / 'main-advance.json'
advance = json.loads(receipt_path.read_text(encoding='utf8')) if receipt_path.exists() else None
for e in s['preserved_worktrees']:
    p = Path(e['path'])
    expected = e['head']
    if advance and p.resolve() == Path(s['mainPath']).resolve():
        assert advance['before'] == e['head'] == s['mainBefore']
        expected = advance['after']
        assert subprocess.run(['git', 'merge-base', '--is-ancestor', s['sourceBase'], expected], cwd=ROOT).returncode == 0
        for pr in s['prs']:
            assert subprocess.run(['git', 'merge-base', '--is-ancestor', pr['head'], expected], cwd=ROOT).returncode == 0
    assert git('rev-parse', 'HEAD', cwd=p).decode().strip() == expected, str(p)
    assert git('status', '--porcelain=v1', '-z', '--untracked-files=all', cwd=p).decode('utf8') == e['status'], str(p)
    for rel, h in e['modified'].items():
        f = p / rel
        assert (hashlib.sha256(f.read_bytes()).hexdigest() if f.is_file() else None) == h, str(f)

allowed = {'AGENTS.md', 'CMakeLists.txt', 'THIRD_PARTY_NOTICES.md', 'docs/WORKLOG.md', 'docs/CURRENT_STATUS.md',
 'docs/PR19_PR20_INTEGRATION_PLAN_2026-10-06.md', 'i18n/catalog.json',
 'include/veyra/engine/EffectChain.h', 'include/veyra/engine/EnhancementSettings.h',
 'include/veyra/engine/GraphDescription.h', 'include/veyra/engine/HdrOutputTuning.h',
 'include/veyra/engine/PresetLibrary.h', 'include/veyra/engine/VideoPresenter.h',
 'include/veyra/gfx/PresentSink.h', 'include/veyra/pipeline/EnhanceGraph.h',
 'include/veyra/pipeline/GpuPassUtils.h', 'include/veyra/ui/QmlPlayerBridge.h',
 'qml/Veyra/ProPage.qml', 'qml/Veyra/SettingsPage.qml', 'shaders/PresentBlit.hlsl',
 'src/engine/EffectChain.cpp', 'src/engine/PresetLibrary.cpp', 'src/engine/VideoPresenter.cpp',
 'src/gfx/PresentSink.cpp', 'src/pipeline/EnhanceGraph.cpp', 'src/pipeline/GpuPassUtils.cpp',
 'src/ui/QmlPlayerBridge.cpp', 'src/sink/MfVideoEncoder.cpp',
 'tests/unit/HdrOutputTuningTests.cpp', 'tests/unit/PresetLibraryTests.cpp', 'tests/unit/EffectChainTests.cpp',
 'tests/unit/HdrDisplayStateTests.cpp', 'tests/qml/QuickSmokeTests.cpp',
 'tests/integration/HdrOutputTuningGpuTests.cpp'}
changed = git('diff', '--name-only', s['sourceBase']).decode().splitlines() + git('ls-files', '--others', '--exclude-standard').decode().splitlines()
for rel in changed:
    assert rel in allowed or rel.startswith('scripts/acceptance/pr19-pr20-'), f'Out of scope: {rel}'
    assert Path(rel).suffix.lower() not in ('.dll', '.lib', '.exe', '.hsaco', '.bin', '.onnx', '.zip', '.7z', '.pdb'), rel
if '--published' in sys.argv:
    for e in s['published']:
        assert hashlib.sha256(Path(e['path']).read_bytes()).hexdigest() == e['sha256'], e['path']
print('GUARD PASS', len(s['preserved_worktrees']), 'preserved worktrees;', len(set(changed)), 'authorized paths')
