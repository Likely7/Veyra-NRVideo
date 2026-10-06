"""Protect prior worktrees/packages and the exact product inputs already tested."""
from pathlib import Path
import subprocess,json,hashlib
BASE=Path('E:/项目/Veyra');TASK='publish-2.0.4-20261006';ROOT=Path(__file__).resolve().parents[2];ARCH=BASE/'archives'/TASK
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(p,*a):return subprocess.check_output(['git','-C',str(p),*a],stderr=subprocess.PIPE).decode('utf8').strip()
start=ARCH/'start.json';assert sha(start)==(ARCH/'start.sha256').read_text().strip()
baseline=json.loads(start.read_text(encoding='utf8'))
mainReceipt=ARCH/'main-merge.json'
main=json.loads(mainReceipt.read_text(encoding='utf8')) if mainReceipt.exists() else {}
for row in baseline['worktrees']:
    p=Path(row['path']);expected=main.get('mainAfter',row['head']) if str(p).replace('\\','/').endswith('/main-merge-20261002') else row['head']
    assert git(p,'rev-parse','HEAD')==expected,str(p)
    assert git(p,'status','--porcelain=v1','-z')==row['status'],str(p)
    for rel,digest in row['dirty'].items():assert sha(p/rel)==digest,str(p/rel)
frozen=json.loads((BASE/'logs/list-preset-flow-20261006/tested-inputs.json').read_text(encoding='utf8'))
for rel,digest in frozen['productInputs'].items():assert sha(ROOT/rel)==digest,rel
for name,digest in frozen['testReceipts'].items():assert sha(BASE/'logs/list-preset-flow-20261006'/(name+'.json'))==digest,name
allowed={'AGENTS.md','README.md','README_CN.md','docs/RELEASE_NOTES_2.0.4.md','docs/RELEASE_SUPPORT.md','docs/BUILD_2.0.4.md',
         'docs/PUBLISH_2.0.4_PLAN_2026-10-06.md','docs/PERF_RELEASE_REPORT_2.0.4_2026-10-06.md','docs/WORKLOG.md','docs/CURRENT_STATUS.md',
         'scripts/package-2.0.4-dependency-source.py'}
allowed.update('scripts/acceptance/publish-2.0.4-'+n+'.py' for n in ('control','package','audit','cold'))
changed=set(git(ROOT,'diff','--name-only',baseline['mergeHead']).splitlines())
changed.update(git(ROOT,'ls-files','--others','--exclude-standard').splitlines())
assert not changed-allowed,sorted(changed-allowed)
for row in json.loads((BASE/'test-packages/list-preset-flow-20261006/DELIVERY.json').read_text(encoding='utf8')):
    assert sha(Path(row['archive']))==row['archiveSha256']
    assert sha(Path(row['sourceZip']))==row['sourceZipSha256']
print('PUBLISH GUARD PASS',len(baseline['worktrees']),'protected worktrees;',len(frozen['productInputs']),'frozen product inputs')
