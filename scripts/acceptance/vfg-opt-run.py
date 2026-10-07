"""Serial production VFG measurements; private profile/staging, normal playback only."""
from pathlib import Path
import argparse, ctypes, hashlib, json, os, re, shutil, statistics, subprocess, sys, time
from ctypes import wintypes
import psutil

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra'); TASK='vfg-optimization-20261007'
PACKAGE=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-NVIDIA-win64-portable'
APP=BASE/'tests'/TASK/'app'
MEDIA=Path('E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4')
EXPECTED_EXE='acd49a23074b58ec9698d260d63b2d5a59a640627e09a9b83f0127ae47ebb936'

def sha(path):
 with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def save(path,data):
 path.parent.mkdir(parents=True,exist_ok=True)
 path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf8')

def inherited_state():
 entries=[]
 for line in subprocess.check_output(['git','worktree','list','--porcelain'],cwd=ROOT,encoding='utf8').splitlines():
  if line.startswith('worktree '):
   path=Path(line[9:])
   if path.resolve()==ROOT.resolve():continue
   status=subprocess.check_output(['git','status','--porcelain=v1','-z','--untracked-files=all'],cwd=path)
   changes=[]
   names=subprocess.check_output(['git','ls-files','-m','-o','--exclude-standard','-z'],cwd=path).split(b'\0')
   for name in sorted(set(x for x in names if x)):
    file=path/os.fsdecode(name)
    changes.append({'path':os.fsdecode(name),'sha256':sha(file) if file.is_file() else None})
   entries.append({'path':str(path),'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=path,encoding='ascii').strip(),'statusHex':status.hex(),'changes':changes})
 return entries

def prepare():
 archive=BASE/'archives'/TASK/'app-start.json'
 assert not archive.exists() and not APP.exists()
 assert sha(PACKAGE/'veyra_qml_ui.exe')==EXPECTED_EXE
 runtime=json.loads((PACKAGE/'vfg-runtime-manifest.json').read_text(encoding='utf-8-sig'))
 for item in runtime['files']:assert sha(PACKAGE/item['path'])==item['sha256'].lower(),item['path']
 stat=MEDIA.stat();prior=json.loads((BASE/'logs/release-2.0.5-20261007/gta-media.json').read_text(encoding='utf8'))
 assert stat.st_size==prior['bytes'] and stat.st_mtime_ns==prior['mtimeNs']
 receipt={'sourceHead':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,encoding='ascii').strip(),'worktrees':inherited_state(),'media':prior,'exeSha256':EXPECTED_EXE,'vfgRuntime':runtime,'originalMainSha256':sha(PACKAGE/'qml/Veyra/Main.qml')}
 save(archive,receipt)
 def copy(src,dst):
  if Path(src).suffix.lower() in ('.dll','.bin','.hsaco','.ptx','.onnx','.cubin','.f16'):os.link(src,dst)
  else:shutil.copy2(src,dst)
  return dst
 shutil.copytree(PACKAGE,APP,copy_function=copy,ignore=shutil.ignore_patterns('*.log','logs','user-data-*','*.dmp','*.pdb'))
 shutil.copy2(ROOT/'scripts/acceptance/vfg-diagnosis.qml',APP/'qml/Veyra/VfgDiagnosis.qml')
 print('prepared',APP,flush=True)

def gpu():
 return subprocess.check_output(['nvidia-smi','--query-gpu=utilization.gpu,memory.used,clocks.current.graphics,power.draw','--format=csv,noheader,nounits'],encoding='utf8',timeout=8).strip()

def focus(proc):
 user=ctypes.WinDLL('user32');user.GetForegroundWindow.restype=wintypes.HWND
 user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
 pid=wintypes.DWORD();user.GetWindowThreadProcessId(user.GetForegroundWindow(),ctypes.byref(pid))
 if pid.value==proc.pid:return True
 callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM);windows=[]
 def collect(hwnd,_):
  pid=wintypes.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid))
  if pid.value==proc.pid and user.IsWindowVisible(hwnd):windows.append(hwnd)
  return True
 user.EnumWindows(callback(collect),0)
 if windows:user.SetForegroundWindow(windows[0])
 return False

def run(args):
 assert re.fullmatch(r'[a-z0-9-]+',args.label)
 for proc in psutil.process_iter(['pid','name']):
  assert proc.info['name'].lower() not in ('veyra.exe','veyra_qml_ui.exe'),f'Existing player PID {proc.pid}; no concurrent GPU runs'
 global EXPECTED_EXE
 selected=PACKAGE/'veyra_qml_ui.exe' if args.original else BASE/'build'/TASK/'veyra_qml_ui.exe'
 EXPECTED_EXE=sha(selected);shutil.copy2(selected,APP/'veyra_qml_ui.exe')
 assert sha(APP/'veyra_qml_ui.exe')==EXPECTED_EXE
 out=BASE/'logs'/TASK/args.label;profile=BASE/'tests'/TASK/args.label/'profile';tmp=BASE/'tmp'/TASK/args.label
 for path in (out,profile,tmp):path.mkdir(parents=True,exist_ok=False)
 save(profile/'qml-preferences.v1.json',{'overlayCompat':'off','gpuPriority':'normal','decode':'auto','reducedMotion':False,'obsGameCapture':False})
 config={'media':MEDIA.as_posix(),'multiplier':args.multiplier,'quality':args.quality,'nr':not args.no_nr,'strict':args.strict,'flow':args.flow,'display':0 if args.tearing else 2,'seconds':args.seconds}
 text=(PACKAGE/'qml/Veyra/Main.qml').read_text(encoding='utf8');at=text.rfind('}')
 loader='\nLoader { source: "VfgDiagnosis.qml"; onLoaded: item.config='+json.dumps(config,ensure_ascii=True)+' }\n'
 (APP/'qml/Veyra/Main.qml').write_text(text[:at]+loader+text[at:],encoding='utf8')
 env=os.environ.copy()
 for key in list(env):
  if key.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_')):env.pop(key)
 env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_LOG_FILE=str(out/'player.log'))
 if not args.no_verbose:env['VEYRA_VERBOSE_FRAME_LOGS']='1'
 if args.cpu_profile:env['VEYRA_TEST_VFG_CPU_PROFILE']='1'
 if args.sync_submit:env['VEYRA_TEST_VFG_SYNC_SUBMIT']='1'
 if args.no_reduced:env['VEYRA_TEST_FG_NO_REDUCED']='1'
 if args.software:env['QT_QUICK_BACKEND']='software'
 receipt={'config':config,'exeSha256':EXPECTED_EXE,'gpuBefore':gpu(),'noReduced':args.no_reduced,'softwareUi':args.software,'verboseFrameLogs':not args.no_verbose,'cpuProfile':args.cpu_profile,'syncSubmit':args.sync_submit,'selectedBinary':str(selected)}
 started=time.monotonic();meters=[]
 with (out/'console.log').open('xb') as stream:
  proc=subprocess.Popen([str(APP/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','135000'],cwd=APP,env=env,stdout=stream,stderr=subprocess.STDOUT)
  receipt['pid']=proc.pid
  try:
   while proc.poll() is None and time.monotonic()-started<145:
    meters.append({'elapsed':time.monotonic()-started,'foregroundAlreadyOwned':focus(proc),'gpu':gpu()});time.sleep(1)
   if proc.poll() is None:proc.kill();proc.wait(timeout=10);rc=124
   else:rc=proc.returncode
  finally:
   if proc.poll() is None:proc.kill();proc.wait(timeout=10)
 text=(out/'player.log').read_text(encoding='utf8',errors='replace');rows=[]
 for line in text.splitlines():
  if 'VFG_DIAG_SAMPLE ' in line:
   row=json.loads(line.split('VFG_DIAG_SAMPLE ',1)[1])
   if row['ready'] and 5<=row['seconds']<=args.seconds+5:rows.append(row)
 stages={}
 for name in sorted({stage['label'] for row in rows for stage in row['stages'] if stage['measured']}):
  values=[stage['ms'] for row in rows for stage in row['stages'] if stage['label']==name and stage['measured']]
  stages[name]={'medianOneSecondMeansMs':statistics.median(values),'min':min(values),'max':max(values)}
 median=lambda key:statistics.median([r[key] for r in rows]) if rows else None
 receipt.update(exitCode=rc,wallSeconds=time.monotonic()-started,passed=rc==0 and 'VFG_DIAG_PASS' in text and 'VFG_DIAG_FAIL' not in text and len(rows)>=args.seconds-3,observations=len(rows),fps=median('fps'),totalMs=median('total'),budgetMs=median('budget'),gpuPercent=median('gpu'),status=sorted({r['status'] for r in rows}),detail=sorted({r['detail'] for r in rows}),stages=stages,errors=[line for line in text.splitlines() if any(x in line for x in ('[ERROR]','[FATAL]','TypeError:','ReferenceError:'))])
 receipt['normalSchedulingClass']=[line for line in text.splitlines() if 'priority' in line.lower()][:5]
 for name,data in [('result',receipt),('samples',rows),('meters',meters)]:save(out/(name+'.json'),data)
 print(json.dumps(receipt,ensure_ascii=False),flush=True)
 if not receipt['passed']:
  print('\n'.join(text.splitlines()[-20:]));raise SystemExit(1)

def audit():
 start=json.loads((BASE/'archives'/TASK/'app-start.json').read_text(encoding='utf8'))
 assert start['worktrees']==inherited_state(),'Other worktree changed'
 assert sha(PACKAGE/'veyra_qml_ui.exe')==start['exeSha256']
 assert sha(PACKAGE/'qml/Veyra/Main.qml')==start['originalMainSha256']
 for item in start['vfgRuntime']['files']:assert sha(PACKAGE/item['path'])==item['sha256'].lower()
 print('original worktrees and published app/runtime unchanged',flush=True)

parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['prepare','case','audit','matrix']);parser.add_argument('label',nargs='?')
parser.add_argument('--multiplier',type=int,choices=range(2,9),default=2);parser.add_argument('--quality',type=int,choices=range(3),default=1)
parser.add_argument('--no-nr',action='store_true');parser.add_argument('--strict',action='store_true');parser.add_argument('--flow',action='store_true');parser.add_argument('--tearing',action='store_true');parser.add_argument('--no-reduced',action='store_true');parser.add_argument('--software',action='store_true');parser.add_argument('--no-verbose',action='store_true');parser.add_argument('--seconds',type=int,default=20)
parser.add_argument('--original',action='store_true');parser.add_argument('--cpu-profile',action='store_true')
parser.add_argument('--sync-submit',action='store_true')
args=parser.parse_args()
if args.mode=='prepare':prepare()
elif args.mode=='case':run(args)
elif args.mode=='matrix':
 for quality in (1,0,2):
  for multiplier in range(2,9):
   label=f'{("low","medium","high")[quality]}{multiplier}-nr'
   output=BASE/'logs'/TASK/label/'result.json'
   if output.exists():
    result=json.loads(output.read_text(encoding='utf8'));assert result['passed']
    continue
   command=[sys.executable,'-B',str(Path(__file__).resolve()),'case',label,'--quality',str(quality),'--multiplier',str(multiplier)]
   subprocess.run(command,cwd=ROOT,check=True,timeout=160)
else:audit()
