"""Protect the 2.0.5 integration inputs, other worktrees and published assets."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.5-20261007'
def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def git(*args,cwd=ROOT):return subprocess.check_output(['git',*args],cwd=cwd,stderr=subprocess.PIPE).decode('utf8')
opening=BASE/'archives'/TASK/'start.json'
assert sha(opening)=='48b998d583676c9c39b14cc417c89d294639060f2e2fd4aa1986b366113625c8'
start=json.loads(opening.read_text(encoding='utf8'))
assert ROOT.resolve()==Path(start['worktree']).resolve()
assert git('branch','--show-current').strip()==start['branch']
subprocess.run(['git','merge-base','--is-ancestor',start['sourceBase'],'HEAD'],cwd=ROOT,check=True)
allowed={'AGENTS.md','CMakeLists.txt','docs/CURRENT_STATUS.md','docs/WORKLOG.md',
 'docs/RELEASE_2.0.5_PLAN_2026-10-07.md','docs/RELEASE_2.0.5_ACCEPTANCE_2026-10-07.md','docs/TEST_NOTES_2.0.5.md','docs/BUILD_2.0.5.md'}
def in_scope(name):return name in allowed or name.startswith('scripts/acceptance/release-2.0.5-')
for name,h in start['trackedSha256'].items():
    assert (ROOT/name).is_file(),name
    if sha(ROOT/name)!=h:assert in_scope(name),name
names=set(filter(None,git('ls-files','-z').split('\0')))|set(filter(None,git('ls-files','--others','--exclude-standard','-z').split('\0')))
for name in names-set(start['trackedSha256']):
    assert in_scope(name),name
    assert Path(name).suffix.lower() not in ('.dll','.exe','.lib','.pdb','.onnx','.bin','.hsaco','.zip','.7z','.ptx'),name
preserved=0
for row in start['preserved_worktrees']:
    location=Path(row['path']);head=git('rev-parse','HEAD',cwd=location).strip()
    if location.resolve()==Path(start['mainPath']).resolve() and head!=row['head']:
        merge=json.loads((BASE/'logs'/TASK/'main-merge.json').read_text(encoding='utf8'))
        assert head==merge['mainAfter'] and merge['mainBefore']==row['head']
        previous=row['head']
        for step in merge.get('steps',[merge]):
            assert step['mainBefore']==previous
            assert git('rev-list','--parents','-n','1',step['mainAfter'],cwd=location).strip().split()[1:]==[previous,step['testedSourceCommit']]
            assert git('rev-parse',step['mainAfter']+'^{tree}',cwd=location).strip()==git('rev-parse',step['testedSourceCommit']+'^{tree}').strip()
            previous=step['mainAfter']
        assert previous==head
        assert git('rev-parse',head+'^{tree}',cwd=location).strip()==git('rev-parse',merge['testedSourceCommit']+'^{tree}').strip()
    else:assert head==row['head'],str(location)
    assert git('status','--porcelain=v1','-z','--untracked-files=all',cwd=location)==row['status'],str(location)
    for name,h in row['modified'].items():
        item=location/name;assert (sha(item) if item.is_file() else None)==h,str(item)
    preserved+=1
if '--published' in sys.argv:
    for row in start['published']:assert sha(row['path'])==row['sha256'],row['path']
subprocess.run(['git','diff','--check'],cwd=ROOT,check=True,capture_output=True)
print('GUARD PASS',preserved,'preserved worktrees; inherited product sources unchanged')
