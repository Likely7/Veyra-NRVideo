"""Immutable scope/ownership baseline for the explicitly authorized VFG work."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007'
START=BASE/'archives'/TASK/'start.json'
ALLOWED={'AGENTS.md','docs/WORKLOG.md','src/pipeline/VfgBackend.cpp','include/veyra/pipeline/VfgBackend.h',
 'src/pipeline/EnhanceGraph.cpp','include/veyra/pipeline/EnhanceGraph.h','src/engine/EngineController.cpp',
 'tests/integration/VfgGpuTests.cpp'}
def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*args,cwd=ROOT):return subprocess.check_output(['git',*args],cwd=cwd)
def others():
 output=[]
 for line in git('worktree','list','--porcelain').decode('utf8').splitlines():
  if not line.startswith('worktree '):continue
  path=Path(line[9:])
  if path.resolve()==ROOT.resolve():continue
  names=git('ls-files','-m','-o','--exclude-standard','-z',cwd=path).decode('utf8').split('\0')
  output.append({'path':str(path),'head':git('rev-parse','HEAD',cwd=path).decode().strip(),'statusHex':git('status','--porcelain=v1','-z','--untracked-files=all',cwd=path).hex(),
   'changed':{name:sha(path/name) if (path/name).is_file() else None for name in sorted(set(names)-{''})}})
 return output
def allowed(name):return name in ALLOWED or name.startswith(('docs/VFG_OPTIMIZATION_','scripts/acceptance/vfg-opt-','include/veyra/pipeline/Vfg','include/veyra/engine/Vfg','tests/unit/Vfg'))
if '--start' in sys.argv:
 assert not START.exists();assert git('rev-parse','HEAD').decode().strip().startswith('aec800e')
 files={name:sha(ROOT/name) for name in git('ls-files','-z').decode('utf8').split('\0') if name and (ROOT/name).is_file()}
 package=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-NVIDIA-win64-portable'
 runtime=json.loads((package/'vfg-runtime-manifest.json').read_text(encoding='utf-8-sig'))
 immutable={str(package/'veyra_qml_ui.exe'):sha(package/'veyra_qml_ui.exe'),str(package/'qml/Veyra/Main.qml'):sha(package/'qml/Veyra/Main.qml')}
 immutable.update({str(package/f['path']):sha(package/f['path']) for f in runtime['files']})
 START.parent.mkdir(parents=True,exist_ok=True);START.write_text(json.dumps({'head':git('rev-parse','HEAD').decode().strip(),'files':files,'otherWorktrees':others(),'immutable':immutable},ensure_ascii=False,indent=2),encoding='utf8')
 print('VFG baseline created',START)
else:
 start=json.loads(START.read_text(encoding='utf8'));changed=[]
 for name,digest in start['files'].items():
  if not (ROOT/name).is_file() or sha(ROOT/name)!=digest:changed.append(name)
 tracked=set(start['files']);current=set(git('ls-files','-c','-o','--exclude-standard','-z').decode('utf8').split('\0'))-{''}
 changed+=sorted(current-tracked)
 assert all(allowed(name) for name in changed),[name for name in changed if not allowed(name)]
 assert others()==start['otherWorktrees'],'Another worktree changed; preserve and investigate'
 for name,digest in start['immutable'].items():assert sha(Path(name))==digest,name
 print('VFG guard passed; other worktrees, published runtime and non-VFG files preserved;',len(set(changed)),'allowed files')
