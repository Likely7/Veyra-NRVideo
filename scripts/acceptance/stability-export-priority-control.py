"""Protect the immutable opening worktree/package inventory for this repair."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='stability-export-priority-20261006'
START=BASE/'archives'/TASK/'start.json'
def digest(f):
    with Path(f).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
assert digest(START)=='a5e44bea4c96f4125ea539aefba424efc3450fadf92c89ccff14b6e2f4a91fff'
s=json.loads(START.read_text(encoding='utf8'))
def git(*args,cwd=ROOT):return subprocess.check_output(['git',*args],cwd=cwd,stderr=subprocess.PIPE)
assert ROOT.resolve()==Path(s['worktree']).resolve()
assert git('branch','--show-current').decode().strip()==s['branch']
assert subprocess.run(['git','merge-base','--is-ancestor',s['sourceBase'],'HEAD'],cwd=ROOT).returncode==0
for e in s['preserved_worktrees']:
    p=Path(e['path'])
    assert git('rev-parse','HEAD',cwd=p).decode().strip()==e['head'],str(p)
    assert git('status','--porcelain=v1','-z','--untracked-files=all',cwd=p).decode('utf8')==e['status'],str(p)
    for rel,h in e['modified'].items():
        f=p/rel;assert (digest(f) if f.is_file() else None)==h,str(f)
allowed={'AGENTS.md','CMakeLists.txt','i18n/catalog.json','docs/WORKLOG.md','docs/CURRENT_STATUS.md',
 'docs/STABILITY_EXPORT_PRIORITY_PLAN_2026-10-06.md','docs/STABILITY_EXPORT_PRIORITY_REPORT_2026-10-06.md'}
prefixes=('src/engine/','include/veyra/engine/','src/gfx/','include/veyra/gfx/',
 'src/pipeline/','include/veyra/pipeline/','src/sink/','include/veyra/sink/',
 'src/ui/QmlPlayerBridge.cpp','include/veyra/ui/QmlPlayerBridge.h','qml/Veyra/',
 'tests/unit/','tests/integration/','tests/qml/','scripts/acceptance/stability-export-priority-')
changed=set(git('diff','--name-only',s['sourceBase']).decode().splitlines()+git('ls-files','--others','--exclude-standard').decode().splitlines())
for rel in changed:
    assert rel in allowed or rel.startswith(prefixes),f'Out of scope: {rel}'
    assert Path(rel).suffix.lower() not in ('.dll','.lib','.exe','.hsaco','.bin','.onnx','.zip','.7z','.pdb'),rel
if '--published' in sys.argv:
    for e in s['published']:assert digest(e['path'])==e['sha256'],e['path']
print('GUARD PASS',len(s['preserved_worktrees']),'preserved worktrees;',len(changed),'repair paths')
