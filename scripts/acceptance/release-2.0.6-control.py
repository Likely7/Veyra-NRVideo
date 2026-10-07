"""Check the independent AMD NR -> FSR repair scope and preserved inputs."""
from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.6-20261007'
START=BASE/'archives'/TASK/'start.json'
ALLOWED={'AGENTS.md','CMakeLists.txt','README.md','README_CN.md','docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/RELEASE_SUPPORT.md','docs/images/2.0.6/community-group.png'}
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*args,cwd=ROOT):return subprocess.check_output(['git',*args],cwd=cwd)
def others():
 out=[]
 for line in git('worktree','list','--porcelain').decode('utf8').splitlines():
  if not line.startswith('worktree '):continue
  p=Path(line[9:])
  if p.resolve()==ROOT.resolve():continue
  names=set(git('ls-files','-m','-o','--exclude-standard','-z',cwd=p).decode('utf8').split('\0'))-{''}
  out.append({'path':str(p),'head':git('rev-parse','HEAD',cwd=p).decode().strip(),
   'statusHex':git('status','--porcelain=v1','-z','--untracked-files=all',cwd=p).hex(),
   'changed':{n:sha(p/n) if (p/n).is_file() else None for n in sorted(names)}})
 return out
def allowed(n):return n in ALLOWED or n.startswith(('docs/RELEASE_2.0.6_','docs/RELEASE_NOTES_2.0.6','docs/BUILD_2.0.6','scripts/acceptance/release-2.0.6-'))
assert sha(START)=='ee8aab7b3484eabc90d55b59547dd5bd7934b14385ebff791c6f11b2eab3aba4','Immutable baseline changed'
start=json.loads(START.read_text(encoding='utf8'))
changed=[n for n,d in start['files'].items() if not (ROOT/n).is_file() or sha(ROOT/n)!=d]
current=set(git('ls-files','-c','-o','--exclude-standard','-z').decode('utf8').split('\0'))-{''}
changed+=sorted(current-set(start['files']))
assert all(allowed(n) for n in changed),[n for n in changed if not allowed(n)]
currentOthers=others()
for actual,previous in zip(currentOthers,start['otherWorktrees']):
 assert actual['path']==previous['path']
 if Path(actual['path']).resolve()==Path(start['mainPath']).resolve() and actual['head']!=previous['head']:
  receipt=json.loads((BASE/'logs'/TASK/'main-merge.json').read_text(encoding='utf8'))
  assert receipt['mainBefore']==previous['head']
  releaseMain=receipt['mainAfter']
  assert git('rev-list','--parents','-n','1',releaseMain).decode().split()[1:]==[previous['head'],receipt['sourceCommit']]
  assert git('rev-parse',releaseMain+'^{tree}')==git('rev-parse',receipt['sourceCommit']+'^{tree}')
  if actual['head']!=releaseMain:
   close=json.loads((BASE/'logs'/TASK/'documentation-close.json').read_text(encoding='utf8'))
   assert close['releaseMainCommit']==releaseMain and close['mainAfter']==actual['head']
   assert git('rev-list','--parents','-n','1',actual['head']).decode().split()[1:]==[releaseMain]
   expected={'docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/RELEASE_2.0.6_REPORT_2026-10-07.md'}
   assert set(git('diff','--name-only',releaseMain,actual['head']).decode().splitlines())==set(close['files'])==expected
  actual=dict(actual,head=previous['head'])
 assert actual==previous,'Another worktree changed: '+actual['path']
assert len(currentOthers)==len(start['otherWorktrees'])
for n,d in start['immutable'].items():assert sha(Path(n))==d,n
print('2.0.6 release guard passed:',len(set(changed)),'scope files;',len(start['otherWorktrees']),'other worktrees preserved;',len(start['immutable']),'immutable files intact')
