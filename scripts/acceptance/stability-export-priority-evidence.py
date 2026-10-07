"""Freeze the measured product; audit the final portable archives separately."""
from pathlib import Path
import hashlib,json,subprocess,sys,zipfile
ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='stability-export-priority-20261006'
LOG=BASE/'logs'/TASK;ARCH=BASE/'archives'/TASK;PACK=BASE/'test-packages'/TASK
EXE=BASE/'build'/TASK/'standard/veyra_qml_ui.exe'
def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf8'))
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True,encoding='utf8').strip()
def check_frozen(frozen):
    assert sha(EXE)==frozen['executableSha256']
    for rel,h in frozen['productInputs'].items():assert sha(ROOT/rel)==h,rel
    for rel,h in frozen['testReceipts'].items():assert sha(LOG/rel)==h,rel
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/stability-export-priority-control.py'),'--published'],check=True)
mode=sys.argv[1];assert mode in ('freeze','close')
if mode=='freeze':
    assert not git('status','--porcelain'),'Commit the reviewed source before freezing'
    old=read(ARCH/'before-amd-default/tested-inputs.json')
    assert sha(ARCH/'before-amd-default/veyra_qml_ui.exe')==old['executableSha256']
    changed={rel for rel,h in old['productInputs'].items() if sha(ROOT/rel)!=h}
    assert changed=={'src/ui/QmlPlayerBridge.cpp','qml/Veyra/ProPage.qml','qml/Veyra/NodePage.qml','qml/Veyra/qmldir'},changed
    receipts=dict(old['testReceipts'])
    for rel,h in receipts.items():assert sha(LOG/rel)==h,rel
    additions=['build-amd-default-final.json','build-telemetry-tests.json','units-amd-default.json','telemetry-second.json',
      'gui-amd-default-seed.json','gui-amd-default-old-second.json','gui-amd-default-migrate.json',
      'gui-amd-xess-keep.json','gui-nvidia-dlss-keep.json','gui-telemetry-lifecycle.json','gui-fsr-final.json','gui-fsr-final-timestamps.json']
    for rel in additions:
        value=read(LOG/rel)
        if isinstance(value,list):assert all(r['passed'] for r in value),rel
        else:assert value.get('passed',value.get('exit')==0),rel
        receipts[rel]=sha(LOG/rel)
    assert read(LOG/'telemetry-second.json')['componentSha256']==sha(ROOT/'qml/Veyra/VTelemetryValue.qml')
    for label in ('gta-meter-final-c3','gta-meter-final-c4','m2-meter-final-e1'):
        result=read(LOG/label/'result.json')
        assert result['passed'] and result['exeSha256']==sha(EXE)
        assert not result['testEnv'] and result['config']['display']==2
        for name,h in result['uiSourceHashes'].items():assert sha(ROOT/'qml/Veyra'/name)==h,name
    for label in ('gta-meter-final-c3','gta-meter-final-c4','gta-ui-reversal-d1','gta-old-bracket-a5','m2-meter-final-e1','m2-old-final-e2'):
        for name in ('result.json','samples.json','meters.json','raw-timing.json','player.log'):
            rel=label+'/'+name;receipts[rel]=sha(LOG/rel)
    comparison=read(LOG/'performance-final-comparison.json')
    assert abs(comparison['gtaMatchedDifferencePercent'])<1 and abs(comparison['m2MatchedDifferencePercent'])<1
    assert all(r['passed'] and r['steadyPriorityStates']==[['0x0',2]] for r in comparison['records'])
    receipts['performance-final-comparison.json']=sha(LOG/'performance-final-comparison.json')
    product={rel:sha(ROOT/rel) for rel in git('ls-files').splitlines()
             if rel.startswith(('cmake/','include/','src/','shaders/','qml/','apps/','i18n/')) or rel in ('CMakeLists.txt','CMakePresets.json')}
    frozen=dict(executableSha256=sha(EXE),productInputs=product,testReceipts=receipts,sourceCommit=git('rev-parse','HEAD'),
      dependencyCache=old['dependencyCache'],
      validationScope='Prior engine/export/pixel receipts use archived 21e8 EXE and unchanged engine inputs; final 65fc EXE adds AMD settings policy, measured final QML and GUI lifecycle/FSR export. No claim of rerunning every GPU case.',
      archivedPreviousFreezeSha256=sha(ARCH/'before-amd-default/tested-inputs.json'))
    check_frozen(frozen);save(LOG/'tested-inputs.json',frozen)
    print('FROZEN',len(product),'product inputs;',len(receipts),'receipts;',frozen['sourceCommit'])
else:
    frozen=read(LOG/'tested-inputs.json');check_frozen(frozen)
    delivery=read(PACK/'DELIVERY.json');cold=read(LOG/'cold-final-zip.json')
    assert len(delivery)==2 and len(cold)==2 and all(r['passed'] for r in cold)
    for item in delivery:
        app=Path(item['app']);manifest=read(app/'package-manifest.json')
        assert sha(Path(item['archive']))==item['archiveSha256'] and item['zipCrcAndAllPayloadHashesPassed']
        assert manifest['sourceCommit']==frozen['sourceCommit'] and not manifest['worktreeDirty']
        assert manifest['localOnly'] and not manifest['releaseReady'] and manifest['executableSha256']==sha(EXE)
        assert sha(Path(item['sourceZip']))==item['sourceZipSha256']
        probe=next(r for r in cold if r['vendor']==item['vendor'])
        assert probe['archiveSha256']==item['archiveSha256'] and probe['exeSha256']==sha(EXE)
        for rel,h in frozen['productInputs'].items():
            if rel.startswith('qml/'):assert sha(app/rel)==h,rel
    with zipfile.ZipFile(delivery[0]['sourceZip']) as zipped:
        assert zipped.testzip() is None
        for rel,h in frozen['productInputs'].items():
            assert hashlib.sha256(zipped.read('Veyra-2.0.4-fix2-source/'+rel)).hexdigest()==h,rel
    subprocess.run(['git','bundle','verify',str(ARCH/'repair-final.bundle')],cwd=ROOT,check=True)
    save(LOG/'final-check.json',dict(passed=True,productCodeCommit=frozen['sourceCommit'],currentHead=git('rev-parse','HEAD'),
         productInputs=len(frozen['productInputs']),testReceipts=len(frozen['testReceipts']),delivery=delivery,coldZipValidation=True,
         archivedBundleSha256=sha(ARCH/'repair-final.bundle'),preservedWorktrees=25,preservedPublishedFiles=14))
    print('FINAL CHECK PASS; product/source/archives/cold-start/protected worktrees unchanged')
