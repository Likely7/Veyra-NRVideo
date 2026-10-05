"""Stage/finalize the local NVIDIA package with source and independent receipts."""
from pathlib import Path
import hashlib,json,shutil,subprocess,sys,zipfile
ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='nr-strength-protection-20261005'
SOURCE=BASE/'test-packages/field-fixes-20261005/Veyra-2.0.3-field-20261005b-NVIDIA-win64-portable'
PARENT=BASE/'test-packages'/TASK
APP=PARENT/'Veyra-2.0.3-nr-controls-NVIDIA-win64-portable'
BUILD=BASE/'build'/TASK;LOG=BASE/'logs'/TASK;ARCHIVE=BASE/'archives'/TASK
mode=sys.argv[1] if len(sys.argv)>1 else 'stage';assert mode in ('stage','finalize')
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/nr-protection-control.py'),'guard'],check=True)
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def save(p,value):p.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args],text=True,encoding='utf8').strip()
expected={'runtime/experimental/nvngx_dlssnr.dll':'F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC',
 'runtime/experimental/nr-ampere/nvngx_dlssnr.dll':'6EB209E764F39872625DEBD6ABAF45E2BB6322F6F270F781F70C059AE30B3927',
 'runtime/experimental/nr-original/nvngx_dlssnr.dll':'E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E'}
for rel,digest in expected.items():assert sha(SOURCE/rel).upper()==digest,rel
if mode=='stage':
    assert not APP.exists();shutil.copytree(SOURCE,APP)
else:
    assert APP.is_dir() and not git('status','--porcelain'),'Commit the validated source before finalizing'
    assert sha(APP/'veyra_qml_ui.exe')==sha(BUILD/'veyra_qml_ui.exe')
    assert sha(APP/'qml/Veyra/Main.qml')==sha(ROOT/'qml/Veyra/Main.qml'),'Remove test injection before delivery'
    for suite in ('final-cpu','final-gpu'):
        checks=json.loads((LOG/suite/'results.json').read_text(encoding='utf8'))
        assert checks and all(c['exit']==0 for c in checks),suite
    assert json.loads((LOG/'qml-mouse4/result.json').read_text(encoding='utf8'))['exit']==0
    ass=json.loads((LOG/'smoke-inherited2/result.json').read_text(encoding='utf8'))
    assert ass['passed'] and ass['exeSha256']==sha(APP/'veyra_qml_ui.exe')
    for label,marker in [('final1','UI'),('finalrestore1','RESTORE'),('multilayer1','MULTI'),('realvideo1','VISUAL')]:
        console=(LOG/('production-'+label)/'console.log').read_text(encoding='utf8',errors='replace')
        assert 'NR_CONTROLS_'+marker+'_PASS' in console and 'NR_CONTROLS_UI_FAIL' not in console,label
    # Move only this task's named diagnostic files; never recursively delete a tree.
    workerLogs=LOG/'export-workers';workerLogs.mkdir(exist_ok=True)
    for p in (APP/'logs').glob('export-worker-*.log'):
        assert p.resolve().is_relative_to(APP.resolve())
        destination=workerLogs/p.name
        assert destination.resolve().is_relative_to(LOG.resolve()) and not destination.exists()
        shutil.move(str(p),str(destination))
shutil.copy2(BUILD/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
shutil.copytree(ROOT/'qml',APP/'qml',dirs_exist_ok=True)
shutil.copytree(BUILD/'shaders',APP/'shaders',dirs_exist_ok=True)
for name in ('THIRD_PARTY_NOTICES.md','README.md','README_CN.md','README_EN.md'):shutil.copy2(ROOT/name,APP/name)
for name in ('NR_STRENGTH_PROTECTION_PLAN_2026-10-05.md','NR_STRENGTH_PROTECTION_EXECUTION_2026-10-05.md'):
    shutil.copy2(ROOT/'docs'/name,APP/'docs'/name)
(APP/'NR_CONTROLS_LOCAL.md').write_text('本地 NR 画面调控版（未发布）\n\nNR → 增强变化量 → 总变化强度：0–5。NR → 画面调控 → 开启调控：默认关闭，开启后选自动／手动。关闭保留原始变化量；手动可调色相、色度、高光、局部压缩、时域稳定，切换自动仍保留手动值。开启调控接管时间域防闪，关闭恢复原设置。\n\n沿用已核验 Lecram / SFv2 / NVIDIA 原版 NR，未替换 Swapper DLL；保留 Claude 的 libass 字幕及其他主线修复。5 是残差外推，保护会降低危险区域的局部增益，不能保证所有视频无失真。RTX 20/30/40/AMD、真实 HDR 显示及长时主观质量还需验收；全局 native HDR 有八项起点已存在的测试失败，不能宣称 HDR 全过。详见 docs/NR_STRENGTH_PROTECTION_EXECUTION_2026-10-05.md。\n',encoding='utf8')
for rel,digest in expected.items():assert sha(APP/rel).upper()==digest,rel
receipt={'sourcePackage':str(SOURCE),'candidate':str(APP),'runtimeHashes':expected,'executableSha256':sha(APP/'veyra_qml_ui.exe'),
 'mainQmlSha256':sha(APP/'qml/Veyra/Main.qml'),'status':mode}
save(LOG/'package-stage.json',receipt)
if mode=='stage':print('CANDIDATE staged',APP);raise SystemExit(0)
head=git('rev-parse','HEAD');assert git('branch','--show-current')=='codex/'+TASK
# Audit every inherited component, including codecs, VFG/CUDA, models and manifests.
immutable={}
for p in SOURCE.rglob('*'):
    if not p.is_file():continue
    rel=p.relative_to(SOURCE).as_posix()
    if rel.startswith(('runtime/','runtime_local/','models/','licenses/')) or p.suffix.lower()=='.dll' or rel in ('release-runtime-manifest.json','vfg-runtime-manifest.json'):
        immutable[rel]=sha(p);assert sha(APP/rel)==immutable[rel],rel
for p in APP.rglob('*'):
    if p.is_file():
        rel=p.relative_to(APP).as_posix()
        assert not rel.startswith(('logs/','tmp/','outputs/','screenshots/','profile/')),rel
        assert p.suffix.lower() not in ('.lib','.pdb','.addon64','.pyc','.whl'),rel
sourceZip=PARENT/'Veyra-2.0.3-nr-controls-veyra-source.zip';assert not sourceZip.exists()
tracked=subprocess.check_output(['git','-C',str(ROOT),'ls-files','-z']).decode('utf8').split('\0')
with zipfile.ZipFile(sourceZip,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for rel in sorted(filter(None,tracked)):z.write(ROOT/rel,arcname='Veyra-2.0.3-nr-controls-source/'+rel)
with zipfile.ZipFile(sourceZip) as z:
    assert z.testzip() is None
    for rel in filter(None,tracked):assert hashlib.sha256(z.read('Veyra-2.0.3-nr-controls-source/'+rel)).hexdigest()==sha(ROOT/rel),rel
bundle=ARCHIVE/'source-final.bundle';assert not bundle.exists()
subprocess.run(['git','-C',str(ROOT),'bundle','create',str(bundle),'HEAD'],check=True)
with (LOG/'final-bundle-verify.log').open('w',encoding='utf8') as out:
    subprocess.run(['git','-C',str(ROOT),'bundle','verify',str(bundle)],stdout=out,stderr=subprocess.STDOUT,check=True)
manifest=json.loads((SOURCE/'package-manifest.json').read_text(encoding='utf8'))
manifest.update(candidate='2.0.3-nr-controls-20261005',baseCommit='de18fc4f71843049dc7fd691898598c17af308f0',
    sourceCommit=head,sourceArchiveCommit=head,worktreeDirty=False,releaseReady=False,localOnly=True,displayVersion='2.0.3-nr-controls',
    executableSha256=receipt['executableSha256'],validationRecord='docs/NR_STRENGTH_PROTECTION_EXECUTION_2026-10-05.md',
    softwareValidation='NR targeted checks passed; eight pre-existing native HDR test failures reproduced on starting main',
    hardwareValidation='RTX 5070 local short tests only; RTX 20/30/40, AMD, real HDR/capture and long subjective quality unverified')
manifest['correspondingSource'].update(application='../'+sourceZip.name,applicationSha256=sha(sourceZip),
    displayVersionOverride='-DVEYRA_DISPLAY_VERSION=2.0.3-nr-controls')
excluded={'package-manifest.json','local-package-manifest.json'}
records=[{'path':p.relative_to(APP).as_posix(),'size':p.stat().st_size,'sha256':sha(p)}
         for p in sorted(APP.rglob('*')) if p.is_file() and p.name not in excluded]
manifest['files']=records;save(APP/'package-manifest.json',manifest)
local={'purpose':'NR strength five with optional automatic/manual picture control; local only','sourceCommit':head,
       'baseCommit':manifest['baseCommit'],'displayVersion':manifest['displayVersion'],
       'exeSha256':receipt['executableSha256'],'worktreeDirty':False,'releaseReady':False,
       'unchangedInheritedComponents':immutable,'files':records,'packageManifestSha256':sha(APP/'package-manifest.json')}
save(APP/'local-package-manifest.json',local)
for row in records:assert sha(APP/row['path'])==row['sha256'],row['path']
archive=PARENT/(APP.name+'.zip');assert not archive.exists()
print('SOURCE verified; packaging',len(records),'files,',len(immutable),'unchanged components',flush=True)
with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in sorted(APP.rglob('*')):
        if p.is_file():z.write(p,arcname=p.relative_to(PARENT).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    for row in records:assert hashlib.sha256(z.read(APP.name+'/'+row['path'])).hexdigest()==row['sha256'],row['path']
    for name in excluded:assert hashlib.sha256(z.read(APP.name+'/'+name)).hexdigest()==sha(APP/name)
receipt.update(status='local delivery; targeted NR tests passed, hardware/quality limits documented',
    sourceCommit=head,sourceZip=str(sourceZip),sourceZipSha256=sha(sourceZip),sourceBundle=str(bundle),sourceBundleSha256=sha(bundle),
    archive=str(archive),archiveBytes=archive.stat().st_size,archiveSha256=sha(archive),
    fileCount=len(records)+len(excluded),unchangedComponentCount=len(immutable),
    packageManifestSha256=sha(APP/'package-manifest.json'),scopePassed=True,zipCrcAndAllPayloadHashesPassed=True)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/nr-protection-control.py'),'guard'],check=True)
save(PARENT/'DELIVERY.json',receipt);save(LOG/'delivery.json',receipt)
print(json.dumps(receipt,ensure_ascii=False,indent=2),flush=True)
