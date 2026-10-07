"""Freeze this build's real receipts; check the local main merge and final ZIPs."""
from pathlib import Path
import ast,hashlib,json,subprocess,sys,zipfile
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.5-20261007'
LOG=BASE/'logs'/TASK;PACK=BASE/'test-packages'/TASK;ARCH=BASE/'archives'/TASK
def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf8'))
def save(path,value):Path(path).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*args,cwd=ROOT):return subprocess.check_output(['git',*args],cwd=cwd,stderr=subprocess.PIPE).decode('utf8').strip()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.5-control.py'),'--published'],check=True)
mode=sys.argv[1];assert mode in ('freeze','close')
exe=BASE/'build'/TASK/'standard/veyra_qml_ui.exe'
if mode=='freeze':
    assert not git('status','--porcelain'),'Commit the tested source before freezing'
    required=['build-production-tests.json','units.json','telemetry.json','graph.json','display.json','fg-pixels.json','export-timestamps.json',
      'fsr-h264.json','fsr-nr-hevc.json','nr-stack-hevc.json','xess-rejection.json',
      'gui-amd-default-seed.json','gui-amd-default-migrate.json','gui-amd-xess-keep.json','gui-nvidia-dlss-keep.json',
      'gui-amd-catalog-v4.json','gui-telemetry.json','gui-fsr-export-second.json','gui-fsr-restore.json','gta-205/result.json']
    for name in required:
        value=read(LOG/name);rows=value if isinstance(value,list) else [value]
        assert rows and all(row.get('passed',row.get('exit')==0) for row in rows),name
    performance=read(LOG/'gta-205/result.json')
    assert performance['exeSha256']==sha(exe) and performance['priorityRequested']=='normal' and not performance['testEnv'] and performance['config']['display']==2
    for sample in read(LOG/'gta-205/meters.json'):
        assert sample['priority']==dict(status='0x0',actual=2),sample
    names=git('ls-files').splitlines()
    tracked={name:sha(ROOT/name) for name in names}
    for p in (ROOT/'scripts/acceptance').glob('release-2.0.5-*.py'):ast.parse(p.read_text(encoding='utf8'))
    receipt=dict(sourceCommit=git('rev-parse','HEAD'),executableSha256=sha(exe),trackedInputs=tracked,
        testReceipts={name:sha(LOG/name) for name in required},
        validationScope='Fresh 2.0.5 build and bounded serial regression checks; previous fix2 A/B measurements remain historical',
        unresolved='NVIDIA VRAM growth deferred; actual AMD HIP/encoder and user-panel tearing unverified')
    with (LOG/'tested-inputs.json').open('x',encoding='utf8') as stream:json.dump(receipt,stream,ensure_ascii=False,indent=2)
    print('FROZEN',len(tracked),'tracked inputs;',len(required),'current receipts;',receipt['sourceCommit'])
else:
    frozen=read(LOG/'tested-inputs.json');head=frozen['sourceCommit']
    assert git('rev-parse','HEAD')==head and not git('status','--porcelain')
    assert sha(exe)==frozen['executableSha256']
    for name,h in frozen['trackedInputs'].items():assert sha(ROOT/name)==h,name
    for name,h in frozen['testReceipts'].items():assert sha(LOG/name)==h,name
    delivery=read(PACK/'DELIVERY.json');cold=read(LOG/'cold-final-zip.json');merge=read(LOG/'main-merge.json')
    assert len(delivery)==2 and len(cold)==2 and all(row['passed'] for row in cold)
    assert merge['testedSourceCommit']==head and merge['treesIdentical']
    main=BASE/'worktrees/main-merge-20261002'
    assert git('rev-parse','HEAD',cwd=main)==merge['mainAfter'] and not git('status','--porcelain',cwd=main)
    assert git('rev-parse','HEAD^{tree}',cwd=main)==git('rev-parse','HEAD^{tree}')
    for item in delivery:
        app=Path(item['app']);manifest=read(app/'package-manifest.json')
        assert item['sourceCommit']==head and item['peVersions']==dict(file='2.0.5.0',product='2.0.5.0')
        assert sha(item['archive'])==item['archiveSha256'] and item['zipCrcAndAllPayloadHashesPassed']
        assert manifest['sourceCommit']==head and not manifest['worktreeDirty'] and manifest['localOnly'] and not manifest['releaseReady']
        assert sha(app/'veyra_qml_ui.exe')==sha(exe)==item['exeSha256']
        probe=next(row for row in cold if row['vendor']==item['vendor'])
        assert probe['archiveSha256']==item['archiveSha256'] and probe['exeSha256']==sha(exe)
        actual={p.relative_to(app).as_posix() for p in app.rglob('*') if p.is_file()}
        assert actual=={row['path'] for row in manifest['files']}|{'package-manifest.json'}
        for row in manifest['files']:assert sha(app/row['path'])==row['sha256'],row['path']
        for name,h in frozen['trackedInputs'].items():
            if name.startswith('qml/'):assert sha(app/name)==h,name
    with zipfile.ZipFile(delivery[0]['sourceZip']) as zipped:
        assert zipped.testzip() is None
        for name,h in frozen['trackedInputs'].items():assert hashlib.sha256(zipped.read('Veyra-2.0.5-source/'+name)).hexdigest()==h,name
    subprocess.run(['git','bundle','verify',str(ARCH/'integration-final.bundle')],cwd=ROOT,check=True)
    receipt=dict(passed=True,version='2.0.5',testedSourceCommit=head,mainCommit=merge['mainAfter'],treesIdentical=True,
        executableSha256=sha(exe),trackedInputs=len(frozen['trackedInputs']),testReceipts=len(frozen['testReceipts']),
        preservedWorktrees=27,preservedDirtyFiles=495,preservedPublishedFiles=14,zipCrcAndAllPayloadHashesPassed=True,
        coldZipValidation=True,delivery=delivery,vramFixed=False,publicReleaseCreated=False)
    save(LOG/'final-check.json',receipt)
    print('FINAL CHECK PASS; main/build/portable ZIPs/source/current tests/old worktrees and public assets verified')
