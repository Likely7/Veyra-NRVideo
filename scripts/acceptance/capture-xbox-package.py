"""Stage/finalize isolated vendor packages without changing accepted runtime bytes."""
from pathlib import Path
import hashlib, json, shutil, subprocess, sys, zipfile

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra'); TASK='capture-xbox-field-20261005'; VERSION='2.0.3-streamfix1'
PARENT=BASE/'test-packages'/TASK; LOG=BASE/'logs'/TASK; ARCH=BASE/'archives'/TASK
mode=sys.argv[1]; assert mode in ('stage','refresh','finalize')
vendors=sys.argv[2:] or ['AMD','NVIDIA']; assert vendors and set(vendors)<= {'AMD','NVIDIA'}
sources={
 'AMD':BASE/'releases/release-2.0.3-20261004/Veyra-2.0.3-AMD-win64-portable',
 'NVIDIA':BASE/'test-packages/nr-strength-protection-20261005/Veyra-2.0.3-nr-controls2-NVIDIA-win64-portable'}
BUILD=BASE/'build'/TASK
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/capture-xbox-control.py')]
subprocess.run(guard,check=True)
for p in (PARENT,LOG,ARCH):p.mkdir(parents=True,exist_ok=True)
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
    return h.hexdigest()
def save(p,obj):p.write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args],encoding='utf8',stderr=subprocess.PIPE).strip()
def component(rel):
    return rel.startswith(('runtime/','runtime_local/','models/','licenses/')) or Path(rel).suffix.lower()=='.dll' or rel in ('release-runtime-manifest.json','vfg-runtime-manifest.json')
def private(rel):
    return rel.startswith(('runtime_local/user-data-','logs/','tmp/','outputs/','screenshots/','profile/'))
head=git('rev-parse','HEAD'); dirty=bool(git('status','--porcelain'))
source_zip=PARENT/f'Veyra-{VERSION}-veyra-source.zip'
if mode=='finalize':
    assert not dirty,'Commit the validated source before packaging'
    for name in ('xbox1','scene1','timing1','codec10','codec11','smoke-AMD3','smoke-NVIDIA3','hot-NVIDIA3'):
        result=json.loads((LOG/(name+'.json')).read_text(encoding='utf8'))
        assert result['passed'],name
        if name.startswith(('smoke-','hot-')):assert result['exeSha256']==sha(BUILD/'veyra_qml_ui.exe'),name
        if name.startswith('codec'):assert result['exeSha256']==sha(BUILD/'veyra_capture_compressed_tests.exe'),name
    assert not source_zip.exists()
    files=[f for f in subprocess.check_output(['git','-C',str(ROOT),'ls-files','-z']).decode('utf8').split('\0') if f]
    with zipfile.ZipFile(source_zip,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for rel in sorted(files):
            assert not rel.startswith(('runtime/','runtime_local/','third_party_local/','models/'))
            assert Path(rel).suffix.lower() not in ('.dll','.lib','.pdb','.addon64','.onnx','.bin')
            z.write(ROOT/rel,arcname=f'Veyra-{VERSION}-source/'+rel)
    with zipfile.ZipFile(source_zip) as z:
        assert z.testzip() is None
        for rel in files:assert hashlib.sha256(z.read(f'Veyra-{VERSION}-source/'+rel)).hexdigest()==sha(ROOT/rel),rel
    bundle=ARCH/'source-final.bundle';assert not bundle.exists()
    subprocess.run(['git','-C',str(ROOT),'bundle','create',str(bundle),'HEAD'],check=True)
    with (LOG/'source-final-verify.log').open('w',encoding='utf8') as out:
        subprocess.run(['git','-C',str(ROOT),'bundle','verify',str(bundle)],stdout=out,stderr=subprocess.STDOUT,check=True)
delivery=[]
common_manifest=json.loads((sources['NVIDIA']/'package-manifest.json').read_text(encoding='utf8'))
common_licenses={row['path']:row['sha256'] for row in common_manifest['files'] if row['path'].startswith('licenses/libass/')}
assert len(common_licenses)==8
for vendor in vendors:
    source=sources[vendor]; app=PARENT/f'Veyra-{VERSION}-{vendor}-win64-portable'
    manifest=json.loads((source/'package-manifest.json').read_text(encoding='utf8'))
    # Verify the accepted source package before copying or loading its runtime.
    for row in manifest['files']:
        if private(row['path']):continue
        p=source/row['path'];assert p.resolve().is_relative_to(source.resolve())
        assert p.is_file() and sha(p)==row['sha256'],str(p)
    source_paths=[row['path'] for row in manifest['files'] if not private(row['path'])]
    immutable={rel:sha(source/rel) for rel in source_paths if component(rel)}
    if mode=='stage':
        assert not app.exists()
        app.mkdir(parents=True)
        # A previously delivered app may now have the user's saved data next
        # to it. Copy only manifest-listed payloads, never an entire live app.
        for rel in source_paths:
            destination=app/rel;assert destination.resolve().is_relative_to(app.resolve())
            destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source/rel,destination)
    # Isolate only copies inadvertently inherited by the first staging run.
    # Both resolved move paths must be within this task, never the source app.
    for copied in (app/'runtime_local').glob('user-data-*'):
        if not copied.is_dir():continue
        destination=LOG/'quarantined-config-copies'/vendor/copied.name
        assert copied.resolve().is_relative_to(app.resolve()) and app.resolve().is_relative_to(PARENT.resolve())
        assert destination.resolve().is_relative_to(LOG.resolve()) and not destination.exists()
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.move(str(copied),str(destination))
        print('EXCLUDED copied configuration directory',vendor,copied.name,flush=True)
    if mode in ('stage','refresh'):
        assert app.is_dir()
        shutil.copy2(BUILD/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
        shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
        shutil.copytree(BUILD/'shaders',app/'shaders',dirs_exist_ok=True)
        for name in ('README.md','README_CN.md','README_EN.md','THIRD_PARTY_NOTICES.md'):
            shutil.copy2(ROOT/name,app/name)
        for name in ('CAPTURE_XBOX_FIELD_PLAN_2026-10-05.md',):shutil.copy2(ROOT/'docs'/name,app/'docs'/name)
        (app/'STREAM_FIX_LOCAL.md').write_text(
            'Xbox 解码恢复本地候选 2.0.3-streamfix1（未发布）\n\n'
            '针对日志 28 的 H.264 解码持续 Invalid argument：错误或输入队列丢 AU 后清理参考历史并等待关键帧；'
            '连续失败时每会话最多一次硬解重开、一次软件回退；恢复首帧带时间线断点，日志标签 xbox-decode-recovery。'
            '软件回退可能增加 CPU 使用和解码耗时，视频仍无法恢复时明确报错。运行库与补帧算法未更换。\n\n'
            '采集 60→55–57 的现场输入仍约 60Hz，已有增强组合处理欠速；本次只修正日志节奏标签，未承诺恢复该组合 60FPS。'
            '保留此前 NR 强度 5、三种风格与自动／手动画面调控。\n\n'
            '本机 RTX5070 的本地协议、软／硬解和正常负载短测不等于 RX9070XT + Xbox 实机通过。'
            '请在受影响电脑上连接 Xbox，切换原先出问题的 FSR／XeSS 补帧，出现问题时保留这次的新日志。\n',encoding='utf8')
    else:
        assert sha(app/'veyra_qml_ui.exe')==sha(BUILD/'veyra_qml_ui.exe')
        for p in (ROOT/'qml').rglob('*'):
            if p.is_file():assert sha(app/'qml'/p.relative_to(ROOT/'qml'))==sha(p),str(p)
        shutil.copy2(ROOT/'docs/CAPTURE_XBOX_FIELD_EXECUTION_2026-10-05.md',app/'docs/CAPTURE_XBOX_FIELD_EXECUTION_2026-10-05.md')
    # Both vendor apps now use the same statically linked libass build; the
    # older AMD release template predates those common dependency notices.
    for rel,digest in common_licenses.items():
        p=sources['NVIDIA']/rel;assert sha(p)==digest
        destination=app/rel;destination.parent.mkdir(parents=True,exist_ok=True)
        if not destination.exists():shutil.copy2(p,destination)
        assert sha(destination)==digest
    for rel,digest in immutable.items():assert sha(app/rel)==digest,rel
    all_files=sorted(p for p in app.rglob('*') if p.is_file())
    expected_paths=set(source_paths)|{'package-manifest.json','local-package-manifest.json','STREAM_FIX_LOCAL.md',
        'docs/CAPTURE_XBOX_FIELD_PLAN_2026-10-05.md','docs/CAPTURE_XBOX_FIELD_EXECUTION_2026-10-05.md'}|set(common_licenses)
    expected_paths.update('qml/'+p.relative_to(ROOT/'qml').as_posix() for p in (ROOT/'qml').rglob('*') if p.is_file())
    expected_paths.update('shaders/'+p.relative_to(BUILD/'shaders').as_posix() for p in (BUILD/'shaders').rglob('*') if p.is_file())
    extras={p.relative_to(app).as_posix() for p in all_files}-expected_paths
    assert not extras, f'Unexpected candidate payloads: {sorted(extras)}'
    for p in all_files:
        rel=p.relative_to(app).as_posix()
        assert not private(rel) and not p.is_symlink(),rel
        assert p.suffix.lower() not in ('.lib','.pdb','.addon64','.pyc','.whl','.dmp','.log'),rel
    excluded={'package-manifest.json','local-package-manifest.json'}
    records=[{'path':p.relative_to(app).as_posix(),'size':p.stat().st_size,'sha256':sha(p)} for p in all_files if p.name not in excluded]
    manifest.update(candidate=VERSION,displayVersion=VERSION,sourceCommit=head,sourceArchiveCommit=head,
        baseCommit='33d6685a3a75d3d4e8ac5d32996b163ef1f488dc',worktreeDirty=dirty,
        localOnly=True,releaseReady=False,executableSha256=sha(app/'veyra_qml_ui.exe'),
        validationRecord='docs/CAPTURE_XBOX_FIELD_EXECUTION_2026-10-05.md',
        hardwareValidation='RTX5070 local short tests only; RX9070XT + Xbox and physical capture reproduction unverified')
    manifest['files']=records
    manifest['commonLibassLicenseHashes']=common_licenses
    manifest['correspondingSource']['libass']=common_manifest['correspondingSource']['libass']
    if mode=='finalize':
        manifest['correspondingSource'].update(application='../'+source_zip.name,applicationSha256=sha(source_zip),
            displayVersionOverride='-DVEYRA_DISPLAY_VERSION='+VERSION)
    save(app/'package-manifest.json',manifest)
    save(app/'local-package-manifest.json',{'purpose':'Xbox hard decode recovery; local field candidate',
        'sourceCommit':head,'worktreeDirty':dirty,'releaseReady':False,'displayVersion':VERSION,
        'sourcePackage':str(source),'unchangedInheritedComponents':immutable,'files':records,
        'exeSha256':sha(app/'veyra_qml_ui.exe'),'packageManifestSha256':sha(app/'package-manifest.json')})
    receipt={'vendor':vendor,'candidate':str(app),'exeSha256':sha(app/'veyra_qml_ui.exe'),
        'fileCount':len(records)+2,'unchangedComponentCount':len(immutable),'mode':mode}
    if mode=='finalize':
        archive=PARENT/(app.name+'.zip');assert not archive.exists()
        print('ARCHIVE',vendor,len(records)+2,'files',flush=True)
        with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for p in sorted(app.rglob('*')):
                if p.is_file():z.write(p,arcname=p.relative_to(PARENT).as_posix())
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            for row in records:assert hashlib.sha256(z.read(app.name+'/'+row['path'])).hexdigest()==row['sha256'],row['path']
            for name in excluded:assert hashlib.sha256(z.read(app.name+'/'+name)).hexdigest()==sha(app/name)
        receipt.update(archive=str(archive),archiveBytes=archive.stat().st_size,archiveSha256=sha(archive),
            sourceCommit=head,sourceZip=str(source_zip),sourceZipSha256=sha(source_zip),
            packageManifestSha256=sha(app/'package-manifest.json'),zipCrcAndAllPayloadHashesPassed=True)
    delivery.append(receipt); print(json.dumps(receipt,ensure_ascii=False),flush=True)
save(LOG/('packages-'+mode+'.json'),delivery)
if mode=='finalize':save(PARENT/'DELIVERY.json',delivery)
subprocess.run(guard,check=True)
