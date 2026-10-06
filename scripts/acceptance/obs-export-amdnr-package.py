"""Local candidates only; retain every approved runtime byte and corresponding source."""
from pathlib import Path
import hashlib,json,subprocess,sys,shutil,zipfile
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='obs-export-amdnr-20261006';VERSION='2.0.4-fix1'
PACK=BASE/'test-packages'/TASK;BUILD=BASE/'build'/TASK;LOG=BASE/'logs'/TASK;ARCH=BASE/'archives'/TASK
mode=sys.argv[1];assert mode in ('stage','finalize')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/obs-export-amdnr-control.py'),'--published'],check=True)
PACK.mkdir(parents=True,exist_ok=True)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True,encoding='utf8',stderr=subprocess.PIPE).strip()
head=git('rev-parse','HEAD');dirty=bool(git('status','--porcelain'));sourcezip=PACK/f'Veyra-{VERSION}-source.zip'
if mode=='finalize':
    assert not dirty,'Commit the reviewable local repair before final source snapshot'
    frozen=json.loads((LOG/'tested-inputs.json').read_text(encoding='utf8'))
    assert frozen['executableSha256']==sha(BUILD/'veyra_qml_ui.exe')
    for rel,h in frozen['productInputs'].items():assert sha(ROOT/rel)==h,rel
    for rel,h in frozen['testReceipts'].items():assert sha(LOG/rel)==h,rel
    files=git('ls-files').splitlines();assert not sourcezip.exists()
    with zipfile.ZipFile(sourcezip,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for rel in files:
            assert not rel.startswith(('runtime/','runtime_local/','third_party_local/','models/'))
            assert Path(rel).suffix.lower() not in ('.dll','.exe','.lib','.pdb','.addon64','.onnx','.bin','.hsaco','.f16','.f32','.ptx')
            z.write(ROOT/rel,arcname='Veyra-'+VERSION+'-source/'+rel)
    with zipfile.ZipFile(sourcezip) as z:
        assert z.testzip() is None
        for rel in files:assert hashlib.sha256(z.read('Veyra-'+VERSION+'-source/'+rel)).hexdigest()==sha(ROOT/rel)
    subprocess.run(['git','bundle','create',str(ARCH/'repair-final.bundle'),'f8045fb..HEAD'],cwd=ROOT,check=True)
    with (LOG/'repair-final-bundle-verify.log').open('xb') as log:subprocess.run(['git','bundle','verify',str(ARCH/'repair-final.bundle')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
delivery=[]
for vendor in ('AMD','NVIDIA'):
    source=BASE/'releases/publish-2.0.4-20261006/packages'/f'Veyra-2.0.4-{vendor}-win64-portable'
    app=PACK/f'Veyra-{VERSION}-{vendor}-win64-portable'
    original=json.loads((source/'package-manifest.json').read_text(encoding='utf8'));immutable={}
    if mode=='stage':assert not app.exists();app.mkdir()
    for row in original['files']:
        rel=row['path'];p=source/rel;assert p.resolve().is_relative_to(source.resolve()) and not p.is_symlink()
        assert p.stat().st_size==row['size'] and sha(p)==row['sha256'],rel
        if mode=='stage':
            target=app/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
        if rel.startswith(('runtime/','runtime_local/','models/','licenses/')) or p.suffix.lower()=='.dll' or rel in ('release-runtime-manifest.json','vfg-runtime-manifest.json'):immutable[rel]=row['sha256']
    if mode=='stage':
        shutil.copy2(BUILD/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
        for f in (BUILD/'shaders').rglob('*'):
            if f.is_file():shutil.copy2(f,app/'shaders'/f.relative_to(BUILD/'shaders'))
    shutil.copy2(ROOT/'docs/OBS_EXPORT_AMD_NR_PLAN_2026-10-06.md',app/'docs/OBS_EXPORT_AMD_NR_PLAN_2026-10-06.md')
    (app/'REPAIR_TEST_2.0.4-fix1.md').write_text(
        '# Veyra 2.0.4-fix1 本地修复候选\n\n'
        '这是独立测试包，尚未合并 main 或发布。请在新目录解压，先不要混装旧 DLL。\n\n'
        '修复：OBS/RTSS 软件 UI 禁用局部重绘；视频导出统一原生 NR 冻结链与参数、保留独立抗闪烁档位，并记录入口失败原因；AMD NR 修复合成阶段误判 NVIDIA 句柄导致的黑屏。\n\n'
        '本机 RTX5070：真实界面四种导出（含两层 NR、4K）各 60 帧完整；AMD 合成用 GPU 恒等提供器覆盖 1/2/4 层、时域开关与 reset，绝不是 HIP 神经推理验收。RX9070 实卡仍需复测。\n\n'
        'OBS 本机未重现旧版拖影，修复版滚动像素检查通过，现场问题仍需确认。软件 UI 全重绘可能增加 CPU 绘制开销。\n\n'
        'AMD 包保留现有 lmxxf 运行库和权重原字节，不宣称 RDNA2/3 已支持。两个参考项目的固定版本与证据见 docs/OBS_EXPORT_AMD_NR_PLAN_2026-10-06.md。\n',encoding='utf8')
    assert sha(app/'veyra_qml_ui.exe')==sha(BUILD/'veyra_qml_ui.exe')
    for rel,h in immutable.items():assert sha(app/rel)==h,rel
    actual={f.relative_to(app).as_posix():f for f in app.rglob('*') if f.is_file()}
    expected={r['path'] for r in original['files']}|{'package-manifest.json','REPAIR_TEST_2.0.4-fix1.md','docs/OBS_EXPORT_AMD_NR_PLAN_2026-10-06.md'}
    assert set(actual)-{'package-manifest.json'}==expected-{'package-manifest.json'}
    assert not any(f.is_symlink() or f.suffix.lower() in ('.pdb','.lib','.addon64','.log','.dmp','.pyc','.whl') or n.startswith(('logs/','tmp/','outputs/','profile/','screenshots/','user-data-')) for n,f in actual.items())
    manifest=dict(original);manifest.update(version=VERSION,displayVersion=VERSION,candidate=VERSION,baseCommit='f8045fb53a7d800b053f0631a9b44dc9f5f1aacb',sourceCommit=head,sourceArchiveCommit=head,
        worktreeDirty=dirty,localOnly=True,releaseReady=False,executableSha256=sha(app/'veyra_qml_ui.exe'),validationRecord='docs/OBS_EXPORT_AMD_NR_PLAN_2026-10-06.md',
        hardwareValidation='RTX5070 native export and identity provider only; affected OBS machine and RX9070XT HIP inference unverified',
        files=[dict(path=n,size=f.stat().st_size,sha256=sha(f)) for n,f in sorted(actual.items()) if n!='package-manifest.json'])
    if mode=='finalize':manifest['correspondingSource'].update(application='../'+sourcezip.name,applicationSha256=sha(sourcezip),displayVersionOverride='-DVEYRA_DISPLAY_VERSION=2.0.4-fix1')
    save(app/'package-manifest.json',manifest)
    subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/obs-export-amdnr-audit.py'),str(app),vendor,mode+'-'+vendor],check=True)
    receipt=dict(vendor=vendor,app=str(app),sourceCommit=head,exeSha256=sha(app/'veyra_qml_ui.exe'),fileCount=len(actual),unchangedComponentCount=len(immutable))
    if mode=='finalize':
        archive=PACK/(app.name+'.zip');assert not archive.exists()
        with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for n,f in sorted(actual.items()):z.write(f,arcname=app.name+'/'+n)
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            for n,f in actual.items():assert hashlib.sha256(z.read(app.name+'/'+n)).hexdigest()==sha(f)
        receipt.update(archive=str(archive),archiveBytes=archive.stat().st_size,archiveSha256=sha(archive),zipCrcAndAllPayloadHashesPassed=True,sourceZip=str(sourcezip),sourceZipSha256=sha(sourcezip))
    delivery.append(receipt);print(json.dumps(receipt,ensure_ascii=False),flush=True)
save(LOG/('packages-'+mode+'.json'),delivery)
if mode=='finalize':save(PACK/'DELIVERY.json',delivery)
