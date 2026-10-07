"""Audit source/test correspondence and assemble a local portable test handoff."""
from pathlib import Path
import ast,hashlib,json,os,re,shutil,subprocess,sys,time,zipfile,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='blackmagic-capture-20261007'
LOGS=BASE/'logs'/TASK;OUT=BASE/'test-packages'/TASK;VERIFY=BASE/'verify'/TASK
APP=OUT/'Veyra-2.0.5-capture-test-NVIDIA-win64-portable'
PACKAGE=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-NVIDIA-win64-portable'
BUILD=BASE/'build'/TASK
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT).decode('utf8').strip()
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/blackmagic-control.py')]
subprocess.run(guard,cwd=ROOT,check=True)
assert not any(p.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_capture_tests.exe','veyra_hdr_color_tests.exe') for p in psutil.process_iter(['name']))
assert not git('status','--porcelain=v1'),'Commit reviewed source before packaging'
subprocess.run(['git','diff','--check'],cwd=ROOT,check=True)
head=git('rev-parse','HEAD');build=read(LOGS/'capture-build-v4.json');assert build['exit']==0
for n,d in build['sourceFiles'].items():assert sha(ROOT/n)==d,'Build input changed: '+n
for p in (ROOT/'scripts/acceptance').glob('blackmagic-*.py'):ast.parse(p.read_text(encoding='utf8'),filename=str(p))
contracts=read(LOGS/'contracts-v4/summary.json');assert len(contracts)==4 and all(x['passed'] and x['exit']==0 for x in contracts)
for x in contracts:assert sha(Path(x['command'][0]))==x['exeSha256'],x['case']
rate=read(LOGS/'usb-rate-v4/summary.json');gui=read(LOGS/'gui-v4/summary.json');props=read(LOGS/'driver-page-v4/summary.json')
assert len(rate)==1 and len(gui)==2 and len(props)==1 and all(x['passed'] and x['exit']==0 for x in rate+gui+props)
exe=sha(BUILD/'veyra_qml_ui.exe');assert sha(APP/'veyra_qml_ui.exe')==exe
assert all(x['exeSha256']==exe for x in gui+props)
assert rate[0]['exeSha256']==sha(BUILD/'veyra_capture_tests.exe')
note=ROOT/'docs/BLACKMAGIC_CAPTURE_REPORT_2026-10-07.md';assert exe in note.read_text(encoding='utf8')
shutil.copy2(note,APP/'CAPTURE-TEST-NOTES.zh-CN.md')
old=read(PACKAGE/'package-manifest.json')
changed={'veyra_qml_ui.exe','qml/Veyra/DialogHost.qml','THIRD_PARTY_NOTICES.md'}
oldPaths={x['path'] for x in old['files']}
for row in old['files']:
 rel=row['path'];assert sha(PACKAGE/rel)==row['sha256'].lower(),rel
 assert not os.path.samefile(APP/rel,PACKAGE/rel),'Writable hardlink: '+rel
 if rel not in changed:assert sha(APP/rel)==row['sha256'].lower(),'Unapproved payload difference: '+rel
for rel in changed-{'veyra_qml_ui.exe'}:assert sha(APP/rel)==sha(ROOT/rel),rel
assert {p.relative_to(APP).as_posix() for p in APP.rglob('*') if p.is_file()}==oldPaths|{'package-manifest.json','CAPTURE-TEST-NOTES.zh-CN.md'}
source=OUT/'Veyra-2.0.5-capture-test-source.zip';assert not source.exists()
subprocess.run(['git','archive','--format=zip','--prefix=Veyra-capture-test/','--output='+str(source),head],cwd=ROOT,check=True,timeout=90)
with zipfile.ZipFile(source) as z:
 assert z.testzip() is None
 for item in z.infolist():assert not re.search(r'\.(dll|lib|exe|pdb|onnx|f16|hsaco|cubin)$',item.filename,re.I),item.filename
 for n in build['sourceFiles']:
  # Archive/check-out EOLs can differ; both must represent the exact compiled text.
  assert z.read('Veyra-capture-test/'+n).replace(b'\r\n',b'\n')==(ROOT/n).read_bytes().replace(b'\r\n',b'\n'),n
manifest={'schema':3,'version':'2.0.5','candidate':'2.0.5-capture-test','displayVersion':'2.0.5-capture-test',
 'releaseReady':False,'localOnly':True,'gpuPackage':'NVIDIA','baseCommit':build['sourceHead'],'sourceArchiveCommit':head,
 'buildInputReceipt':'capture-build-v4.json','compiledSourceFilesVerified':len(build['sourceFiles']),
 'publishedBaseVersion':'2.0.5','publishedBaseExeSha256':sha(PACKAGE/'veyra_qml_ui.exe'),
 'correspondingSource':{'application':'../'+source.name,'applicationSha256':sha(source),
  'dependencies':old['correspondingSource']['dependencies'],'dependencySha256':old['correspondingSource']['dependencySha256'],
  'instructions':'docs/BUILD_2.0.5.md','displayVersionOverride':'-DVEYRA_DISPLAY_VERSION=2.0.5-capture-test'},
 'validation':'Local RTX5070 GPU pixels + KUHAIMI27P USB/GUI only; Blackmagic physical hardware awaiting field test',
 'knownIssues':['Physical Blackmagic HDMI/SDI routing and model-specific property pages not validated',
  'No new deinterlacing or native v210 conversion','Community NR initialization exception in supplied log is separate and unresolved',
  'NVIDIA VRAM growth remains unresolved; higher VFG multipliers retain prior test-candidate limits'],
 'files':[]}
for p in sorted(APP.rglob('*')):
 if p.is_file() and p.name!='package-manifest.json':manifest['files'].append({'path':p.relative_to(APP).as_posix(),'size':p.stat().st_size,'sha256':sha(p)})
(APP/'package-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
portable=OUT/(APP.name+'.zip');assert not portable.exists();start=time.monotonic()
with zipfile.ZipFile(portable,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=3,allowZip64=True) as z:
 for p in sorted(APP.rglob('*')):
  if p.is_file():z.write(p,APP.name+'/'+p.relative_to(APP).as_posix())
expected={APP.name+'/'+x['path']:x['sha256'] for x in manifest['files']}
expected[APP.name+'/package-manifest.json']=sha(APP/'package-manifest.json')
with zipfile.ZipFile(portable) as z:
 assert len(z.namelist())==len(expected) and set(z.namelist())==set(expected)
 for name,d in expected.items():
  with z.open(name) as f:assert hashlib.file_digest(f,'sha256').hexdigest()==d,name
 assert z.testzip() is None
print('Portable ZIP payload verified',len(expected),'files',round(time.monotonic()-start,3),'seconds',flush=True)
# Only task-owned generated CUDA caches are disposable. Preserve profiles,
# screenshots, logs, current build and final deliverables.
removed=[];taskTmp=(BASE/'tmp'/TASK).resolve()
for cache in sorted(taskTmp.rglob('cuda-cache')):
 resolved=cache.resolve();assert resolved.is_relative_to(taskTmp) and resolved!=taskTmp and cache.is_dir() and not cache.is_symlink()
 size=sum(p.stat().st_size for p in cache.rglob('*') if p.is_file())
 shutil.rmtree(resolved);removed.append({'path':str(resolved),'bytes':size})
subprocess.run(guard,cwd=ROOT,check=True)
assert not git('status','--porcelain=v1')
result={'passed':True,'head':head,'branch':git('branch','--show-current'),'candidate':str(APP),
 'exeSha256':exe,'source':str(source),'sourceSha256':sha(source),'portable':str(portable),'portableSha256':sha(portable),
 'packageManifestSha256':sha(APP/'package-manifest.json'),'compiledSourceFilesVerified':len(build['sourceFiles']),
 'payloadFiles':len(expected),'contracts':contracts,'rate':rate,'gui':gui,'properties':props,
 'otherWorktreesAndPublishedPackagesPreserved':True,'runtimeModelShaderBytesUnchanged':True,'noProprietaryBinariesInSource':True,
 'blackmagicPhysicalValidation':False,'sourceAndPortableCrcAndPayloadVerified':True,'removedTaskCaches':removed}
VERIFY.mkdir(parents=True,exist_ok=True);(VERIFY/'final-check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({k:result[k] for k in ('passed','head','candidate','exeSha256','portable','portableSha256','source','payloadFiles','compiledSourceFilesVerified')},ensure_ascii=False),flush=True)
