"""Protect the independently captured publication baseline and tested product."""
from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='publish-2.0.5-20261007'
ARCH=BASE/'archives'/TASK;LOG=BASE/'logs'/TASK
def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.PIPE).decode('utf8')
opening=ARCH/'start.json'
assert sha(opening)=='a94e3e83c8b80dc16adb1e65c68b508fb9e56f3af5b944b45c264b516c6c9c0d'
start=json.loads(opening.read_text(encoding='utf8'))
assert ROOT.resolve()==Path(start['worktree']).resolve()
assert git(ROOT,'branch','--show-current').strip()==start['branch']
subprocess.run(['git','-C',str(ROOT),'merge-base','--is-ancestor',start['sourceBase'],'HEAD'],check=True)
allowed={'AGENTS.md','README.md','README_CN.md','docs/BUILD_2.0.5.md','docs/CURRENT_STATUS.md','docs/WORKLOG.md',
         'docs/RELEASE_NOTES_2.0.5.md','docs/PUBLISH_2.0.5_PLAN_2026-10-07.md','docs/PUBLISH_2.0.5_REPORT_2026-10-07.md'}
def in_scope(n):return n in allowed or n.startswith('scripts/acceptance/publish-2.0.5-')
for n,h in start['trackedSha256'].items():
    assert (ROOT/n).is_file(),n
    if sha(ROOT/n)!=h:assert in_scope(n),n
names={n for n in git(ROOT,'ls-files','-z').split('\0') if n}|{n for n in git(ROOT,'ls-files','--others','--exclude-standard','-z').split('\0') if n}
assert all(in_scope(n) for n in names-set(start['trackedSha256']))
for row in start['preserved_worktrees']:
    p=Path(row['path']);head=git(p,'rev-parse','HEAD').strip()
    if p.resolve()==Path(start['mainPath']).resolve() and head!=row['head']:
        merge=json.loads((LOG/'main-merge.json').read_text(encoding='utf8'))
        release_head=merge['mainAfter']
        assert merge['mainBefore']==row['head']
        assert git(p,'rev-list','--parents','-n','1',release_head).strip().split()[1:]==[row['head'],merge['sourceCommit']]
        assert git(p,'rev-parse',release_head+'^{tree}').strip()==git(ROOT,'rev-parse',merge['sourceCommit']+'^{tree}').strip()
        if head!=release_head:
            finish=json.loads((LOG/'documentation-close.json').read_text(encoding='utf8'))
            assert finish['releaseMainCommit']==release_head and finish['mainAfter']==head
            assert git(p,'rev-parse',head+'^').strip()==release_head
            changed=set(git(p,'diff','--name-only',release_head,head).splitlines())
            assert changed=={'docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/PUBLISH_2.0.5_REPORT_2026-10-07.md'}
    else:assert head==row['head'],str(p)
    assert git(p,'status','--porcelain=v1','-z','--untracked-files=all')==row['status'],str(p)
    for n,h in row['modified'].items():assert (sha(p/n) if (p/n).is_file() else None)==h,str(p/n)
for row in start['protectedFiles']:assert sha(row['path'])==row['sha256'],row['path']
frozen=json.loads((BASE/'logs/release-2.0.5-20261007/tested-inputs.json').read_text(encoding='utf8'))
for n,h in frozen['testReceipts'].items():assert sha(BASE/'logs/release-2.0.5-20261007'/n)==h,n
assert sha(BASE/'build/release-2.0.5-20261007/standard/veyra_qml_ui.exe')==frozen['executableSha256']
subprocess.run(['git','-C',str(ROOT),'diff','--check'],check=True,capture_output=True)
print('PUBLISH GUARD PASS',len(start['preserved_worktrees']),'worktrees;',len(start['protectedFiles']),'protected files; product and',len(frozen['testReceipts']),'test receipts unchanged',flush=True)
