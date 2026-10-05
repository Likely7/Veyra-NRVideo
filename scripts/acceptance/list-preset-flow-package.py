"""Create the two 2.0.4 candidates from audited payload lists and one production EXE."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,zipfile
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='list-preset-flow-20261006';VERSION='2.0.4'
PACK=BASE/'test-packages'/TASK;BUILD=BASE/'build'/TASK;LOG=BASE/'logs'/TASK;ARCH=BASE/'archives'/TASK
mode=sys.argv[1];assert mode in ('stage','refresh','finalize')
label=sys.argv[2] if len(sys.argv)>2 else mode
assert label.replace('-','').isalnum()
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/list-preset-flow-control.py')]
subprocess.run(guard,check=True)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,obj):p.write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args],encoding='utf8',stderr=subprocess.PIPE).strip()
def private(rel):return any(part.startswith('user-data-') for part in Path(rel).parts) or rel.startswith(('logs/','tmp/','outputs/','screenshots/','profile/'))
def component(rel):return rel.startswith(('runtime/','runtime_local/','models/','licenses/')) or Path(rel).suffix.lower()=='.dll' or rel in ('release-runtime-manifest.json','vfg-runtime-manifest.json')
head=git('rev-parse','HEAD');dirty=bool(git('status','--porcelain'))
sourcezip=PACK/f'Veyra-{VERSION}-veyra-source.zip'
if mode=='finalize':
    assert not dirty,'Validated source must be committed before final packaging'
    frozen=json.loads((LOG/'tested-inputs.json').read_text(encoding='utf8'))
    assert frozen['executableSha256']==sha(BUILD/'veyra_qml_ui.exe')
    for rel,digest in frozen['productInputs'].items():assert sha(ROOT/rel)==digest,'Tested product changed: '+rel
    for name,digest in frozen['testReceipts'].items():assert sha(LOG/(name+'.json'))==digest,'Test receipt changed: '+name
    merge=json.loads((ARCH/'main-merge.json').read_text(encoding='utf8'))
    main=Path(merge['mainPath'])
    assert subprocess.check_output(['git','-C',str(main),'diff','--name-only',head,'HEAD'],text=True).strip()==''
    names=['preset3','chain1','repair1','legacy1','i18n1','qml1','ui3','uirestore1','hot1','nrexport2','nrrestore1','smokeAMD1','smokeNVIDIA1','smokeNative1']
    for name in names:
        result=json.loads((LOG/(name+'.json')).read_text(encoding='utf8'));assert result['passed'],name
        if name.startswith(('ui','hot','nr','smoke')):assert result['exeSha256']==sha(BUILD/'veyra_qml_ui.exe'),name
    assert not sourcezip.exists()
    files=[f for f in subprocess.check_output(['git','-C',str(ROOT),'ls-files','-z']).decode('utf8').split('\0') if f]
    with zipfile.ZipFile(sourcezip,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for rel in sorted(files):
            assert not rel.startswith(('runtime/','runtime_local/','third_party_local/','models/'))
            assert Path(rel).suffix.lower() not in ('.dll','.exe','.lib','.pdb','.addon64','.onnx','.bin','.hsaco','.ptx','.f16','.f32')
            z.write(ROOT/rel,arcname=f'Veyra-{VERSION}-source/'+rel)
    with zipfile.ZipFile(sourcezip) as z:
        assert z.testzip() is None
        for rel in files:assert hashlib.sha256(z.read(f'Veyra-{VERSION}-source/'+rel)).hexdigest()==sha(ROOT/rel)
    bundle=ARCH/'source-final.bundle';assert not bundle.exists()
    subprocess.run(['git','-C',str(ROOT),'bundle','create',str(bundle),'HEAD'],check=True)
    with (LOG/'source-final-verify.log').open('xb') as out:subprocess.run(['git','-C',str(ROOT),'bundle','verify',str(bundle)],stdout=out,stderr=subprocess.STDOUT,check=True)
delivery=[]
for vendor in ('AMD','NVIDIA'):
    source=BASE/'test-packages/release-2.0.4-20261005'/f'Veyra-2.0.4-{vendor}-win64-portable'
    app=PACK/f'Veyra-{VERSION}-{vendor}-win64-portable'
    manifest=json.loads((source/'package-manifest.json').read_text(encoding='utf8'))
    sourcepaths=[];immutable={}
    for row in manifest['files']:
        rel=row['path'];p=source/rel
        assert not private(rel) and p.resolve().is_relative_to(source.resolve())
        assert p.is_file() and p.stat().st_size==row['size'] and sha(p)==row['sha256'],str(p)
        sourcepaths.append(rel)
        if component(rel):immutable[rel]=row['sha256']
    if mode=='stage':
        assert not app.exists();app.mkdir(parents=True)
        for rel in sourcepaths:
            target=app/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source/rel,target)
    assert app.is_dir()
    # The export subprocess writes under its runtime even when the parent log
    # is redirected. Preserve only this task's candidate logs outside the pack.
    workerlogs=app/'logs'
    if workerlogs.exists():
        target=LOG/'candidate-worker-logs'/(vendor+'-'+label)
        assert workerlogs.resolve().is_relative_to(PACK.resolve()) and not workerlogs.is_symlink()
        assert target.resolve().is_relative_to(LOG.resolve()) and not target.exists()
        assert all(p.is_file() and not p.is_symlink() and p.name.startswith('export-worker-') and p.suffix=='.log' for p in workerlogs.iterdir())
        target.parent.mkdir(parents=True,exist_ok=True)
        preserved=[{'file':p.name,'sha256':sha(p)} for p in workerlogs.iterdir()]
        shutil.move(str(workerlogs),str(target))
        save(target.with_suffix('.json'),{'from':str(workerlogs),'to':str(target),'files':preserved})
    if mode in ('stage','refresh'):
        shutil.copy2(BUILD/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
        shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
        shutil.copytree(BUILD/'shaders',app/'shaders',dirs_exist_ok=True)
        for name in ('README.md','README_CN.md','README_EN.md','THIRD_PARTY_NOTICES.md'):shutil.copy2(ROOT/name,app/name)
        for name in ('RELEASE_2.0.4_PLAN_2026-10-05.md','RELEASE_2.0.4_ACCEPTANCE_2026-10-05.md','RELEASE_NOTES_2.0.4.md','BRANCH_MAP_2026-10-05.md','LIST_PRESET_FLOW_PLAN_2026-10-06.md','LIST_PRESET_FLOW_ACCEPTANCE_2026-10-06.md'):
            p=ROOT/'docs'/name
            if p.is_file():shutil.copy2(p,app/'docs'/name)
        (app/'TEST_2.0.4.md').write_text('Veyra 2.0.4 本地测试候选 / local test candidate\n\n'
            '整合 NR 强度5及三风格自动/手动调控、Xbox解码错误恢复、Claude UI修复、性能/计时/AMD NR/字幕及此前本地修复。'
            '公开版本仍为2.0.3，本包用于发布前测试。请先在新目录解压，不复制旧配置目录或混装DLL。\n\n'
            '重点复测：真实Xbox补帧热切换、采集60帧及后台播放、RX9000 NR、列表预设的光流与内容节奏保存/恢复。'
            '本机RTX5070短测不能代替对应显卡或主机实测，软件解码回退可能增加CPU与延迟；默认GPU优先级为实时，旧选择保留。\n\n'
            '详见docs/RELEASE_NOTES_2.0.4.md和docs/RELEASE_2.0.4_ACCEPTANCE_2026-10-05.md。\n',encoding='utf8')
    assert sha(app/'veyra_qml_ui.exe')==sha(BUILD/'veyra_qml_ui.exe')
    for p in (ROOT/'qml').rglob('*'):
        if p.is_file():assert sha(app/'qml'/p.relative_to(ROOT/'qml'))==sha(p)
    for rel,digest in immutable.items():assert sha(app/rel)==digest,rel
    expected=set(sourcepaths)|{'package-manifest.json','TEST_2.0.4.md',
        'docs/RELEASE_2.0.4_PLAN_2026-10-05.md','docs/RELEASE_2.0.4_ACCEPTANCE_2026-10-05.md','docs/RELEASE_NOTES_2.0.4.md','docs/BRANCH_MAP_2026-10-05.md','docs/LIST_PRESET_FLOW_PLAN_2026-10-06.md','docs/LIST_PRESET_FLOW_ACCEPTANCE_2026-10-06.md'}
    expected.update('qml/'+p.relative_to(ROOT/'qml').as_posix() for p in (ROOT/'qml').rglob('*') if p.is_file())
    expected.update('shaders/'+p.relative_to(BUILD/'shaders').as_posix() for p in (BUILD/'shaders').rglob('*') if p.is_file())
    actual={p.relative_to(app).as_posix() for p in app.rglob('*') if p.is_file()}
    assert not actual-expected,sorted(actual-expected)
    assert not any(private(rel) or (app/rel).is_symlink() or Path(rel).suffix.lower() in ('.pdb','.lib','.addon64','.log','.dmp','.pyc','.whl') for rel in actual)
    records=[{'path':rel,'size':(app/rel).stat().st_size,'sha256':sha(app/rel)} for rel in sorted(actual-{'package-manifest.json'})]
    manifest.update(version=VERSION,candidate=VERSION,displayVersion=VERSION,sourceCommit=head,sourceArchiveCommit=head,
        baseCommit=head,worktreeDirty=dirty,localOnly=True,releaseReady=False,executableSha256=sha(app/'veyra_qml_ui.exe'),
        validationRecord='docs/LIST_PRESET_FLOW_ACCEPTANCE_2026-10-06.md',files=records,
        hardwareValidation='RTX5070 local regression only; actual Xbox/RX9070XT, RX9000 NR, RTX20/30/40 and affected capture card remain unverified')
    if mode=='finalize':manifest['correspondingSource'].update(application='../'+sourcezip.name,applicationSha256=sha(sourcezip),displayVersionOverride='-DVEYRA_DISPLAY_VERSION=2.0.4')
    save(app/'package-manifest.json',manifest)
    receipt={'vendor':vendor,'candidate':str(app),'exeSha256':sha(app/'veyra_qml_ui.exe'),'fileCount':len(records)+1,
        'unchangedComponentCount':len(immutable),'sourceCommit':head,'mainCommit':json.loads((ARCH/'main-merge.json').read_text(encoding='utf8'))['mainAfter'] if (ARCH/'main-merge.json').exists() else None}
    subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/list-preset-flow-audit.py'),str(app),vendor,label+'-'+vendor],check=True)
    if mode=='finalize':
        archive=PACK/(app.name+'.zip');assert not archive.exists()
        print('ARCHIVE',vendor,flush=True)
        with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for rel in sorted(actual):z.write(app/rel,arcname=app.name+'/'+rel)
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            for row in records:assert hashlib.sha256(z.read(app.name+'/'+row['path'])).hexdigest()==row['sha256']
            assert hashlib.sha256(z.read(app.name+'/package-manifest.json')).hexdigest()==sha(app/'package-manifest.json')
        receipt.update(archive=str(archive),archiveBytes=archive.stat().st_size,archiveSha256=sha(archive),
            sourceZip=str(sourcezip),sourceZipSha256=sha(sourcezip),packageManifestSha256=sha(app/'package-manifest.json'),zipCrcAndAllPayloadHashesPassed=True)
    delivery.append(receipt);print(json.dumps(receipt,ensure_ascii=False),flush=True)
save(LOG/('packages-'+label+'.json'),delivery)
if mode=='finalize':save(PACK/'DELIVERY.json',delivery)
subprocess.run(guard,check=True)
