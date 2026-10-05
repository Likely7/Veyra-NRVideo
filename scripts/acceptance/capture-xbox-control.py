"""Scope guard for the two current field reports; preserves previous worktrees."""
from pathlib import Path
import hashlib, json, subprocess

BASE=Path('E:/项目/Veyra'); TASK='capture-xbox-field-20261005'
ROOT=Path(__file__).resolve().parents[2]; ARCH=BASE/'archives'/TASK
allowed={
 'AGENTS.md','docs/WORKLOG.md','docs/CAPTURE_XBOX_FIELD_PLAN_2026-10-05.md',
 'docs/CAPTURE_XBOX_FIELD_EXECUTION_2026-10-05.md',
 'include/veyra/xbox/VideoDecodeRecovery.h','src/source/XboxSessionSource.cpp',
 'src/engine/EngineController.cpp','tests/xbox/XboxTests.cpp',
 'tests/unit/CaptureCompressedDecoderTests.cpp',
 'scripts/acceptance/capture-xbox-control.py','scripts/acceptance/capture-xbox-build.py',
 'scripts/acceptance/capture-xbox-tests.py','scripts/acceptance/capture-xbox-evidence.py',
 'scripts/acceptance/capture-xbox-package.py'
}
def git(root,*args):return subprocess.check_output(['git','-c','core.safecrlf=false','-C',str(root),*args],stderr=subprocess.PIPE)
def sha(data):return hashlib.sha256(data).hexdigest()
startfile=ARCH/'start.json'
assert sha(startfile.read_bytes())==(ARCH/'start.sha256').read_text().strip(),'immutable start receipt changed'
start=json.loads(startfile.read_text(encoding='utf8'))
assert ROOT.resolve()==Path(start['root']).resolve()
assert git(ROOT,'branch','--show-current').decode().strip()==start['branch']
assert git(ROOT,'merge-base','HEAD',start['start']).decode().strip()==start['start']
changed=set(git(ROOT,'diff','--name-only',start['start']).decode('utf8').splitlines())
changed.update(f for f in git(ROOT,'ls-files','--others','--exclude-standard','-z').decode('utf8').split('\0') if f)
assert changed<=allowed, f'out of scope: {sorted(changed-allowed)}'
for record in start['protected']:
    path=Path(record['path'])
    assert git(path,'rev-parse','HEAD').decode().strip()==record['head'],f'protected HEAD changed: {path}'
    assert sha(git(path,'status','--porcelain=v1','--untracked-files=all'))==record['statusSha256'],f'protected status changed: {path}'
    assert sha(git(path,'diff','--binary'))==record['workingDiffSha256'],f'protected content changed: {path}'
    assert sha(git(path,'diff','--cached','--binary'))==record['indexDiffSha256'],f'protected index changed: {path}'
    for f,digest in record['untracked'].items():assert sha((path/f).read_bytes())==digest,f'protected untracked changed: {path/f}'
print('CAPTURE/XBOX GUARD PASS',len(changed),'scoped files;',len(start['protected']),'worktrees unchanged')
