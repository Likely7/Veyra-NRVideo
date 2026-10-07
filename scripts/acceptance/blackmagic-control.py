"""Preserve all other worktrees and existing packages during capture compatibility work."""
from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='blackmagic-capture-20261007'
START=BASE/'archives'/TASK/'start.json'
ALLOWED={'AGENTS.md','docs/WORKLOG.md','THIRD_PARTY_NOTICES.md',
 'src/source/CaptureCardSource.cpp','include/veyra/source/CaptureCardSource.h',
 'include/veyra/source/CapturePixelFormat.h','include/veyra/source/CaptureMediaType.h',
 'include/veyra/source/CaptureFormatSelection.h','src/ui/QmlPlayerBridge.cpp','include/veyra/ui/QmlPlayerBridge.h',
 'qml/Veyra/DialogHost.qml','src/engine/EngineController.cpp','include/veyra/engine/EngineController.h',
 'tests/unit/CaptureColorContractTests.cpp','tests/unit/CaptureFormatCases.h','tests/unit/CaptureFormatSelectionTests.cpp',
 'tests/integration/CaptureFormatGpuCases.h'}
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
def allowed(n):return n in ALLOWED or n.startswith(('docs/BLACKMAGIC_CAPTURE_','scripts/acceptance/blackmagic-','include/veyra/source/DirectShowCaptureSetup','include/veyra/source/CaptureSignalProbe','tests/unit/BlackmagicCaptureCases'))
start=json.loads(START.read_text(encoding='utf8'))
assert sha(START)=='6303b4856c05cf6765dfb6ccc8a5f41e02f263c0062e72cb6a34e6394ae40596','Immutable baseline changed'
changed=[n for n,d in start['files'].items() if not (ROOT/n).is_file() or sha(ROOT/n)!=d]
current=set(git('ls-files','-c','-o','--exclude-standard','-z').decode('utf8').split('\0'))-{''}
changed+=sorted(current-set(start['files']))
assert all(allowed(n) for n in changed),[n for n in changed if not allowed(n)]
assert others()==start['otherWorktrees'],'Another worktree changed; preserve and investigate'
for n,d in start['immutable'].items():assert sha(Path(n))==d,n
print('Capture guard passed:',len(set(changed)),'scope files;',len(start['otherWorktrees']),'other worktrees preserved;',len(start['immutable']),'immutable files intact')
