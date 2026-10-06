"""Final byte/preservation check; bookkeeping commits do not replace tested product source."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='obs-export-amdnr-20261006'
LOG=BASE/'logs'/TASK;PACK=BASE/'test-packages'/TASK;ARCH=BASE/'archives'/TASK
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/obs-export-amdnr-control.py'),'--published'],check=True)
assert not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT).strip()
frozen=json.loads((LOG/'tested-inputs.json').read_text(encoding='utf8'))
for rel,h in frozen['productInputs'].items():assert sha(ROOT/rel)==h,rel
for rel,h in frozen['testReceipts'].items():assert sha(LOG/rel)==h,rel
assert sha(BASE/'build'/TASK/'veyra_qml_ui.exe')==frozen['executableSha256']
delivery=json.loads((PACK/'DELIVERY.json').read_text(encoding='utf8'))
cold=json.loads((LOG/'cold-verify-results.json').read_text(encoding='utf8'))
assert len(cold)==2 and all(r['passed'] and r['exit']==0 for r in cold)
for row in delivery:
    assert sha(Path(row['archive']))==row['archiveSha256']
    assert sha(Path(row['sourceZip']))==row['sourceZipSha256']
    assert row['exeSha256']==frozen['executableSha256']
    candidate=Path(row['app']);manifest=json.loads((candidate/'package-manifest.json').read_text(encoding='utf8'))
    assert manifest['sourceCommit']==row['sourceCommit'] and not manifest['worktreeDirty']
    assert sha(candidate/'veyra_qml_ui.exe')==frozen['executableSha256']
    assert all(sha(candidate/r['path'])==r['sha256'] for r in manifest['files'])
    assert any(r['archiveSha256']==row['archiveSha256'] and r['executableSha256']==row['exeSha256'] for r in cold)
head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
productCommit=delivery[0]['sourceCommit']
changed=subprocess.check_output(['git','diff','--name-only',productCommit,head],cwd=ROOT,text=True).splitlines()
assert all(n in ('docs/WORKLOG.md','docs/OBS_EXPORT_AMD_NR_PLAN_2026-10-06.md') or n.startswith('scripts/acceptance/obs-export-amdnr-') for n in changed)
sourceDeps=BASE/'releases/publish-2.0.4-20261006/Veyra-2.0.4-open-source-dependencies.zip'
assert sha(sourceDeps)=='4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9'
result=dict(passed=True,productSourceCommit=productCommit,bookkeepingHead=head,
    executableSha256=frozen['executableSha256'],productInputs=len(frozen['productInputs']),receipts=len(frozen['testReceipts']),
    candidates=delivery,coldReceiptSha256=sha(LOG/'cold-verify-results.json'),cleanup=json.loads((LOG/'cleanup.json').read_text(encoding='utf-8-sig')),
    dependencySourceArchive=str(sourceDeps),dependencySourceSha256=sha(sourceDeps),
    dependencySourceURL='https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.4/Veyra-2.0.4-open-source-dependencies.zip',
    mainMerged=False,pushed=False,published=False,amdHipInferenceVerified=False,affectedObsMachineVerified=False)
out=LOG/'final-check.json';assert not out.exists();out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
sums={Path(r['archive']).name:r['archiveSha256'] for r in delivery};sums[Path(delivery[0]['sourceZip']).name]=delivery[0]['sourceZipSha256']
with (PACK/'SHA256SUMS.txt').open('x',encoding='ascii') as f:f.write(''.join(h+'  '+n+'\n' for n,h in sums.items()))
print('FINAL PASS',head,len(frozen['productInputs']),'unchanged product inputs;',len(delivery),'verified local candidates')
