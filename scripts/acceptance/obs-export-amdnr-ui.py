"""Own-process GUI export and GDI screenshots of the real software-rendered window."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import os,sys,json,subprocess,shutil,time,re,hashlib
from PIL import Image,ImageChops
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='obs-export-amdnr-20261006'
label,case,version=sys.argv[1:4];out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK
for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
app=BASE/'verify'/TASK/'ui-app'
source=BASE/'releases/publish-2.0.4-20261006/packages/Veyra-2.0.4-NVIDIA-win64-portable'
if not app.exists():shutil.copytree(source,app)
exeSource=source/'veyra_qml_ui.exe' if version=='before' else BASE/'build'/TASK/'veyra_qml_ui.exe'
shutil.copyfile(exeSource,app/'veyra_qml_ui.exe')
if version!='before':
    for f in (BASE/'build'/TASK/'shaders').glob('*'):
        if f.is_file():shutil.copyfile(f,app/'shaders'/f.name)
(out/'outputs').mkdir();(out/'screenshots').mkdir()
main=app/'qml/Veyra/Main.qml';original=main.read_bytes();text=original.decode('utf8');at=text.rfind('}')
fixture=ROOT/'scripts/acceptance/obs-export-amdnr-ui.qml';media=BASE/'tests/release-2.0.4-20261005/nr-fixture.mp4'
injection='\nLoader{anchors.fill:parent;source:'+json.dumps(fixture.as_uri())+';onLoaded:{item.caseName='+json.dumps('obs' if case.startswith('obs') else 'export')+';item.media='+json.dumps(str(media))+'}}\n'
main.write_text(text[:at]+injection+text[at:],encoding='utf8')
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logs/(label+'-engine.log')),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',CUDA_CACHE_PATH=str(tmp/'cuda-cache'))
if case=='obs-full':env['QSG_SOFTWARE_RENDERER_FORCE_PARTIAL_UPDATES']='0'
if 'fractional' in label:env['QT_SCALE_FACTOR']='1.25'
args=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(out/'profile'),'--quit-after','260000','--export-out',str(out/'outputs')]
if case!='native':args+=['--obs-game-capture']
user=C.WinDLL('user32',use_last_error=True);gdi=C.WinDLL('gdi32',use_last_error=True);kernel=C.WinDLL('kernel32',use_last_error=True)
kernel.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];kernel.OpenProcess.restype=W.HANDLE
kernel.CloseHandle.argtypes=[W.HANDLE]
kernel.GetProcessTimes.argtypes=[W.HANDLE,C.POINTER(W.FILETIME),C.POINTER(W.FILETIME),C.POINTER(W.FILETIME),C.POINTER(W.FILETIME)]
user.GetDC.argtypes=[W.HWND];user.GetDC.restype=W.HDC;user.ReleaseDC.argtypes=[W.HWND,W.HDC]
gdi.CreateCompatibleDC.argtypes=[W.HDC];gdi.CreateCompatibleDC.restype=W.HDC
gdi.CreateCompatibleBitmap.argtypes=[W.HDC,C.c_int,C.c_int];gdi.CreateCompatibleBitmap.restype=W.HANDLE
gdi.SelectObject.argtypes=[W.HDC,W.HANDLE];gdi.SelectObject.restype=W.HANDLE
gdi.BitBlt.argtypes=[W.HDC,C.c_int,C.c_int,C.c_int,C.c_int,W.HDC,C.c_int,C.c_int,W.DWORD]
gdi.DeleteObject.argtypes=[W.HANDLE];gdi.DeleteDC.argtypes=[W.HDC]
user.GetClientRect.argtypes=[W.HWND,C.POINTER(W.RECT)]
user.GetWindowThreadProcessId.argtypes=[W.HWND,C.POINTER(W.DWORD)]
user.IsWindowVisible.argtypes=[W.HWND];user.GetDpiForWindow.argtypes=[W.HWND];user.GetDpiForWindow.restype=W.UINT
class BIH(C.Structure):_fields_=[('size',W.DWORD),('width',W.LONG),('height',W.LONG),('planes',W.WORD),('bits',W.WORD),('compression',W.DWORD),('sizeImage',W.DWORD),('x',W.LONG),('y',W.LONG),('used',W.DWORD),('important',W.DWORD)]
class BI(C.Structure):_fields_=[('header',BIH),('colors',W.DWORD*3)]
gdi.GetDIBits.argtypes=[W.HDC,W.HANDLE,W.UINT,W.UINT,C.c_void_p,C.POINTER(BI),W.UINT]
CALLBACK=C.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
def ownedWindow(pid):
    found=[]
    @CALLBACK
    def cb(hwnd,unused):
        actual=W.DWORD();user.GetWindowThreadProcessId(hwnd,C.byref(actual));rect=W.RECT()
        if actual.value==pid and user.IsWindowVisible(hwnd) and user.GetClientRect(hwnd,C.byref(rect)) and rect.right>=1000 and rect.bottom>=600:found.append(hwnd)
        return True
    user.EnumWindows(cb,0)
    if len(found)!=1:raise RuntimeError('Own main window count '+str(len(found)))
    return found[0]
def capture(pid,name,area):
    hwnd=ownedWindow(pid);actual=W.DWORD();user.GetWindowThreadProcessId(hwnd,C.byref(actual));assert actual.value==pid
    rect=W.RECT();assert user.GetClientRect(hwnd,C.byref(rect));w,h=rect.right,rect.bottom
    dc=user.GetDC(hwnd);mem=gdi.CreateCompatibleDC(dc);bitmap=gdi.CreateCompatibleBitmap(dc,w,h);old=gdi.SelectObject(mem,bitmap)
    assert gdi.BitBlt(mem,0,0,w,h,dc,0,0,0x00CC0020)
    bits=C.create_string_buffer(w*h*4);info=BI();info.header=BIH(C.sizeof(BIH),w,-h,1,32,0,w*h*4,0,0,0,0)
    assert gdi.GetDIBits(mem,bitmap,0,h,bits,C.byref(info),0)==h
    image=Image.frombytes('RGB',(w,h),bits.raw,'raw','BGRX')
    image.save(out/'screenshots'/(name+'.png'))
    # The fixture sets logical width=1280; include QT_SCALE_FACTOR as well as
    # the monitor DPI when mapping the inspector to physical client pixels.
    scale=w/1280
    x,y,aw,ah=area;crop=image.crop((round(x*scale),round(y*scale),round((x+aw)*scale),round((y+ah)*scale)))
    crop.save(out/'screenshots'/(name+'-inspector.png'))
    gdi.SelectObject(mem,old);gdi.DeleteObject(bitmap);gdi.DeleteDC(mem);user.ReleaseDC(hwnd,dc)
    return crop
console=logs/(label+'.log');shots={};seen=set();area=None;begin=time.monotonic();cpu=0;processHandle=None
try:
    with console.open('xb') as f:
        p=subprocess.Popen(args,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT)
        processHandle=kernel.OpenProcess(0x1000,False,p.pid)
        while p.poll() is None:
            if time.monotonic()-begin>270:p.kill();raise TimeoutError('Own GUI deadline')
            time.sleep(.08)
            for line in console.read_text(encoding='utf8',errors='replace').splitlines():
                if line in seen:continue
                seen.add(line)
                if 'OBS_RECT' in line:
                    m=re.search(r'OBS_RECT (\{.*\}) ([\d.]+) ([\d.]+)',line);point=json.loads(m[1]);area=(point['x'],point['y'],float(m[2]),float(m[3]))
                if 'OBS_BASE_READY' in line:shots['base']=capture(p.pid,'base',area)
                if 'OBS_SCROLL_READY' in line:
                    m=re.search(r'OBS_SCROLL_READY (\d+) ([\d.]+)',line);shots['scroll-'+m[1]]=capture(p.pid,'scroll-'+m[1],area)
        rc=p.returncode
        if processHandle:
            created,ended,systemCpu,userCpu=W.FILETIME(),W.FILETIME(),W.FILETIME(),W.FILETIME()
            assert kernel.GetProcessTimes(processHandle,C.byref(created),C.byref(ended),C.byref(systemCpu),C.byref(userCpu))
            cpu=((systemCpu.dwHighDateTime<<32)+systemCpu.dwLowDateTime+(userCpu.dwHighDateTime<<32)+userCpu.dwLowDateTime)/10000000
finally:
    main.write_bytes(original)
    if processHandle:kernel.CloseHandle(processHandle)
content=console.read_text(encoding='utf8',errors='replace');engine=Path(env['VEYRA_LOG_FILE']).read_text(encoding='utf8',errors='replace')
result=dict(exit=rc,seconds=time.monotonic()-begin,processCpuSeconds=cpu,args=args,sourceExeSHA=hashlib.sha256(exeSource.read_bytes()).hexdigest(),version=version,case=case,area=area)
if case.startswith('obs'):
    comparisons=[]
    for key in ('scroll-0','scroll-6','scroll-8'):
        if key not in shots:continue
        a,b=shots['base'],shots[key];assert a.size==b.size
        diff=ImageChops.difference(a,b);different=sum(1 for px in diff.getdata() if max(px)>8)
        comparisons.append(dict(frame=key,pixelsDifferentOver8=different,pixels=a.width*a.height,fraction=different/(a.width*a.height)))
        diff.save(out/'screenshots'/(key+'-difference.png'))
    result['comparisons']=comparisons;result['partialUpdatesDisabled']='partial updates disabled' in engine or case=='obs-full'
    passed=rc==0 and 'OBS_DONE' in content and len(shots)>=10 and 'FIELD_UI_FAIL' not in content
    if version!='before':passed=passed and result['partialUpdatesDisabled'] and all(x['fraction']<.005 for x in comparisons)
else:
    files=list((out/'outputs').glob('*.mp4'));result['exports']=[]
    for file in files:
        probe=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-count_frames','-show_streams','-of','json',str(file)],timeout=30))
        video=next(x for x in probe['streams'] if x['codec_type']=='video');result['exports'].append(dict(path=str(file),video=video))
    passed=rc==0 and 'FIELD_EXPORT_PASS' in content and len(files)==4 and all(int(x['video']['nb_read_frames'])==60 for x in result['exports'])
    if version=='before':passed=rc==0 and 'FIELD_UI_FAIL' in content and len(files)==2 and content.count('FIELD_EXPORT_DONE')==2 and '[export-worker] finished' in engine
result['passed']=passed
(logs/(label+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False));print('\n'.join(content.splitlines()[-4:]));raise SystemExit(0 if passed else 1)
