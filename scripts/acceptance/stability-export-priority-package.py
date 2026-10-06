"""Make isolated local candidates, retaining every approved runtime byte."""
from pathlib import Path
import hashlib,json,subprocess,sys,shutil,zipfile
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='stability-export-priority-20261006';VERSION='2.0.4-fix2'
PACK=BASE/'test-packages'/TASK;BUILD=BASE/'build'/TASK/'standard';LOG=BASE/'logs'/TASK;ARCH=BASE/'archives'/TASK
mode=sys.argv[1];assert mode in ('stage','refresh','finalize')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/stability-export-priority-control.py'),'--published'],check=True)
PACK.mkdir(parents=True,exist_ok=True)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True,encoding='utf8',stderr=subprocess.PIPE).strip()
head=git('rev-parse','HEAD');dirty=bool(git('status','--porcelain'));sourcezip=PACK/f'Veyra-{VERSION}-source.zip'
if mode=='finalize':
    assert not dirty,'Commit the reviewed repair before the final source archive'
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
    with zipfile.ZipFile(sourcezip) as z:assert z.testzip() is None
    base=json.loads((ARCH/'start.json').read_text(encoding='utf8'))['sourceBase']
    subprocess.run(['git','bundle','create',str(ARCH/'repair-final.bundle'),base+'..HEAD'],cwd=ROOT,check=True)
delivery=[]
for vendor in ('AMD','NVIDIA'):
    source=BASE/'test-packages/obs-export-amdnr-20261006'/f'Veyra-2.0.4-fix1-{vendor}-win64-portable'
    app=PACK/f'Veyra-{VERSION}-{vendor}-win64-portable'
    original=json.loads((source/'package-manifest.json').read_text(encoding='utf8'));immutable={}
    if mode=='stage':assert not app.exists();app.mkdir()
    for row in original['files']:
        rel=row['path'];p=source/rel;assert p.resolve().is_relative_to(source.resolve()) and not p.is_symlink()
        assert p.stat().st_size==row['size'] and sha(p)==row['sha256'],rel
        if mode=='stage':
            target=app/rel;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target)
        if rel.startswith(('runtime/','runtime_local/','models/','licenses/')) or p.suffix.lower()=='.dll' or rel in ('release-runtime-manifest.json','vfg-runtime-manifest.json'):immutable[rel]=row['sha256']
    if mode!='finalize':
        shutil.copy2(BUILD/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
        for folder in ('qml','shaders'):
            incoming=ROOT/folder if folder=='qml' else BUILD/folder
            for f in incoming.rglob('*'):
                if f.is_file():
                    target=app/folder/f.relative_to(incoming);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,target)
    for name in ('STABILITY_EXPORT_PRIORITY_PLAN_2026-10-06.md','STABILITY_EXPORT_PRIORITY_REPORT_2026-10-06.md'):
        if (ROOT/'docs'/name).exists():shutil.copy2(ROOT/'docs'/name,app/'docs'/name)
    (app/'REPAIR_TEST_2.0.4-fix2.md').write_text(
        '# Veyra 2.0.4-fix2 本地修复候选\n\n'
        '请解压到新目录测试。本包沿用已批准的运行库原字节，包含已经合入 main 的 PR19/20 和此前 fix1。尚未公开发布。\n\n'
        '修复 AMD 通用补帧入口；FSR 视频导出保留 FSR 后端；AMD 视频导出内部最高 1080p NR、原输出尺寸合成编码；XeSS 导出明确提示改选 FSR/DLSS/VFG。\n\n'
        '自动显示同步在窗口和全屏等待垂直同步防撕裂，提交 FPS 受显示器刷新率限制；仍可手动选择允许撕裂。GPU 优先级避免重复写入已生效档位。\n\n'
        '实测、性能比较的控制条件和未验证项见 docs/STABILITY_EXPORT_PRIORITY_REPORT_2026-10-06.md。本机为 RTX5070，AMD 4K 合成检查使用恒等提供器，不冒充 AMD HIP/硬件编码验收。\n',encoding='utf8')
    assert sha(app/'veyra_qml_ui.exe')==sha(BUILD/'veyra_qml_ui.exe')
    for rel,h in immutable.items():assert sha(app/rel)==h,rel
    actual={f.relative_to(app).as_posix():f for f in app.rglob('*') if f.is_file()}
    assert not any(f.is_symlink() or f.suffix.lower() in ('.pdb','.lib','.addon64','.log','.dmp','.pyc','.whl') or n.startswith(('logs/','tmp/','outputs/','profile/','screenshots/','user-data-')) for n,f in actual.items())
    manifest=dict(original);manifest.update(version=VERSION,displayVersion=VERSION,candidate=VERSION,baseCommit='578d63c3a0143429b306e26eeb89137473b4d99f',sourceCommit=head,sourceArchiveCommit=head,productCodeCommit=head,
        worktreeDirty=dirty,localOnly=True,releaseReady=False,executableSha256=sha(app/'veyra_qml_ui.exe'),validationRecord='docs/STABILITY_EXPORT_PRIORITY_REPORT_2026-10-06.md',
        validation='RTX5070 targeted export, presentation and settings checks; details and limitations in the validation record',
        softwareValidation='See current validation record, not inherited fix1 test results',hardwareValidation='RTX5070; AMD 4K compositor with identity provider; real AMD inference/encoder and user panel tearing unverified',
        files=[dict(path=n,size=f.stat().st_size,sha256=sha(f)) for n,f in sorted(actual.items()) if n!='package-manifest.json'])
    manifest['baseReleaseTag']=manifest.pop('releaseTag',None)
    manifest['baseReleaseUrl']=manifest.pop('releaseUrl',None)
    if mode=='finalize':
        manifest['correspondingSource'].update(application='../'+sourcezip.name,applicationSha256=sha(sourcezip),displayVersionOverride='-DVEYRA_DISPLAY_VERSION=2.0.4-fix2')
        manifest['sourceZipSha256']=sha(sourcezip)
    save(app/'package-manifest.json',manifest)
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
