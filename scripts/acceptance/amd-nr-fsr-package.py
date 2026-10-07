"""Prepare, cold-start and audit the local AMD candidate; never publish."""
from pathlib import Path
import ast,hashlib,json,os,re,shutil,subprocess,sys,time,zipfile,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='amd-nr-fsr-handoff-20261007'
LOGS=BASE/'logs'/TASK;OUT=BASE/'test-packages'/TASK;VERIFY=BASE/'verify'/TASK
APP=OUT/'Veyra-2.0.5-amd-nr-test-AMD-win64-portable'
PACKAGE=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-AMD-win64-portable'
BUILD=BASE/'build'/TASK
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT).decode('utf8').strip()
def verifyBuild():
 build=read(LOGS/'build-after.json');assert build['exit']==0
 for n,d in build['sourceFiles'].items():assert sha(ROOT/n)==d,'Build input changed: '+n
 original=(ROOT/'src/pipeline/EnhanceGraph.cpp').read_text(encoding='utf8')
 before='ring_.queue(),context_.adapter().vendorId) || !instance->amd->admits'
 after='ring_.queue(),0x1002 /* identity fixture only */) || !instance->amd->admits'
 assert original.count(before)==1
 assert (BUILD/'amd-nr-graph-test/EnhanceGraph.cpp').read_text(encoding='utf8')==original.replace(before,after),'Unexpected graph test substitution'
 return build
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/amd-nr-fsr-control.py')]
subprocess.run(guard,cwd=ROOT,check=True)
assert not any(p.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_amd_nr_graph_tests.exe','veyra_lmxxf_nr_tests.exe') for p in psutil.process_iter(['name']))
mode=sys.argv[1];assert mode in ('prepare','cold','final')
build=verifyBuild();old=read(PACKAGE/'package-manifest.json');exe=sha(BUILD/'veyra_qml_ui.exe')
changed={'veyra_qml_ui.exe','qml/Veyra/DialogHost.qml','THIRD_PARTY_NOTICES.md'}
for row in old['files']:
 assert sha(PACKAGE/row['path'])==row['sha256'].lower(),row['path']
 if row['path'].startswith('shaders/'):
  assert sha(BUILD/row['path'])==row['sha256'].lower(),'Shader differs from actual GPU-tested build: '+row['path']
if mode=='prepare':
 assert not APP.exists();OUT.mkdir(parents=True,exist_ok=True)
 shutil.copytree(PACKAGE,APP,copy_function=shutil.copy2)
 shutil.copy2(BUILD/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
 for n in changed-{'veyra_qml_ui.exe'}:shutil.copy2(ROOT/n,APP/n)
 result={'candidate':str(APP),'exeSha256':exe,'build':'build-after','independentCopies':True,'shadersIdenticalToGpuTest':True}
 (LOGS/'prepare.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(result,ensure_ascii=False))
elif mode=='cold':
 assert sha(APP/'veyra_qml_ui.exe')==exe
 label=sys.argv[2];assert label.replace('-','').isalnum()
 out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=LOGS/label
 for p in (out,tmp,logs):p.mkdir(parents=True,exist_ok=False)
 profile=out/'profile';profile.mkdir()
 (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','gpuPriority':'normal','obsGameCapture':False}),encoding='utf8')
 env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
 windows=Path(env['WINDIR']);engine=logs/'engine.log';console=logs/'console.log'
 env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),VEYRA_LOG_FILE=str(engine),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',PATH=os.pathsep.join(map(str,(windows/'System32',windows,windows/'System32/Wbem'))))
 video=BASE/'tests/field-fixes-20261005/media/fx-embedded.mkv';assert video.is_file()
 args=[str(APP/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','10000',str(video)]
 start=time.monotonic();modules=[]
 with console.open('xb') as f:
  process=subprocess.Popen(args,cwd=APP,env=env,stdout=f,stderr=subprocess.STDOUT)
  try:
   time.sleep(3)
   for module in psutil.Process(process.pid).memory_maps():
    p=Path(module.path)
    if p.name.startswith(('Qt6','avcodec-','avformat-','avutil-','swscale-','swresample-')):
     assert p.resolve().is_relative_to(APP.resolve()),str(p);modules.append(str(p))
   rc=process.wait(timeout=50)
  except BaseException:
   process.kill();process.wait(timeout=10);raise
 text=engine.read_text(encoding='utf8',errors='replace')+'\n'+console.read_text(encoding='utf8',errors='replace')
 bad=[line for line in text.splitlines() if any(word in line for word in ('[ERROR]','[FATAL]','ReferenceError:','TypeError:','no root object'))]
 passed=rc==0 and not bad and len(modules)>10 and 'track ready events=5' in text and 'fonts configured container=1' in text
 result={'passed':bool(passed),'exit':rc,'seconds':time.monotonic()-start,'exeSha256':exe,'command':args,'ownQtAndCodecModules':modules,'console':str(console),'engine':str(engine),'errors':bad[:20],'scope':'NVIDIA local cold startup/playback with default effects off; not AMD inference'}
 (logs/'summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
 assert passed,result
else:
 assert not git('status','--porcelain=v1'),'Commit the reviewed source before archiving'
 subprocess.run(['git','diff','--check'],cwd=ROOT,check=True)
 for p in (ROOT/'scripts/acceptance').glob('amd-nr-fsr-*.py'):ast.parse(p.read_text(encoding='utf8'),filename=str(p))
 tests={label:read(LOGS/label/'summary.json') for label in ('pixels-before','pixels-after','abi-after','chain-after')}
 for label,rows in tests.items():
  assert len(rows)==1 and rows[0]['passed']
  row=rows[0];assert sha(Path(row['command'][0]))==row['exeSha256']
  assert row['providerSha256']==sha(BUILD/'lmxxf-test-runtime/LmxxfNrRuntime.dll')
  if label!='pixels-before':assert row['sourceGraphSha256']==sha(ROOT/'src/pipeline/EnhanceGraph.cpp')
 assert tests['pixels-before'][0]['exit']==1 and tests['pixels-after'][0]['exit']==0
 cold=read(LOGS/'cold-after/summary.json');assert cold['passed'] and cold['exeSha256']==exe
 assert sha(APP/'veyra_qml_ui.exe')==exe
 note=ROOT/'docs/AMD_NR_FSR_REPORT_2026-10-07.md';assert exe in note.read_text(encoding='utf8')
 shutil.copy2(note,APP/'AMD-NR-TEST-NOTES.zh-CN.md')
 oldPaths={x['path'] for x in old['files']}
 for row in old['files']:
  rel=row['path'];assert not os.path.samefile(APP/rel,PACKAGE/rel),'Writable hardlink: '+rel
  if rel not in changed:assert sha(APP/rel)==row['sha256'].lower(),'Unexpected payload change: '+rel
 for rel in changed-{'veyra_qml_ui.exe'}:assert sha(APP/rel)==sha(ROOT/rel),rel
 assert sha(APP/'runtime/amd-nr/LmxxfNrRuntime.dll')!=sha(BUILD/'lmxxf-test-runtime/LmxxfNrRuntime.dll'),'Test fixture must never ship'
 assert {p.relative_to(APP).as_posix() for p in APP.rglob('*') if p.is_file()}==oldPaths|{'package-manifest.json','AMD-NR-TEST-NOTES.zh-CN.md'}
 head=git('rev-parse','HEAD');source=OUT/'Veyra-2.0.5-amd-nr-test-source.zip';assert not source.exists()
 subprocess.run(['git','archive','--format=zip','--prefix=Veyra-amd-nr-test/','--output='+str(source),head],cwd=ROOT,check=True,timeout=90)
 with zipfile.ZipFile(source) as z:
  assert z.testzip() is None
  for item in z.infolist():assert not re.search(r'\.(dll|lib|exe|pdb|onnx|f16|hsaco|cubin)$',item.filename,re.I),item.filename
  for n in build['sourceFiles']:
   assert z.read('Veyra-amd-nr-test/'+n).replace(b'\r\n',b'\n')==(ROOT/n).read_bytes().replace(b'\r\n',b'\n'),n
 manifest={'schema':3,'version':'2.0.5','candidate':'2.0.5-amd-nr-test','displayVersion':'2.0.5-amd-nr-test',
  'releaseReady':False,'localOnly':True,'gpuPackage':'AMD','baseCommit':build['sourceHead'],'sourceArchiveCommit':head,
  'buildInputReceipt':'build-after.json','compiledSourceFilesVerified':len(build['sourceFiles']),
  'publishedBaseVersion':'2.0.5','publishedBaseExeSha256':sha(PACKAGE/'veyra_qml_ui.exe'),
  'correspondingSource':{'application':'../'+source.name,'applicationSha256':sha(source),
   'dependencies':old['correspondingSource']['dependencies'],'dependencySha256':old['correspondingSource']['dependencySha256'],
   'instructions':'docs/BUILD_2.0.5.md','displayVersionOverride':'-DVEYRA_DISPLAY_VERSION=2.0.5-amd-nr-test'},
  'validation':'RTX5070 actual FSR3.1.5 + non-identity/identity test NR provider: shared handoff only; AMD HIP/RX9070/XSX field test pending',
  'includedPriorCandidates':['VFG optimization cc22e88','Blackmagic capture compatibility 4294341'],
  'knownIssues':['No local AMD HIP inference or Xbox streaming hardware verification','Independent XSX hardware decode fallback in field log is unchanged',
   'Physical Blackmagic compatibility remains unverified','NVIDIA VRAM growth remains unresolved'],
  'files':[]}
 for p in sorted(APP.rglob('*')):
  if p.is_file() and p.name!='package-manifest.json':manifest['files'].append({'path':p.relative_to(APP).as_posix(),'size':p.stat().st_size,'sha256':sha(p)})
 (APP/'package-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
 portable=OUT/(APP.name+'.zip');assert not portable.exists();start=time.monotonic()
 with zipfile.ZipFile(portable,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=3,allowZip64=True) as z:
  for p in sorted(APP.rglob('*')):
   if p.is_file():z.write(p,APP.name+'/'+p.relative_to(APP).as_posix())
 expected={APP.name+'/'+x['path']:x['sha256'] for x in manifest['files']};expected[APP.name+'/package-manifest.json']=sha(APP/'package-manifest.json')
 with zipfile.ZipFile(portable) as z:
  assert len(z.namelist())==len(expected) and set(z.namelist())==set(expected)
  for name,d in expected.items():
   with z.open(name) as f:assert hashlib.file_digest(f,'sha256').hexdigest()==d,name
  assert z.testzip() is None
 print('ZIP payload verified',len(expected),'files',round(time.monotonic()-start,3),'seconds',flush=True)
 subprocess.run(guard,cwd=ROOT,check=True);assert not git('status','--porcelain=v1')
 result={'passed':True,'head':head,'branch':git('branch','--show-current'),'candidate':str(APP),'exeSha256':exe,
  'source':str(source),'sourceSha256':sha(source),'portable':str(portable),'portableSha256':sha(portable),
  'packageManifestSha256':sha(APP/'package-manifest.json'),'compiledSourceFilesVerified':len(build['sourceFiles']),
  'payloadFiles':len(expected),'tests':tests,'cold':cold,'otherWorktreesAndOriginalPackagesPreserved':True,
  'runtimeModelShaderBytesUnchanged':True,'noTestRuntimeInCandidate':True,'noProprietaryBinariesInSource':True,
  'amdHardwareValidation':False,'sourceAndPortableCrcAndPayloadVerified':True}
 VERIFY.mkdir(parents=True,exist_ok=True);(VERIFY/'final-check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
 print(json.dumps({k:result[k] for k in ('passed','head','candidate','exeSha256','portable','portableSha256','source','payloadFiles','compiledSourceFilesVerified')},ensure_ascii=False),flush=True)
if mode!='final':subprocess.run(guard,cwd=ROOT,check=True)
