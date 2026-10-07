"""Local USB and production GUI regressions; this does not validate Blackmagic hardware."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,time,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='blackmagic-capture-20261007'
APP=BASE/'test-packages'/TASK/'Veyra-2.0.5-capture-test-NVIDIA-win64-portable'
PACKAGE=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-NVIDIA-win64-portable'
BUILD=BASE/'build'/TASK
mode,label=sys.argv[1:3];assert mode in ('prepare','rate','ui','properties') and label.replace('-','').isalnum()
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/blackmagic-control.py')],cwd=ROOT,check=True)
assert not any(p.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_capture_tests.exe','veyra_hdr_color_tests.exe') for p in psutil.process_iter(['name']))
logs=BASE/'logs'/TASK/label;logs.mkdir(parents=True,exist_ok=False)
if mode=='prepare':
 build=json.loads((BASE/'logs'/TASK/'capture-build-v4.json').read_text(encoding='utf8'));assert build['exit']==0
 for n,d in build['sourceFiles'].items():assert sha(ROOT/n)==d,n
 old=json.loads((PACKAGE/'package-manifest.json').read_text(encoding='utf-8-sig'))
 for row in old['files']:assert sha(PACKAGE/row['path'])==row['sha256'].lower(),row['path']
 assert not APP.exists();APP.parent.mkdir(parents=True,exist_ok=True)
 shutil.copytree(PACKAGE,APP,copy_function=shutil.copy2)
 shutil.copy2(BUILD/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
 for name in ('qml/Veyra/DialogHost.qml','THIRD_PARTY_NOTICES.md'):shutil.copy2(ROOT/name,APP/name)
 result={'candidate':str(APP),'exeSha256':sha(APP/'veyra_qml_ui.exe'),'build':'capture-build-v4','independentCopies':True}
 (logs/'summary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');print(result,flush=True);sys.exit(0)
out,tmp=(BASE/k/TASK/label for k in ('tests','tmp'))
for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
windows=Path(env['WINDIR']);env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
env['PATH']=os.pathsep.join(map(str,([BUILD,PACKAGE] if mode=='rate' else [])+[windows/'System32',windows,windows/'System32/Wbem']))
results=[]
def run(name,cmd,cwd,marker):
 log=logs/(name+'.log');child=env.copy();child['VEYRA_LOG_FILE']=str(logs/(name+'-engine.log'));start=time.monotonic()
 with log.open('xb') as f:
  try:rc=subprocess.run(cmd,cwd=cwd,env=child,stdout=f,stderr=subprocess.STDOUT,timeout=150).returncode
  except subprocess.TimeoutExpired:rc=124
 text=log.read_text(encoding='utf8',errors='replace')
 passed=rc==0 and marker in text and not re.search(r'^FAIL\b|BLACKMAGIC_UI_FAIL|ReferenceError:|TypeError:|no root object',text,re.M)
 result={'case':name,'command':cmd,'exit':rc,'seconds':time.monotonic()-start,'exeSha256':sha(Path(cmd[0])),'passed':bool(passed),'log':str(log)}
 results.append(result);(logs/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8');print(result,flush=True)
 assert passed,text[-5000:]
 return text
if mode=='rate':
 # Enumerated KUHAIMI 27P device 0, YUY2 VideoInfo2 1080p60 native index 1.
 text=run('usb-1080p60',[str(BUILD/'veyra_capture_tests.exe'),'--rate-test','capture:0:1:-1','0'],out,'PASS RATE pass=1')
 assert len(re.findall(r'PASS RATE pass=[01]',text))==2 and re.search(r'\[capture-upstream\].*inputMediums=0 crossbar=false existing=false hr=0x00000001 routeChanged=0',text)
else:
 assert sha(APP/'veyra_qml_ui.exe')==sha(BUILD/'veyra_qml_ui.exe')
 profile=out/'profile';profile.mkdir();images=out/'screenshots';images.mkdir()
 (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','gpuPriority':'normal','obsGameCapture':False,'screenshotDir':str(images)},ensure_ascii=False),encoding='utf8')
 main=APP/'qml/Veyra/Main.qml';original=main.read_bytes();source=original.decode('utf8');at=source.rfind('}')
 try:
  remembered=''
  for phase in (('properties',) if mode=='properties' else ('capture','restore')):
   if phase=='restore':
    prefs=profile/'capture-preferences.v1';data=prefs.read_text(encoding='utf-16-le');quoted=re.findall(r'"((?:\\.|[^"\\])*)"',data)
    assert len(quoted)>=2;remembered=quoted[1];assert remembered.endswith(':scan=0'),remembered
    shutil.copy2(prefs,out/'capture-preferences-before-legacy.v1')
    prefs.write_text(data.replace(remembered,remembered.rsplit(':scan=',1)[0],1),encoding='utf-16-le')
   setting='item.dialogHost=dialogs;item.evidence='+json.dumps(str(images))+';item.phase='+json.dumps(phase)+';item.rememberedKey='+json.dumps(remembered)+';'
   loader='\nLoader { source: '+json.dumps((ROOT/'scripts/acceptance/blackmagic-ui.qml').as_uri())+'; onLoaded: {'+setting+'} }\n'
   main.write_text(source[:at]+loader+source[at:],encoding='utf8')
   text=run(phase,[str(APP/'veyra_qml_ui.exe'),'--page','pro','--size','1280x900','--data-dir',str(profile),'--exit-after','110000'],APP,{'capture':'BLACKMAGIC_UI_PASS','restore':'BLACKMAGIC_UI_RESTORE_PASS','properties':'BLACKMAGIC_UI_PROPERTIES_PASS'}[phase])
   if phase=='properties':assert re.search(r'\[capture-driver-settings\].*crossbarHr=0x00000001 beforeGetFormatHr=0x00000000 afterGetFormatHr=0x00000000 pageHr=0x80004002',text),'Real COM graph/property fallback was not exercised'
 finally:main.write_bytes(original)
 assert sha(main)==sha(PACKAGE/'qml/Veyra/Main.qml')
 if mode=='ui':assert len(list(images.glob('veyra-*.png')))==1,'Explicit captured-frame screenshot missing'
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/blackmagic-control.py')],cwd=ROOT,check=True)
