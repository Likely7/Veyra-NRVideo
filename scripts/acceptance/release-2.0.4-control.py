"""Preserve every other checkout and keep the 2.0.4 integration auditable."""
from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='release-2.0.4-20261005';ARCH=BASE/'archives'/TASK
def git(root,*args):return subprocess.check_output(['git','-c','core.safecrlf=false','-C',str(root),*args],stderr=subprocess.PIPE)
def sha(data):return hashlib.sha256(data).hexdigest()
data=(ARCH/'start.json').read_bytes()
assert sha(data)==(ARCH/'start.sha256').read_text().strip(),'Immutable start changed'
start=json.loads(data);assert ROOT.resolve()==Path(start['root']).resolve()
assert git(ROOT,'branch','--show-current').decode().strip()=='codex/release-2.0.4-20261005'
assert git(ROOT,'merge-base','HEAD',start['codeBase']).decode().strip()==start['codeBase']
ui=next(r for r in start['worktrees'] if Path(r['path']).name=='ui-fixes-20261005')
allowed=set(ui['files'])|{'AGENTS.md','CMakeLists.txt','README.md','README_CN.md','README_EN.md',
 'docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/RELEASE_2.0.4_PLAN_2026-10-05.md',
 'docs/RELEASE_2.0.4_ACCEPTANCE_2026-10-05.md','docs/RELEASE_NOTES_2.0.4.md',
 'docs/BRANCH_MAP_2026-10-05.md','scripts/acceptance/release-2.0.4-control.py',
 'scripts/acceptance/release-2.0.4-build.py','scripts/acceptance/release-2.0.4-tests.py',
 'scripts/acceptance/release-2.0.4-package.py','scripts/acceptance/release-2.0.4-cold.py','scripts/acceptance/release-2.0.4-audit.py',
 'scripts/acceptance/release-2.0.4-ui.qml','tests/qml/quick/tst_slider_value.qml'}
changed=set(git(ROOT,'diff','--name-only',start['codeBase']).decode('utf8').splitlines())
changed.update(p for p in git(ROOT,'ls-files','--others','--exclude-standard','-z').decode('utf8').split('\0') if p)
assert changed<=allowed,sorted(changed-allowed)
assert not any(Path(p).suffix.lower() in {'.dll','.exe','.lib','.pdb','.onnx','.f16','.f32','.hsaco','.ptx','.addon64','.whl'} for p in changed)
for r in start['worktrees']:
    path=Path(r['path'])
    if path.resolve()==Path(start['mainPath']).resolve():
        receipt=ARCH/'main-merge.json'
        expected=json.loads(receipt.read_text(encoding='utf8'))['mainAfter'] if receipt.exists() else start['mainBefore']
        assert git(path,'rev-parse','HEAD').decode().strip()==expected,'Unexpected main modification'
        assert not git(path,'status','--porcelain').strip(),'Main must stay clean'
        continue
    assert git(path,'rev-parse','HEAD').decode().strip()==r['head'],f'Protected HEAD changed: {path}'
    for args,key in [(('status','--porcelain=v1','--untracked-files=all'),'statusSha256'),
                     (('diff','--binary'),'workingDiffSha256'),(('diff','--cached','--binary'),'indexDiffSha256')]:
        assert sha(git(path,*args))==r[key],f'Protected {key} changed: {path}'
    for rel,digest in r['files'].items():
        p=path/rel
        assert (p.is_file() and sha(p.read_bytes())==digest) if digest else not p.exists(),str(p)
print('2.0.4 GUARD PASS',len(changed),'integration paths;',len(start['worktrees'])-1,'other worktrees unchanged')
