"""Freeze tested product inputs and audit preserved negative evidence without rewriting it."""
from pathlib import Path
import hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='obs-export-amdnr-20261006'
LOG=BASE/'logs'/TASK;BUILD=BASE/'build'/TASK
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,v):
    assert not p.exists();p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
before=json.loads((LOG/'export-before-v1.json').read_text(encoding='utf8'))
text=(LOG/'export-before-v1.log').read_text(encoding='utf8',errors='replace')
assert len(before['exports'])==2 and all(int(x['video']['nb_read_frames'])==60 for x in before['exports'])
assert text.count('FIELD_EXPORT_DONE')==2 and 'FIELD_EXPORT_STARTED 2 ' in text and 'FIELD_UI_FAIL 22 ' in text
assert 'source=0 generated=0 holds=0 encoded=0' in text
save(LOG/'export-before-audit-v2.json',dict(passed=True,expectedNegative=True,
    originalReceiptSha256=sha(LOG/'export-before-v1.json'),originalLogSha256=sha(LOG/'export-before-v1.log'),
    explanation='Original assertion expected zero files; actual cases 0/1 succeeded, case 2 (two NR layers) failed before decoding. Original receipt retained unchanged.',
    completedCases=2,failedCase=2,sourceExeSHA=before['sourceExeSHA']))
good=['build-final-v4.json','amd-graph-before-v2.json','amd-graph-after-v1.json','units-after-v1.json','abi-after-v1.json',
      'field-stack-after-v2.json','field-single-final-v1.json','worker-mapping-reject-v2.json','lifecycle-final-v1.json',
      'lifecycle-boundary-final-v1.json','export-final-v2.json','obs-fractional-before-v4.json',
      'obs-fractional-after-v1.json','obs-normal-after-v1.json','export-before-audit-v2.json']
for n in good:
    r=json.loads((LOG/n).read_text(encoding='utf8'))
    if isinstance(r,list):assert r and all(x['expectedPass'] for x in r),n
    elif n.startswith('build'):assert r['exit']==0,n
    else:assert r['passed'],n
exe=sha(BUILD/'veyra_qml_ui.exe')
for n in ['export-final-v2.json','obs-fractional-after-v1.json','obs-normal-after-v1.json']:
    assert json.loads((LOG/n).read_text(encoding='utf8'))['sourceExeSHA']==exe,n
assert '7 cases, 0 failures' in (LOG/'amd-graph-after-v1-veyra_amd_nr_graph_tests.log').read_text(encoding='utf8')
names=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode('utf8').split('\0')
product=[n for n in names if n and (n.startswith(('apps/','src/','include/','qml/','shaders/')) or n in ('CMakeLists.txt','CMakePresets.json'))]
save(LOG/'tested-inputs.json',dict(executableSha256=exe,productInputs={n:sha(ROOT/n) for n in product},
    testReceipts={n:sha(LOG/n) for n in good},localOnly=True,amdHipInferenceVerified=False,affectedObsMachineVerified=False))
print('EVIDENCE PASS',len(product),'product inputs;',len(good),'receipts; final EXE',exe)
