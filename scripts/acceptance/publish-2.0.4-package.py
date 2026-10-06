"""Make formal archives from one tested EXE and the prior audited payload lists."""
from pathlib import Path
import hashlib,json,subprocess,shutil,zipfile,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='publish-2.0.4-20261006';OUT=BASE/'releases'/TASK
PACK=OUT/'packages';LOG=BASE/'logs'/TASK;ARCH=BASE/'archives'/TASK;OLD=BASE/'test-packages/list-preset-flow-20261006'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*a):return subprocess.check_output(['git','-C',str(ROOT),*a],stderr=subprocess.PIPE).decode('utf8').strip()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/publish-2.0.4-control.py')],check=True)
assert not git('status','--porcelain'),'Commit exact release source before packaging'
head=git('rev-parse','HEAD');files=list(filter(None,git('ls-files','-z').split('\0')))
sourcezip=OUT/'Veyra-2.0.4-source.zip';assert not sourcezip.exists()
with zipfile.ZipFile(sourcezip,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for rel in sorted(files):
        assert not rel.startswith(('runtime/','runtime_local/','models/','third_party_local/'))
        assert Path(rel).suffix.lower() not in ('.dll','.exe','.lib','.pdb','.addon64','.onnx','.bin','.hsaco','.ptx','.f16','.f32')
        z.write(ROOT/rel,'Veyra-2.0.4-source/'+rel)
with zipfile.ZipFile(sourcezip) as z:
    assert z.testzip() is None
    for rel in files:
        with z.open('Veyra-2.0.4-source/'+rel) as f:assert hashlib.file_digest(f,'sha256').hexdigest()==sha(ROOT/rel),rel
dependency=OUT/'Veyra-2.0.4-dependency-source.zip';assert dependency.is_file()
subprocess.run(['git','-C',str(ROOT),'bundle','create',str(ARCH/'source-final.bundle'),'HEAD'],check=True)
subprocess.run(['git','-C',str(ROOT),'bundle','verify',str(ARCH/'source-final.bundle')],check=True)
delivery=[]
docnames=['RELEASE_NOTES_2.0.4.md','BUILD_2.0.4.md','PERF_RELEASE_REPORT_2.0.4_2026-10-06.md','RELEASE_SUPPORT.md',
          'PUBLISH_2.0.4_PLAN_2026-10-06.md','RELEASE_2.0.4_ACCEPTANCE_2026-10-05.md','LIST_PRESET_FLOW_ACCEPTANCE_2026-10-06.md',
          'PERF_R0_ACCEPTANCE_2026-10-05.md','PERF_2A_CORE_REUSE_2026-10-04.md','PERF_2C_RECENT_CACHE_2026-10-04.md',
          'PERF_5C_PAUSED_NR_2026-10-04.md','PERF_EXECUTION_NR_2026-10-04.md']
for vendor in ('AMD','NVIDIA'):
    source=OLD/f'Veyra-2.0.4-{vendor}-win64-portable';app=PACK/source.name;assert not app.exists();app.mkdir(parents=True)
    old=json.loads((source/'package-manifest.json').read_text(encoding='utf8'));immutable={}
    for row in old['files']:
        rel=row['path'];p=source/rel
        assert p.is_file() and p.stat().st_size==row['size'] and sha(p)==row['sha256'],rel
        assert p.resolve().is_relative_to(source.resolve()) and not p.is_symlink()
        if rel in ('TEST_2.0.4.md','NR_CONTROLS_LOCAL.md'):continue
        q=app/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
        if rel.startswith(('runtime/','runtime_local/','models/','licenses/')) or p.suffix.lower() in ('.dll','.exe','.cso') or rel in ('release-runtime-manifest.json','vfg-runtime-manifest.json'):
            immutable[rel]=row['sha256']
    for name in ('README.md','README_CN.md','README_EN.md','THIRD_PARTY_NOTICES.md','LICENSE'):shutil.copyfile(ROOT/name,app/name)
    shutil.copyfile(ROOT/'assets/star-history.svg',app/'assets/star-history.svg')
    for name in docnames:shutil.copyfile(ROOT/'docs'/name,app/'docs'/name)
    (app/'RELEASE_2.0.4.md').write_text('Veyra2.0.4 正式应用版本 / Stable application release\n\n选择与显卡相符的包，在新目录完整解压，运行veyra_qml_ui.exe。此包包含已审计实验增强组件，其硬件/画质边界见docs/RELEASE_NOTES_2.0.4.md。\n\nRelease: https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.4\nDiscord: https://discord.gg/c9aREyMj8\nKo-fi: https://ko-fi.com/likely7\n对应源码及静态字幕依赖替换/重编译见docs/BUILD_2.0.4.md。\n',encoding='utf8')
    for rel,digest in immutable.items():assert sha(app/rel)==digest,rel
    assert sha(app/'veyra_qml_ui.exe')=='d01329aa6d9fbd6ac2d1eee294782f4cb9d233490595ed2e3e1a9ea12c16b2d6'
    for p in (ROOT/'qml').rglob('*'):
        if p.is_file():assert sha(app/'qml'/p.relative_to(ROOT/'qml'))==sha(p)
    actual={p.relative_to(app).as_posix() for p in app.rglob('*') if p.is_file()}
    assert not any(Path(n).suffix.lower() in ('.pdb','.lib','.addon64','.log','.dmp','.pyc','.whl') or n.startswith(('logs/','tmp/','profile/','outputs/')) for n in actual)
    records=[dict(path=rel,size=(app/rel).stat().st_size,sha256=sha(app/rel)) for rel in sorted(actual)]
    manifest=dict(old);manifest.update(version='2.0.4',sourceCommit=head,baseCommit=head,sourceArchiveCommit=head,worktreeDirty=False,
        localOnly=False,releaseReady=True,releaseTag='v2.0.4',releaseUrl='https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.4',
        validationRecord='docs/LIST_PRESET_FLOW_ACCEPTANCE_2026-10-06.md',files=records)
    manifest['correspondingSource'].update(application='https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.4/'+sourcezip.name,
        dependencies='https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.4/'+dependency.name,
        applicationSha256=sha(sourcezip),dependencySha256=sha(dependency),instructions='docs/BUILD_2.0.4.md')
    save(app/'package-manifest.json',manifest)
    subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/publish-2.0.4-audit.py'),str(app),vendor,'stage-'+vendor],check=True)
    archive=OUT/(app.name+'.zip');assert not archive.exists();print('ARCHIVE',vendor,flush=True)
    with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for rel in sorted(actual|{'package-manifest.json'}):z.write(app/rel,app.name+'/'+rel)
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for row in records:
            with z.open(app.name+'/'+row['path']) as f:assert hashlib.file_digest(f,'sha256').hexdigest()==row['sha256'],row['path']
    receipt=dict(vendor=vendor,candidate=str(app),archive=str(archive),archiveBytes=archive.stat().st_size,archiveSha256=sha(archive),
        exeSha256=sha(app/'veyra_qml_ui.exe'),sourceCommit=head,sourceZip=str(sourcezip),sourceZipSha256=sha(sourcezip),
        unchangedComponentCount=len(immutable),fileCount=len(records)+1,zipCrcAndAllPayloadHashesPassed=True)
    delivery.append(receipt);print(json.dumps(receipt,ensure_ascii=False),flush=True)
save(PACK/'DELIVERY.json',delivery);save(OUT/'DELIVERY.json',delivery)
assets=[Path(r['archive']) for r in delivery]+[sourcezip,dependency]
(OUT/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in sorted(assets)),encoding='ascii')
save(LOG/'assets.json',[dict(path=str(p),name=p.name,bytes=p.stat().st_size,sha256=sha(p)) for p in assets+[OUT/'SHA256SUMS.txt']])
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/publish-2.0.4-control.py')],check=True)
