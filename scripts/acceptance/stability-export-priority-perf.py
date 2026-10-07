"""Serial released-package A/B at identical original-NR settings; no GPU load generator."""
from pathlib import Path
import ctypes,hashlib,json,os,re,shutil,statistics,subprocess,sys,time
import psutil
from ctypes import wintypes
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='stability-export-priority-20261006'
PACKAGES={'203':BASE/'releases/release-2.0.3-20261004/Veyra-2.0.3-NVIDIA-win64-portable',
 '204':BASE/'releases/publish-2.0.4-20261006/packages/Veyra-2.0.4-NVIDIA-win64-portable',
 'fix2':BASE/'test-packages'/TASK/'Veyra-2.0.4-fix2-NVIDIA-win64-portable'}
MEDIA=Path('E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4') if '--gta' in sys.argv else BASE/'tests/perf-matrix/media/M2.mkv'
def sha(f):
 with Path(f).open('rb') as s:return hashlib.file_digest(s,'sha256').hexdigest()
def copy(src,dst):
 if Path(src).suffix.lower() in ('.dll','.bin','.hsaco','.ptx','.onnx','.cubin','.f16'):os.link(src,dst)
 else:shutil.copy2(src,dst)
 return dst
def gpu():
 p=subprocess.run(['nvidia-smi','--query-gpu=name,driver_version,utilization.gpu,memory.used,clocks.current.graphics,power.draw','--format=csv,noheader,nounits'],capture_output=True,text=True,timeout=8)
 return p.stdout.strip() if p.returncode==0 else 'query failed '+str(p.returncode)
def query_priority(proc):
 gdi=ctypes.WinDLL('gdi32');fn=gdi.D3DKMTGetProcessSchedulingPriorityClass
 fn.argtypes=[wintypes.HANDLE,ctypes.POINTER(ctypes.c_int)];fn.restype=wintypes.LONG
 out=ctypes.c_int(-1);status=fn(int(proc._handle),ctypes.byref(out))
 return {'status':hex(status&0xffffffff),'actual':out.value}
def set_priority(proc,priority):
 gdi=ctypes.WinDLL('gdi32');fn=gdi.D3DKMTSetProcessSchedulingPriorityClass
 fn.argtypes=[wintypes.HANDLE,ctypes.c_int];fn.restype=wintypes.LONG
 return hex(fn(int(proc._handle),{'normal':2,'high':4,'realtime':5}[priority])&0xffffffff)
def foreground_pid():
 user=ctypes.WinDLL('user32');user.GetForegroundWindow.restype=wintypes.HWND
 user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
 pid=wintypes.DWORD();user.GetWindowThreadProcessId(user.GetForegroundWindow(),ctypes.byref(pid));return pid.value
def focus_owned(proc):
 user=ctypes.WinDLL('user32');callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
 windows=[]
 def collect(hwnd,_):
  pid=wintypes.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
  if pid.value==proc.pid and user.IsWindowVisible(hwnd):windows.append(hwnd)
  return True
 user.EnumWindows(callback(collect),0)
 if windows:user.SetForegroundWindow(windows[0])
def run(variant,label,priority='normal',seconds=40,test_env=None,explicit=False,display=2,reduced=False,page='pro',software=False,oldUi=False):
 assert label.replace('-','').isalnum() and priority in ('normal','high','realtime')
 subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/stability-export-priority-control.py')],check=True)
 if variant in PACKAGES:pkg=PACKAGES[variant]
 else:pkg=BASE/'tests'/TASK/('candidate-'+variant)
 app=BASE/'tests'/TASK/('perf-'+label)
 assert not app.exists();shutil.copytree(pkg,app,copy_function=copy,ignore=shutil.ignore_patterns('*.log','logs','user-data-*','*.dmp','*.pdb'))
 if oldUi:shutil.copytree(PACKAGES['203']/'qml',app/'qml',dirs_exist_ok=True)
 shutil.copy2(ROOT/'scripts/acceptance/stability-export-priority-perf.qml',app/'qml/Veyra/RegressionPerf.qml')
 conf={'modern':variant!='203','seconds':seconds,'media':MEDIA.as_posix(),'display':display,'reducedMotion':reduced,'page':page,'oldUi':oldUi}
 main=app/'qml/Veyra/Main.qml';text=main.read_text(encoding='utf8');at=text.rfind('}')
 text=text[:at]+'\n Loader { source:"RegressionPerf.qml"; onLoaded:item.config='+json.dumps(conf,ensure_ascii=True)+' }\n'+text[at:];main.write_text(text,encoding='utf8')
 out=BASE/'logs'/TASK/label;profile=BASE/'tests'/TASK/label/'profile';tmp=BASE/'tmp'/TASK/label
 for p in (out,profile,tmp):p.mkdir(parents=True,exist_ok=False)
 (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','gpuPriority':priority,'decode':'auto','reducedMotion':reduced,'obsGameCapture':software}),encoding='utf8')
 env=os.environ.copy()
 for key in tuple(env):
  if key.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_')):env.pop(key)
 env.update(TEMP=str(tmp),TMP=str(tmp),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_LOG_FILE=str(out/'player.log'),VEYRA_VERBOSE_FRAME_LOGS='1')
 if test_env:env.update(test_env)
 mediaReceipt=BASE/'logs'/TASK/('gta-media.json' if '--gta' in sys.argv else 'm2-media.json')
 st=MEDIA.stat();mediaIdentity={'path':str(MEDIA),'bytes':st.st_size,'mtimeNs':st.st_mtime_ns}
 prior=json.loads(mediaReceipt.read_text(encoding='utf8')) if mediaReceipt.exists() else {}
 if any(prior.get(k)!=v for k,v in mediaIdentity.items()):
  mediaIdentity['sha256']=sha(MEDIA);mediaReceipt.write_text(json.dumps(mediaIdentity,ensure_ascii=False,indent=2),encoding='utf8')
 else:mediaIdentity=prior
 receipt={'uiSourceHashes':{n:sha(app/'qml/Veyra'/n) if (app/'qml/Veyra'/n).is_file() else None for n in ('ProPage.qml','NodePage.qml','VTelemetryValue.qml','qmldir')},'variant':variant,'label':label,'priorityRequested':priority,'mediaSha256':mediaIdentity['sha256'],'exeSha256':sha(app/'veyra_qml_ui.exe'),'nrSha256':sha(app/'runtime/experimental/nr-original/nvngx_dlssnr.dll'),'gpuBefore':gpu(),'config':conf,'testEnv':test_env or {},'softwareUiRequested':software}
 t=time.monotonic();meters=[]
 with (out/'console.log').open('xb') as stream:
  proc=subprocess.Popen([str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page',page,'--size','1280x800','--exit-after','125000'],cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT)
  applied=False
  try:
   while proc.poll() is None and time.monotonic()-t<140:
    elapsed=time.monotonic()-t
    if 3<elapsed and foreground_pid()!=proc.pid:focus_owned(proc)
    if explicit and not applied and elapsed>10 and query_priority(proc)['status']=='0x0':
     receipt['explicitSetStatus']=set_priority(proc,priority);applied=True
    meters.append({'elapsed':elapsed,'gpu':gpu(),'priority':query_priority(proc),'foregroundOwned':foreground_pid()==proc.pid})
    time.sleep(1)
   if proc.poll() is None:proc.kill();proc.wait(timeout=8);rc=124
   else:rc=proc.returncode
  finally:
   if proc.poll() is None:proc.kill();proc.wait(timeout=8)
 text=(out/'player.log').read_text(encoding='utf8',errors='replace');rows=[];raw=[]
 for line in text.splitlines():
  if 'REGRESSION_SAMPLE ' in line:
   r=json.loads(line.split('REGRESSION_SAMPLE ',1)[1]);
   if r['ready'] and 10<=r['position']<=48:rows.append(r)
  if '[player-timing]' in line:raw.append(dict((k,float(v)) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',line)))
 names=sorted({r['label'] for row in rows for r in row['stages'] if r['measured']})
 summary={}
 for name in names:
  values=[r['ms'] for row in rows for r in row['stages'] if r['label']==name and r['measured']]
  summary[name]={'medianOfOneSecondMeans':statistics.median(values),'min':min(values),'max':max(values),'observations':len(values)}
 passed=rc==0 and 'REGRESSION_PASS' in text and not any(s in text for s in ('REGRESSION_FAIL','ReferenceError:','TypeError:','[ERROR]','[FATAL]')) and len(rows)>=10
 uiRates=[b['uiFrames']-a['uiFrames'] for a,b in zip(rows,rows[1:])]
 receipt.update(uiFramesPerSecondMedian=statistics.median(uiRates) if uiRates else None,exitCode=rc,wallSeconds=time.monotonic()-t,passed=passed,summary=summary,foregroundSamples=sum(r['active'] for r in rows),observations=len(rows),medianSubmitFps=statistics.median(r['fps'] for r in rows) if rows else 0)
 for name,obj in [('result',receipt),('meters',meters),('samples',rows),('raw-timing',raw)]:
  (out/(name+'.json')).write_text(json.dumps(obj,ensure_ascii=False,indent=2),encoding='utf8')
 print(json.dumps(receipt,ensure_ascii=False),flush=True)
 if not passed:print('\n'.join(text.splitlines()[-20:]));raise SystemExit(1)
if __name__=='__main__':run(*sys.argv[1:4],seconds=int(sys.argv[4]) if len(sys.argv)>4 else 40,explicit='--explicit' in sys.argv,display=0 if '--tearing' in sys.argv else 2,
    reduced='--reduced' in sys.argv,page='min' if '--minimal' in sys.argv else 'pro',software='--software' in sys.argv,oldUi='--old-ui' in sys.argv,
    test_env={key:value for flag,key,value in [('--compute','VEYRA_TEST_GRAPH_COMPUTE','1'),('--d3d11','VEYRA_UI_RHI','d3d11'),('--basic','QSG_RENDER_LOOP','basic'),('--scene-info','QSG_INFO','1')] if flag in sys.argv})
