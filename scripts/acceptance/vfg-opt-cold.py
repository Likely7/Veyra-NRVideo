"""Cold-start the independent candidate with a private tested VFG/NR profile."""
from pathlib import Path
import ctypes,hashlib,json,os,re,shutil,subprocess,sys,time,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007'
VERIFY=BASE/'verify'/TASK;receipt=json.loads((VERIFY/'candidate-package.json').read_text(encoding='utf8'));APP=Path(receipt['app'])
assert not any(p.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_vfg_gpu_tests.exe','veyra_vfg_export_probe.exe') for p in psutil.process_iter(['name']))
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-opt-control.py')],cwd=ROOT,check=True)
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
manifest=json.loads((APP/'package-manifest.json').read_text(encoding='utf8'))
for row in manifest['files']:assert sha(APP/row['path'])==row['sha256'],row['path']
assert sha(APP/'veyra_qml_ui.exe')==receipt['exeSha256']
def vfg4_running(text):
 return bool(re.search(r'\[vfg\] SDK version=.* multiplier=4 quality=1 async=1',text)) and bool(re.search(r'\[fg-validity\].* valid=[1-9][0-9]* invalid=0.*backend=NVIDIA-VFG',text))
def verify_payload():
 for row in manifest['files']:assert sha(APP/row['path'])==row['sha256'],'Candidate payload changed during startup: '+row['path']
 assert {x.relative_to(APP).as_posix() for x in APP.rglob('*') if x.is_file()}=={x['path'] for x in manifest['files']}|{'package-manifest.json'},'Unexpected candidate payload'
if '--verify-existing' in sys.argv:
 # Preserve the failed harness receipt and re-evaluate its existing successful
 # run; no new GPU playback is needed for a corrected log-field assertion.
 prior=VERIFY/'candidate-cold.json';initial=VERIFY/'candidate-cold-initial.json'
 result=json.loads(prior.read_text(encoding='utf8'));assert not initial.exists() and not result['passed'] and not result['vfg4']
 assert result['exit']==0 and result['exeSha256']==receipt['exeSha256'] and not result['errors'] and len(result['ownQtAndCodecModulePaths'])>10
 assert all(result[k] for k in ('boundedWorker','nr1080','normalGpuPriority'))
 engine=Path(result['engine']);console=Path(result['console']);text=engine.read_text(encoding='utf8',errors='replace')+'\n'+console.read_text(encoding='utf8',errors='replace')
 assert vfg4_running(text)
 verify_payload();shutil.copy2(prior,initial)
 result.update(vfg4=True,passed=True,revalidatedFrom=str(initial),initialReceiptSha256=sha(initial),engineSha256=sha(engine),consoleSha256=sha(console),revalidation='Use actual VFG SDK configuration plus successful fg-validity fields; original playback exit0 preserved')
 prior.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
 print('VFG CANDIDATE COLD START PASS (existing run verified)',len(result['ownQtAndCodecModulePaths']),'own Qt/codec modules',flush=True)
 sys.exit(0)
out,logs,tmp=(BASE/k/TASK/'candidate-cold-v3' for k in ('tests','logs','tmp'))
for p in (out,logs,tmp):p.mkdir(parents=True,exist_ok=False)
profile=out/'profile';shutil.copytree(BASE/'tests'/TASK/'matched-new-medium4/profile',profile)
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
windows=Path(env['WINDIR']);engine=logs/'engine.log';console=logs/'console.log'
env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),VEYRA_LOG_FILE=str(engine),
 QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',
 PATH=os.pathsep.join(map(str,(windows/'System32',windows,windows/'System32/Wbem'))))
cmd=[str(APP/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','28000',
 'E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4']
start=time.monotonic();modules=[]
with console.open('xb') as f:
 process=subprocess.Popen(cmd,cwd=APP,env=env,stdout=f,stderr=subprocess.STDOUT)
 try:
  time.sleep(12)
  kernel=ctypes.WinDLL('kernel32',use_last_error=True);psapi=ctypes.WinDLL('psapi',use_last_error=True)
  kernel.OpenProcess.argtypes=[ctypes.c_ulong,ctypes.c_int,ctypes.c_ulong];kernel.OpenProcess.restype=ctypes.c_void_p
  kernel.CloseHandle.argtypes=[ctypes.c_void_p]
  psapi.EnumProcessModulesEx.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_void_p),ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong),ctypes.c_ulong]
  psapi.GetModuleFileNameExW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_ulong]
  handle=kernel.OpenProcess(0x0410,False,process.pid);assert handle
  try:
   items=(ctypes.c_void_p*1024)();needed=ctypes.c_ulong()
   assert psapi.EnumProcessModulesEx(handle,items,ctypes.sizeof(items),ctypes.byref(needed),3)
   for i in range(min(1024,needed.value//ctypes.sizeof(ctypes.c_void_p))):
    buffer=ctypes.create_unicode_buffer(32768)
    if psapi.GetModuleFileNameExW(handle,items[i],buffer,len(buffer)):
     path=Path(buffer.value)
     if path.name.startswith(('Qt6','avcodec-','avformat-','avutil-','swscale-','swresample-')):
      assert path.resolve().is_relative_to(APP.resolve()),str(path);modules.append(str(path))
  finally:kernel.CloseHandle(handle)
  rc=process.wait(timeout=65)
 except BaseException:
  if process.poll() is None:process.kill();process.wait(timeout=10)
  raise
text=engine.read_text(encoding='utf8',errors='replace')+'\n'+console.read_text(encoding='utf8',errors='replace')
bad=[line for line in text.splitlines() if re.search(r'\[ERROR\s*\]|\[FATAL\s*\]|ReferenceError:|TypeError:|no root object',line)]
result={'exit':rc,'seconds':time.monotonic()-start,'command':cmd,'exeSha256':receipt['exeSha256'],'ownQtAndCodecModulePaths':modules,
 'boundedWorker':'SDK submission=bounded worker' in text,'nr1080':'internal=1920x1080 composite=3840x2160' in text,
 'vfg4':vfg4_running(text),
 'normalGpuPriority':'requested=2 actual=2' in text,'errors':bad,'engine':str(engine),'console':str(console)}
result['passed']=rc==0 and len(modules)>10 and all(result[k] for k in ('boundedWorker','nr1080','vfg4','normalGpuPriority')) and not bad
(VERIFY/'candidate-cold.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
assert result['passed'],result
verify_payload()
print('VFG CANDIDATE COLD START PASS',len(modules),'own Qt/codec modules',result['seconds'],flush=True)
