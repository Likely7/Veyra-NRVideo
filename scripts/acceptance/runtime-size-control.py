"""Only runtime loading/package changes; HDR is still review-only."""
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BASE='fc2ca7163e986f97e53dcab51ffe1ac77bb0615a'
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,stderr=subprocess.PIPE).decode().strip()
assert git('branch','--show-current')=='codex/runtime-size-20261004'
assert git('rev-parse','checkpoint/pre-runtime-size-20261004^{commit}')==BASE
paths=set(git('diff','--name-only',BASE).splitlines())|set(git('ls-files','--others','--exclude-standard').splitlines())
extra={p for p in paths if p not in {'AGENTS.md','src/pipeline/VfgBackend.cpp'} and not p.startswith(('docs/','scripts/acceptance/runtime-size-'))}
binary={p for p in paths if Path(p).suffix.lower() in {'.dll','.exe','.lib','.pdb','.zip','.7z','.whl','.f16','.f32','.i32','.bin','.onnx','.hsaco','.cso','.ptx','.addon64'}}
assert not extra|binary,'Runtime scope violation: '+str(extra|binary)
print('RUNTIME SIZE SCOPE PASS',len(paths),'paths; runtime/model bytes and HDR code untouched')
