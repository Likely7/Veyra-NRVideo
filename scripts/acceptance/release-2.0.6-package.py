"""Build audited formal packages from the clean tested 2.0.6 source."""
from pathlib import Path
import hashlib,json,subprocess,shutil,sys,zipfile,pefile
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.6-20261007'
OUT=BASE/'releases'/TASK;PACK=OUT/'packages';LOG=BASE/'logs'/TASK;ARCH=BASE/'archives'/TASK
OLD=BASE/'releases/publish-2.0.5-20261007/packages';BUILD=BASE/'build'/TASK
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
def git(*a):return subprocess.check_output(['git','-C',str(ROOT),*a],stderr=subprocess.PIPE).decode('utf8').strip()
def guard():subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.6-control.py')],check=True)
guard();assert not git('status','--porcelain'),'Commit exact release source first'
OUT.mkdir(parents=True,exist_ok=True);PACK.mkdir(exist_ok=False)
head=git('rev-parse','HEAD');merge=json.loads((LOG/'main-merge.json').read_text(encoding='utf8'));assert merge['sourceCommit']==head
build=json.loads((LOG/'build-product.json').read_text(encoding='utf8'));assert build['exit']==0
for rel,h in build['sourceFiles'].items():assert sha(ROOT/rel)==h,rel
exe=BUILD/'veyra_qml_ui.exe';EXE=sha(exe)
pe=pefile.PE(str(exe));v=pe.VS_FIXEDFILEINFO[0]
assert (v.FileVersionMS,v.FileVersionLS,v.ProductVersionMS,v.ProductVersionLS)==(2<<16,6<<16,2<<16,6<<16)
pe.close()
tracked=sorted(n for n in git('ls-files','-z').split('\0') if n)
excluded=[n for n in tracked if '__pycache__' in Path(n).parts or Path(n).suffix.lower()=='.pyc']
assert excluded==['scripts/acceptance/__pycache__/fg-utilization-matrix.cpython-311.pyc'];files=[n for n in tracked if n not in excluded]
for rel in files:
 assert not rel.startswith(('runtime/','runtime_local/','models/','third_party_local/'))
 assert Path(rel).suffix.lower() not in ('.dll','.exe','.lib','.pdb','.addon64','.onnx','.bin','.hsaco','.ptx','.f16','.f32','.pyc','.zip','.7z')
sourcezip=OUT/'Veyra-2.0.6-source.zip';partial=sourcezip.with_suffix('.zip.partial');assert not sourcezip.exists() and not partial.exists()
with zipfile.ZipFile(partial,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for rel in files:z.write(ROOT/rel,'Veyra-2.0.6-source/'+rel)
with zipfile.ZipFile(partial) as z:
 assert z.testzip() is None and len(z.infolist())==len(files)
 for rel in files:
  with z.open('Veyra-2.0.6-source/'+rel) as f:assert hashlib.file_digest(f,'sha256').hexdigest()==sha(ROOT/rel),rel
partial.rename(sourcezip)
dependency=OUT/'Veyra-2.0.6-dependency-source.zip';assert not dependency.exists()
olddep=BASE/'releases/publish-2.0.5-20261007/Veyra-2.0.5-dependency-source.zip'
assert sha(olddep)=='4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9'
shutil.copyfile(olddep,dependency);assert sha(dependency)==sha(olddep)
with zipfile.ZipFile(dependency) as z:assert z.testzip() is None
subprocess.run(['git','-C',str(ROOT),'bundle','create',str(ARCH/'source-final.bundle'),'HEAD'],check=True)
subprocess.run(['git','-C',str(ROOT),'bundle','verify',str(ARCH/'source-final.bundle')],check=True)
save(LOG/'source-audit.json',dict(sourceCommit=head,mainCommit=merge['mainAfter'],sourceFiles=len(files),buildInputFiles=len(build['sourceFiles']),executableSha256=EXE,peVersion='2.0.6.0',excludedTrackedCaches=excluded,sourceSha256=sha(sourcezip),dependencySha256=sha(dependency),dependencyByteIdenticalTo='v2.0.5',passed=True))
docnames=('RELEASE_NOTES_2.0.6.md','BUILD_2.0.6.md','RELEASE_2.0.6_PLAN_2026-10-07.md','RELEASE_2.0.6_ACCEPTANCE_2026-10-07.md','VFG_OPTIMIZATION_REPORT_2026-10-07.md','BLACKMAGIC_CAPTURE_REPORT_2026-10-07.md','AMD_NR_FSR_REPORT_2026-10-07.md','RELEASE_SUPPORT.md')
delivery=[]
for vendor in ('AMD','NVIDIA'):
 source=OLD/f'Veyra-2.0.5-{vendor}-win64-portable';app=PACK/f'Veyra-2.0.6-{vendor}-win64-portable';app.mkdir()
 old=json.loads((source/'package-manifest.json').read_text(encoding='utf8'));immutable={}
 for row in old['files']:
  rel=row['path'];p=source/rel
  assert p.is_file() and p.stat().st_size==row['size'] and sha(p)==row['sha256'],rel
  assert p.resolve().is_relative_to(source.resolve()) and not p.is_symlink()
  if rel in ('RELEASE_2.0.5.md','TEST_NOTES_2.0.5.md','docs/TEST_NOTES_2.0.5.md'):continue
  q=app/rel;q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
  if rel.startswith(('runtime/','runtime_local/','models/','licenses/')) or p.suffix.lower() in ('.dll','.cso') or rel in ('release-runtime-manifest.json','vfg-runtime-manifest.json'):immutable[rel]=row['sha256']
 shutil.copyfile(exe,app/exe.name)
 for name in ('README.md','README_CN.md','README_EN.md','THIRD_PARTY_NOTICES.md','LICENSE'):shutil.copyfile(ROOT/name,app/name)
 for name in docnames:shutil.copyfile(ROOT/'docs'/name,app/'docs'/name)
 qr=app/'docs/images/2.0.6/community-group.png';qr.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'docs/images/2.0.6/community-group.png',qr)
 for p in (ROOT/'qml').rglob('*'):
  if p.is_file():
   q=app/'qml'/p.relative_to(ROOT/'qml');q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
 (app/'RELEASE_2.0.6.md').write_text('Veyra 2.0.6 正式应用版本 / Stable application release\n\n新目录完整解压后运行veyra_qml_ui.exe。VFG调度、AMD NR→FSR、Blackmagic兼容更新，详情/数据/未验证项见docs/RELEASE_NOTES_2.0.6.md。NVIDIA显存持续增长未修复。\n\nRelease: https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.6\nDiscord: https://discord.gg/c9aREyMj8\nKo-fi: https://ko-fi.com/likely7\n对应源码与静态依赖重链接见docs/BUILD_2.0.6.md。\n',encoding='utf8')
 for rel,h in immutable.items():assert sha(app/rel)==h,rel
 assert sha(app/'veyra_qml_ui.exe')==EXE
 for folder,sourcefolder in (('qml',ROOT/'qml'),('shaders',BUILD/'shaders')):
  for p in sourcefolder.rglob('*'):
   if p.is_file():assert sha(app/folder/p.relative_to(sourcefolder))==sha(p),str(p)
 actual={p.relative_to(app).as_posix() for p in app.rglob('*') if p.is_file()};assert 'package-manifest.json' not in actual
 assert not any(Path(n).suffix.lower() in ('.pdb','.lib','.addon64','.log','.dmp','.pyc','.whl') or n.startswith(('logs/','tmp/','profile/','outputs/')) for n in actual)
 assert not any('test-runtime' in n.lower() or 'test-provider' in n.lower() or Path(n).name.startswith('veyra_') and n.endswith('_tests.exe') for n in actual)
 records=[dict(path=rel,size=(app/rel).stat().st_size,sha256=sha(app/rel)) for rel in sorted(actual)]
 manifest=dict(old);manifest.update(version='2.0.6',displayVersion='2.0.6',candidate='2.0.6',sourceCommit=head,baseCommit=head,sourceArchiveCommit=head,mainCommit=merge['mainAfter'],worktreeDirty=False,localOnly=False,releaseReady=True,releaseTag='v2.0.6',releaseUrl='https://github.com/Likely7/Veyra-NRVideo/releases/tag/v2.0.6',productCodeCommit='bf6e6517a62abc94ea8de043b8db938be0a8638d',executableSha256=EXE,validation='Fresh 2.0.6 targeted regression; formal payload/PE/ZIP audit and final-ZIP private cold start',files=records)
 manifest['correspondingSource'].update(application='https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/'+sourcezip.name,dependencies='https://github.com/Likely7/Veyra-NRVideo/releases/download/v2.0.6/'+dependency.name,applicationSha256=sha(sourcezip),dependencySha256=sha(dependency),instructions='docs/BUILD_2.0.6.md')
 manifest['sourceZipSha256']=sha(sourcezip);save(app/'package-manifest.json',manifest)
 subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.6-audit.py'),str(app),vendor,'stage-'+vendor],check=True)
 archive=OUT/(app.name+'.zip');assert not archive.exists();print('ARCHIVE',vendor,flush=True)
 with zipfile.ZipFile(archive,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for rel in sorted(actual|{'package-manifest.json'}):z.write(app/rel,app.name+'/'+rel)
 with zipfile.ZipFile(archive) as z:
  assert z.testzip() is None and len(z.infolist())==len(records)+1
  for row in records:
   with z.open(app.name+'/'+row['path']) as f:assert hashlib.file_digest(f,'sha256').hexdigest()==row['sha256'],row['path']
  assert json.loads(z.read(app.name+'/package-manifest.json'))==manifest
 receipt=dict(vendor=vendor,candidate=str(app),archive=str(archive),archiveBytes=archive.stat().st_size,archiveSha256=sha(archive),exeSha256=EXE,sourceCommit=head,mainCommit=merge['mainAfter'],sourceZip=str(sourcezip),sourceZipSha256=sha(sourcezip),unchangedComponentCount=len(immutable),fileCount=len(records)+1,zipCrcAndAllPayloadHashesPassed=True)
 delivery.append(receipt);print(json.dumps(receipt,ensure_ascii=False),flush=True)
save(PACK/'DELIVERY.json',delivery);save(OUT/'DELIVERY.json',delivery)
assets=[Path(r['archive']) for r in delivery]+[sourcezip,dependency]
(OUT/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in sorted(assets)),encoding='ascii')
save(LOG/'assets.json',[dict(path=str(p),name=p.name,bytes=p.stat().st_size,sha256=sha(p)) for p in assets+[OUT/'SHA256SUMS.txt']]);guard()
