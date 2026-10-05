from pathlib import Path
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='list-preset-flow-20261006';ARCH=BASE/'archives'/TASK
def git(root,*args):return subprocess.check_output(['git','-c','core.safecrlf=false','-C',str(root),*args],stderr=subprocess.PIPE)
def sha(data):return hashlib.sha256(data).hexdigest()
raw=(ARCH/'start.json').read_bytes();assert sha(raw)==(ARCH/'start.sha256').read_text().strip();start=json.loads(raw)
assert ROOT.resolve()==Path(start['root']).resolve() and git(ROOT,'branch','--show-current').decode().strip()==start['branch']
assert git(ROOT,'merge-base','HEAD',start['codeBase']).decode().strip()==start['codeBase']
allowed={'AGENTS.md','README.md','README_CN.md','i18n/catalog.json','include/veyra/engine/PresetLibrary.h','src/engine/PresetLibrary.cpp',
 'include/veyra/ui/QmlPlayerBridge.h','src/ui/QmlPlayerBridge.cpp','qml/Veyra/ProPage.qml','qml/Veyra/DialogHost.qml',
 'tests/unit/PresetLibraryTests.cpp','docs/WORKLOG.md','docs/CURRENT_STATUS.md','docs/RELEASE_NOTES_2.0.4.md',
 'docs/LIST_PRESET_FLOW_PLAN_2026-10-06.md','docs/LIST_PRESET_FLOW_ACCEPTANCE_2026-10-06.md',
 'scripts/acceptance/list-preset-flow-control.py','scripts/acceptance/list-preset-flow-build.py','scripts/acceptance/list-preset-flow-tests.py',
 'scripts/acceptance/list-preset-flow-ui.qml','scripts/acceptance/list-preset-flow-package.py','scripts/acceptance/list-preset-flow-audit.py',
 'scripts/acceptance/list-preset-flow-cold.py'}
changed=set(git(ROOT,'diff','--name-only',start['codeBase']).decode('utf8').splitlines())
changed.update(p for p in git(ROOT,'ls-files','--others','--exclude-standard','-z').decode('utf8').split('\0') if p)
assert changed<=allowed,sorted(changed-allowed)
assert not any(Path(p).suffix.lower() in {'.dll','.exe','.lib','.pdb','.onnx','.f16','.f32','.hsaco','.ptx','.addon64','.whl'} for p in changed)
for r in start['worktrees']:
    path=Path(r['path'])
    if path.resolve()==Path(start['mainPath']).resolve():
        receipt=ARCH/'main-merge.json';expected=json.loads(receipt.read_text(encoding='utf8'))['mainAfter'] if receipt.exists() else start['mainBefore']
        assert git(path,'rev-parse','HEAD').decode().strip()==expected and not git(path,'status','--porcelain').strip();continue
    assert git(path,'rev-parse','HEAD').decode().strip()==r['head'],str(path)
    for args,key in [(('status','--porcelain=v1','--untracked-files=all'),'statusSha256'),(('diff','--binary'),'workingDiffSha256'),(('diff','--cached','--binary'),'indexDiffSha256')]:assert sha(git(path,*args))==r[key],str(path)
    for rel,digest in r['files'].items():
        p=path/rel;assert (p.is_file() and sha(p.read_bytes())==digest) if digest else not p.exists(),str(p)
for name,digest in start['protectedPackages'].items():
    with Path(name).open('rb') as stream:assert hashlib.file_digest(stream,'sha256').hexdigest()==digest,name
print('LIST PRESET GUARD PASS',len(changed),'paths;',len(start['worktrees'])-1,'other checkouts and previous packages unchanged')
