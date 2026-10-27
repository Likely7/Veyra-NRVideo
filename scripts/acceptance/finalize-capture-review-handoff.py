"""Verify receipts, protected refs, restored fixture files and immutable runtimes."""
from pathlib import Path
import hashlib,json,subprocess
B=Path('E:/项目/Veyra');T='codex-capture-review-handoff-20261026';R=Path(__file__).resolve().parents[2]
O=B/'reports'/T;S=B/'archives'/T;A=B/'tests'/T/'app';D=B/'build/claude-screen-recording-20261022'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(R),*args],text=True,encoding='utf8').strip()
assert not git('status','--porcelain'),'commit the reviewed source first'
assert git('branch','--show-current')=='codex/capture-review-handoff-20261026'
assert git('rev-parse','main')=='68def10a9c5cdc3af626a0b784029275ebda323f'
assert git('rev-parse','claude/capture-review-20261026')=='b2eb77dfe7c25bcc595d82970f7d95ba977171f9'
before=dict(line.split(' ',1)[::-1] for line in (S/'refs-before.txt').read_text('utf8').splitlines())
after=dict(line.split(' ',1)[::-1] for line in git('show-ref').splitlines())
assert all(after.get(k)==v for k,v in before.items()),'a protected pre-existing ref changed'
other=0
for block in (S/'worktrees-before.txt').read_text('utf8').strip().split('\n\n'):
    fields=dict(line.split(' ',1) for line in block.splitlines() if ' ' in line)
    p=Path(fields['worktree'])
    if p.resolve()==R.resolve():continue
    head=subprocess.check_output(['git','-C',str(p),'rev-parse','HEAD'],text=True).strip();assert head==fields['HEAD'],str(p);other+=1
contracts=json.loads((O/'contracts.json').read_text('utf8'));gpu=json.loads((O/'aa-gpu.json').read_text('utf8'));ui=json.loads((O/'ui.json').read_text('utf8'))
assert len(contracts)==6 and all(x['exit']==0 for x in contracts)
assert {x['case'] for x in gpu}=={'real-frame','sdr-smaa-high','hdr-smaa'} and all(x['exit']==0 for x in gpu)
assert {x['case'] for x in ui}=={'sdr','hdr'} and all(x['passed'] for x in ui)
assert next(x for x in ui if x['case']=='hdr')['screenshot']['file'].endswith('.jxr')
assert sha(A/'veyra_qml_ui.exe')==sha(D/'veyra_qml_ui.exe')
for folder in ['qml','i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file():assert p.read_bytes().replace(b'\r\n',b'\n')==(A/folder/p.relative_to(R/folder)).read_bytes().replace(b'\r\n',b'\n'),str(p)
donor=B/'releases/release-2.1.0-20261019/runtime-base/NVIDIA';manifest=json.loads((donor/'package-manifest.json').read_text('utf8'))
runtime=[x for x in manifest['files'] if Path(x['path']).suffix.lower() in ['.dll','.onnx','.tflite','.hsaco','.dxil']]
for x in runtime:assert sha(A/x['path'])==sha(donor/x['path'])==x['sha256'],x['path']
original=B/'reports/claude-capture-review-20261026'
for name in ['tests.json','capture-round.json','aa-shots.json']:assert sha(original/name)==sha(S/name),name
with (O/'i18n-check.log').open('wb') as f:
    checked=subprocess.run(['python','-X','utf8','-B','-I','scripts/i18n/extract.py','--check'],cwd=R,stdout=f,stderr=subprocess.STDOUT,timeout=90)
assert checked.returncode==0
delta=git('diff','--name-only','b2eb77d','HEAD').splitlines()
assert not any(Path(x).suffix.lower() in ['.dll','.lib','.onnx','.pdb','.hsaco'] for x in delta)
result=dict(completed=True,sourceCommit=git('rev-parse','HEAD'),sourceWorktree=str(R),branch=git('branch','--show-current'),mainUnchanged=git('rev-parse','main'),claudeHeadPreserved=git('rev-parse','claude/capture-review-20261026'),preExistingRefsPreserved=len(before),otherWorktreeHeadsPreserved=other,sourceFilesChanged=delta,exeSha256=sha(A/'veyra_qml_ui.exe'),runtimeFilesUnchanged=len(runtime),contracts=contracts,gpu=gpu,ui=ui,sourceQmlRestored=True,realCaptureRepeated=False,originalCaptureReportPreserved=True,allEffectsCaptureVerified=False,unverifiedCaptureEffects=['SR','Beauty'],audioEndpointChanged=False,lecramPaused=True,merged=False,pushed=False,published=False,packageCreated=False,report=str(R/'docs/CAPTURE_REVIEW_REPORT_2026-10-26.md'))
(O/'FINAL.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8')
(original/'FINAL.json').write_text(json.dumps(dict(completedBy='Codex handoff',sourceCommit=result['sourceCommit'],branch=result['branch'],details=str(O/'FINAL.json'),report=result['report'],realCaptureRepeated=False,allEffectsCaptureVerified=False,mainUnchanged=result['mainUnchanged'],merged=False,pushed=False,published=False,packageCreated=False),ensure_ascii=False,indent=1),'utf8')
print(json.dumps({k:result[k] for k in ['sourceCommit','branch','mainUnchanged','preExistingRefsPreserved','otherWorktreeHeadsPreserved','runtimeFilesUnchanged','realCaptureRepeated','allEffectsCaptureVerified','packageCreated']},ensure_ascii=False))
