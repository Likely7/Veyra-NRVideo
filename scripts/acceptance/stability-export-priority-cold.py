"""Cold portable starts with package-only Qt/FFmpeg and private profiles."""
from pathlib import Path
import os,sys,json,subprocess,time,hashlib,ctypes
from ctypes import wintypes
BASE=Path('E:/项目/Veyra');TASK='stability-export-priority-20261006'
ROOT=Path(__file__).resolve().parents[2]
label=sys.argv[1];assert label.replace('-','').isalnum()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/stability-export-priority-control.py')],check=True)
results=[]
api=ctypes.WinDLL('psapi')
api.EnumProcessModulesEx.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.HMODULE),wintypes.DWORD,ctypes.POINTER(wintypes.DWORD),wintypes.DWORD]
api.EnumProcessModulesEx.restype=wintypes.BOOL
api.GetModuleFileNameExW.argtypes=[wintypes.HANDLE,wintypes.HMODULE,wintypes.LPWSTR,wintypes.DWORD]
api.GetModuleFileNameExW.restype=wintypes.DWORD
def loaded_modules(process):
    # Read the loader's paths. Mapped section paths can retain a prior hardlink
    # alias even though the loader selected this package's DLL correctly.
    modules=(wintypes.HMODULE*512)();needed=wintypes.DWORD()
    if not api.EnumProcessModulesEx(int(process._handle),modules,ctypes.sizeof(modules),ctypes.byref(needed),3):return []
    paths=[]
    for module in modules[:min(512,needed.value//ctypes.sizeof(wintypes.HMODULE))]:
        name=ctypes.create_unicode_buffer(32768)
        if api.GetModuleFileNameExW(int(process._handle),module,name,len(name)):paths.append(name.value)
    return paths
for vendor in ('AMD','NVIDIA'):
    app=BASE/'test-packages'/TASK/f'Veyra-2.0.4-fix2-{vendor}-win64-portable'
    out=BASE/'tests'/TASK/(label+'-'+vendor);tmp=BASE/'tmp'/TASK/(label+'-'+vendor)
    for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
    logs=BASE/'logs'/TASK;console=logs/(label+'-'+vendor+'.log')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
    env.update(TEMP=str(tmp),TMP=str(tmp),PATH=os.environ['SystemRoot']+'/System32;'+os.environ['SystemRoot'],
        QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_LOG_FILE=str(logs/(label+'-'+vendor+'-engine.log')))
    args=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(out/'profile'),'--size','1280x800','--exit-after','5000']
    modules=set();begin=time.monotonic()
    with console.open('xb') as stream:
        process=subprocess.Popen(args,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT)
        while process.poll() is None and time.monotonic()-begin<35:
            modules.update(p for p in loaded_modules(process) if p.lower().endswith('.dll'))
            time.sleep(.15)
        if process.poll() is None:process.kill();process.wait(timeout=5);rc=124
        else:rc=process.returncode
    body=console.read_text(encoding='utf8',errors='replace')
    required=[p for p in modules if Path(p).name.lower().startswith(('qt6','avcodec','avformat','avutil','swscale','swresample'))]
    passed=rc==0 and len(required)>=12 and all(Path(p).resolve().is_relative_to(app.resolve()) for p in required) and not any(x in body for x in ('ReferenceError:','TypeError:','[ERROR]','[FATAL]','failed to load'))
    results.append(dict(vendor=vendor,passed=passed,exit=rc,seconds=time.monotonic()-begin,exeSha256=hashlib.sha256((app/'veyra_qml_ui.exe').read_bytes()).hexdigest(),packageModules=sorted(required),privateProfile=str(out/'profile')))
    print(vendor,'PASS' if passed else 'FAIL','modules',len(required),flush=True)
(BASE/'logs'/TASK/(label+'.json')).write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
