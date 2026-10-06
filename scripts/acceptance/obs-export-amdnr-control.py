"""Independent scope and preservation guard for the three authorized field fixes."""
from pathlib import Path
import hashlib, json, subprocess, sys
ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra'); TASK='obs-export-amdnr-20261006'
START=BASE/'archives'/TASK/'start.json'
assert hashlib.sha256(START.read_bytes()).hexdigest()=='67b1dd86b3550dc92ef9094a370dd09714c7b132e6db5185dddbb01b83f78f38'
s=json.loads(START.read_text(encoding='utf8'))
def git(*args,cwd=ROOT):return subprocess.check_output(['git',*args],cwd=cwd)
assert ROOT.resolve()==Path(s['worktree']).resolve()
assert git('branch','--show-current').decode().strip()==s['branch']
assert subprocess.run(['git','merge-base','--is-ancestor',s['baseline'],'HEAD'],cwd=ROOT).returncode==0
for e in s['preserved_worktrees']:
    p=Path(e['path'])
    assert git('rev-parse','HEAD',cwd=p).decode().strip()==e['head'],str(p)
    assert git('status','--porcelain=v1','-z','--untracked-files=all',cwd=p).decode('utf8')==e['status'],str(p)
    for rel,h in e['modified'].items():
        f=p/rel
        assert (hashlib.sha256(f.read_bytes()).hexdigest() if f.is_file() else None)==h,str(f)
allowed={'AGENTS.md','CMakeLists.txt','THIRD_PARTY_NOTICES.md','docs/WORKLOG.md',
 'docs/OBS_EXPORT_AMD_NR_PLAN_2026-10-06.md','apps/veyra-qml/main.cpp',
 'src/engine/ExportJobManager.cpp','src/engine/EffectChain.cpp','include/veyra/engine/ExportJobManager.h',
 'src/pipeline/LmxxfNrBackend.cpp','include/veyra/pipeline/LmxxfNrBackend.h',
 'src/pipeline/EnhanceGraph.cpp','include/veyra/pipeline/EnhanceGraph.h',
 'src/ui/QmlPlayerBridge.cpp','include/veyra/ui/QmlPlayerBridge.h',
 'tests/integration/ExportWorkflowTests.cpp','tests/integration/LmxxfNrTests.cpp',
 'tests/integration/LmxxfCodecTests.cpp','tests/integration/LmxxfNrGraphTests.cpp','tests/unit/EffectChainTests.cpp'}
prefixes=('scripts/acceptance/obs-export-amdnr-','scripts/amd-nr/','third_party/lmxxf/')
changed=git('diff','--name-only',s['baseline']).decode().splitlines()+git('ls-files','--others','--exclude-standard').decode().splitlines()
for rel in changed:
    assert rel in allowed or rel.startswith(prefixes),f'Out of scope: {rel}'
    assert Path(rel).suffix.lower() not in ('.dll','.lib','.exe','.hsaco','.bin','.onnx','.zip','.7z','.pdb'),rel
for e in s['inputs']:
    assert hashlib.sha256(Path(e['copy']).read_bytes()).hexdigest()==e['sha256']
if '--published' in sys.argv:
    for e in s['published']:assert hashlib.sha256(Path(e['path']).read_bytes()).hexdigest()==e['sha256']
print('GUARD PASS',len(s['preserved_worktrees']),'preserved worktrees;',len(set(changed)),'authorized paths')
