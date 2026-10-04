"""Installed RTSS Profile API/real OSD/native video parameter test. Profiles restored."""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

import psutil
from PIL import Image, ImageGrab

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra'); TASK='rtss-restart-loop-20261004'
RUN=BASE/'tests'/TASK/sys.argv[1]; LOGS=BASE/'logs'/TASK/sys.argv[1]; TMP=BASE/'tmp'/TASK/sys.argv[1]
for path in (RUN,LOGS,TMP):path.mkdir(parents=True,exist_ok=False)
APP=BASE/'test-packages'/TASK/'Veyra-2.0.3-rtssfix-NVIDIA-win64-portable'
RTSS=Path('E:/App/RivaTuner Statistics Server')
PROFILE=b'veyra_qml_ui.exe'
transport_only=len(sys.argv)>2 and sys.argv[2]=='--transport-only'
expected_cases=1 if transport_only else 11
started=time.time(); owned=[]; seen={}; cleanup=[]; results=[]
before={p.name:p.read_bytes() for p in (RTSS/'Profiles').iterdir() if p.is_file()}
(RUN/'profiles-before').mkdir()
for name,data in before.items():(RUN/'profiles-before'/name).write_bytes(data)

def helpers():
    roots={p.pid for p in owned};hits=[]
    for p in psutil.process_iter(['pid','ppid','name','exe','create_time']):
        x=p.info
        if (x['exe'] and Path(x['exe']).parent==RTSS and x['name'].lower().startswith('rtss')
            and x['create_time']>=started-1 and (x['pid'] in roots or x['ppid'] in roots or seen.get(x['pid'])==x['create_time'])):
            hits.append(p);seen[x['pid']]=x['create_time']
    return hits
def stop():
    for _ in range(3):
        found=helpers()
        if not found:break
        for p in sorted(found,key=lambda p:p.name().lower()!='rtss.exe'):
            try:cleanup.append({'pid':p.pid,'name':p.name(),'path':p.exe()});p.terminate()
            except psutil.NoSuchProcess:pass
        psutil.wait_procs(found,timeout=3)
    assert not helpers()

assert not any(p.info['name'].lower()=='rtss.exe' for p in psutil.process_iter(['name'])),'RTSS was already active'
dll=ctypes.WinDLL(str(RTSS/'RTSSHooks64.dll'),winmode=0x1100)
for name in ('LoadProfile','SaveProfile','DeleteProfile'):
    getattr(dll,name).argtypes=[ctypes.c_char_p];getattr(dll,name).restype=None
for name in ('GetProfileProperty','SetProfileProperty'):
    getattr(dll,name).argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_uint32];getattr(dll,name).restype=ctypes.c_int
dll.UpdateProfiles.argtypes=[];dll.UpdateProfiles.restype=None
def set_profile(values):
    dll.LoadProfile(PROFILE)
    actual={}
    for key,value in values.items():
        number=ctypes.c_uint32(value)
        assert dll.SetProfileProperty(key.encode(),ctypes.byref(number),4),(key,'SetProfileProperty rejected')
    dll.SaveProfile(PROFILE);dll.LoadProfile(PROFILE)
    for key,value in values.items():
        number=ctypes.c_uint32()
        assert dll.GetProfileProperty(key.encode(),ctypes.byref(number),4) and number.value==value,(key,value,number.value)
        actual[key]=number.value
    dll.UpdateProfiles();return actual

k=ctypes.WinDLL('kernel32');u=ctypes.WinDLL('user32')
k.OpenFileMappingW.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_wchar_p];k.OpenFileMappingW.restype=ctypes.c_void_p
k.MapViewOfFile.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_size_t];k.MapViewOfFile.restype=ctypes.c_void_p
k.UnmapViewOfFile.argtypes=[ctypes.c_void_p];k.CloseHandle.argtypes=[ctypes.c_void_p]
CALLBACK=ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_void_p)
u.EnumWindows.argtypes=[CALLBACK,ctypes.c_void_p];u.EnumChildWindows.argtypes=[ctypes.c_void_p,CALLBACK,ctypes.c_void_p]
u.GetWindowThreadProcessId.argtypes=[ctypes.c_void_p,ctypes.POINTER(wintypes.DWORD)]
u.GetClassNameW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p,ctypes.c_int]
u.GetClientRect.argtypes=[ctypes.c_void_p,ctypes.POINTER(wintypes.RECT)]
u.ClientToScreen.argtypes=[ctypes.c_void_p,ctypes.POINTER(wintypes.POINT)]
u.WindowFromPoint.argtypes=[wintypes.POINT];u.WindowFromPoint.restype=ctypes.c_void_p
u.SetWindowPos.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_uint]
def windows(pid,parent=None):
    found=[]
    @CALLBACK
    def callback(hwnd,_):
        owner=wintypes.DWORD();u.GetWindowThreadProcessId(hwnd,ctypes.byref(owner));name=ctypes.create_unicode_buffer(256)
        u.GetClassNameW(hwnd,name,256)
        if owner.value==pid:found.append((hwnd,name.value))
        return True
    if parent:u.EnumChildWindows(parent,callback,None)
    else:u.EnumWindows(callback,None)
    return found
def native_capture(pid,path):
    for root,cls in windows(pid):
        if not cls.startswith('Qt'):continue
        video=next((h for h,c in windows(pid,root) if c=='VeyraQmlVideoHost'),None)
        if not video:continue
        u.SetWindowPos(root,ctypes.c_void_p(-1),0,0,0,0,0x13)
        rect=wintypes.RECT();point=wintypes.POINT(0,0)
        assert u.GetClientRect(video,ctypes.byref(rect)) and u.ClientToScreen(video,ctypes.byref(point))
        w,h=rect.right,rect.bottom
        for x,y in ((8,8),(w-9,8),(8,h-9),(w-9,h-9),(w//2,h//2)):
            owner=wintypes.DWORD();u.GetWindowThreadProcessId(u.WindowFromPoint(wintypes.POINT(point.x+x,point.y+y)),ctypes.byref(owner))
            assert owner.value==pid,'Owned native client is occluded; no unrelated window captured'
        image=ImageGrab.grab(bbox=(point.x,point.y,point.x+w,point.y+h),all_screens=True).convert('RGB');image.save(path)
        assert max(image.resize((32,32)).getextrema()[0])>30,'Black native video capture'
        return {'rect':[point.x,point.y,w,h],'file':str(path)}
    raise AssertionError('No native video HWND')

# Use Windows atomics and SDK-derived offsets, not hand-computed struct layouts.
atomic=TMP/'atomic.cpp'
atomic.write_text('''#include <windows.h>
#include <intrin.h>
#include <cstddef>
#include "RTSSSharedMemory.h"
extern "C" __declspec(dllexport) long try_lock(volatile long* p){return _InterlockedCompareExchange(p,1,0);}
extern "C" __declspec(dllexport) void unlock(volatile long* p){_InterlockedExchange(p,0);}
extern "C" __declspec(dllexport) unsigned long app_osd_offset(){return offsetof(RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY,dwOSDX);}
extern "C" __declspec(dllexport) unsigned long app_frames_offset(){return offsetof(RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY,dwTime0);}
extern "C" __declspec(dllexport) unsigned long busy_offset(){return offsetof(RTSS_SHARED_MEMORY,dwBusy);}
''',encoding='ascii')
cmd=TMP/'atomic.cmd'
cmd.write_text('@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\ncl /nologo /LD /O2 /MD /I"%TASK_RTSS%\\SDK\\Include" /Fe:"%TASK_TMP%\\atomic.dll" /Fo:"%TASK_TMP%\\atomic.obj" "%TASK_TMP%\\atomic.cpp"\n',encoding='ascii')
env=os.environ.copy();env.update(TASK_TMP=str(TMP),TASK_RTSS=str(RTSS),TEMP=str(TMP),TMP=str(TMP))
with (LOGS/'atomic-build.log').open('xb') as log:subprocess.run(['cmd.exe','/d','/c',str(cmd)],cwd=TMP,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60,check=True)
atom=ctypes.WinDLL(str(TMP/'atomic.dll'));atom.try_lock.argtypes=[ctypes.POINTER(ctypes.c_long)];atom.try_lock.restype=ctypes.c_long;atom.unlock.argtypes=[ctypes.POINTER(ctypes.c_long)]
for name in ('app_osd_offset','app_frames_offset','busy_offset'):
    getattr(atom,name).argtypes=[];getattr(atom,name).restype=ctypes.c_uint32
offsets={name:getattr(atom,name)() for name in ('app_osd_offset','app_frames_offset','busy_offset')}
(LOGS/'sdk-offsets.json').write_text(json.dumps(offsets,indent=2),encoding='utf8')
def osd(view,text,pid,release=False):
    head=(ctypes.c_uint32*9).from_address(view)
    assert head[0]==0x52545353 and head[1]>=0x2000e and 4608<=head[5]<=400000 and 2<=head[7]<=128
    busy=ctypes.cast(view+offsets['busy_offset'],ctypes.POINTER(ctypes.c_long))
    if atom.try_lock(busy)!=0:return None
    owner=b'VeyraRtssAcceptance20261004';entry=None
    try:
        for n in range(1,head[7]):
            at=view+head[6]+n*head[5];name=ctypes.string_at(at+256,256).split(b'\0',1)[0]
            if name==owner or (not name and not release):entry=at;break
        if entry:
            if release:ctypes.memset(entry,0,head[5])
            else:
                ctypes.memmove(entry+256,owner+b'\0',len(owner)+1)
                encoded=text.encode('ascii');ctypes.memset(entry+512,0,4096);ctypes.memmove(entry+512,encoded,len(encoded))
            head[8]+=1
        entries=[]
        for n in range(head[4]):
            at=view+head[3]+n*head[2]
            if ctypes.c_uint32.from_address(at).value==pid:
                fields=(ctypes.c_uint32*5).from_address(at+offsets['app_osd_offset'])
                counters=(ctypes.c_uint32*3).from_address(at+offsets['app_frames_offset'])
                entries.append({'pid':pid,'osdX':fields[0],'osdY':fields[1],'zoom':fields[2],'color':fields[3],'osdFrame':fields[4],
                                'frames':counters[2],'rtssPresentFps':1000*counters[2]/(counters[1]-counters[0]) if counters[1]>counters[0]>0 else None,'globalOsdFrame':head[8]})
        return entries
    finally:atom.unlock(busy)

media=BASE/'tests'/TASK/'rtss-stress-2k30.mp4'
if not media.exists():
    with (LOGS/'fixture.log').open('xb') as log:
        subprocess.run(['ffmpeg','-v','warning','-n','-f','lavfi','-i','testsrc2=size=2560x1440:rate=30','-t','150','-c:v','libx264','-preset','ultrafast','-crf','28','-pix_fmt','yuv420p','-threads','4',str(media)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120,check=True)
main=APP/'qml/Veyra/Main.qml';original=main.read_bytes();view=None;handle=None
profiles=[
    ('low-osd',{'AppDetectionLevel':1,'EnableOSD':1,'ZoomRatio':2,'PositionX':16,'PositionY':16,'EnableBgnd':1,'EnableStat':1,'BaseColor':0x00ff8000,'FramerateLimit':0}),
    ('high-large-osd',{'AppDetectionLevel':3,'EnableOSD':1,'ZoomRatio':3,'PositionX':24,'PositionY':24,'EnableBgnd':0,'EnableStat':1,'BaseColor':0x0000ff00,'FramerateLimit':0}),
    ('medium-osd-off-cap60',{'AppDetectionLevel':2,'EnableOSD':0,'ZoomRatio':1,'PositionX':48,'PositionY':32,'EnableBgnd':1,'EnableStat':0,'BaseColor':0x00ff8000,'FramerateLimit':60}),
]
if transport_only:profiles=profiles[:1]
try:
    shutil.copy2(BASE/'build'/TASK/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
    for name,values in profiles:
        stop();actual=set_profile(values)
        out=RUN/name;logs=LOGS/name;tmp=TMP/name
        for p in (out,logs,tmp):p.mkdir()
        (logs/'rtss-profile.json').write_text(json.dumps(actual,indent=2),encoding='utf8')
        si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
        server_env=os.environ.copy();server_env.update(__COMPAT_LAYER='RunAsInvoker',TEMP=str(tmp),TMP=str(tmp))
        server=subprocess.Popen([str(RTSS/'RTSS.exe')],cwd=RTSS,env=server_env,startupinfo=si,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);owned.append(server)
        deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            handle=k.OpenFileMappingW(0xf001f,False,'RTSSSharedMemoryV2')
            if handle:
                view=k.MapViewOfFile(handle,0xf001f,0,0,0)
                if view and ctypes.c_uint32.from_address(view).value==0x52545353:break
                if view:k.UnmapViewOfFile(view);view=None
                k.CloseHandle(handle);handle=None
            time.sleep(.1)
        assert view,'Installed RTSS did not create shared memory'
        time.sleep(1.5)
        source=original.decode('utf8');at=source.rfind('}')
        loader='\nLoader { source: '+json.dumps((ROOT/'scripts/acceptance/rtss-restart-stress.qml').as_uri())+'; onLoaded: { item.media='+json.dumps(str(media))+';item.evidence='+json.dumps(str(out))+' } }\n'
        if transport_only:loader=loader.replace('item.media=','item.cases=[item.cases[10]];item.media=',1)
        main.write_text(source[:at]+loader+source[at:],encoding='utf8')
        profile=out/'profile';profile.mkdir();(profile/'qml-preferences.v1.json').write_text('{"language":"zh-CN","overlayCompat":"auto"}',encoding='utf8')
        app_env={key:value for key,value in os.environ.items() if key.upper() not in ('QT_QUICK_BACKEND','QT_QPA_PLATFORM','VEYRA_UI_RHI','VEYRA_TEST_IGNORE_RTSS','RTSSHOOKSPROFILEOVERRIDE')}
        app_env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logs/'app.log'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
        snapshots=[];captured=set();deadline=time.monotonic()+255
        with (logs/'console.log').open('xb') as console:
            process=subprocess.Popen([str(APP/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--exit-after','240000','--reduced-motion'],cwd=APP,env=app_env,stdout=console,stderr=subprocess.STDOUT);owned.append(process)
            while process.poll() is None and time.monotonic()<deadline:
                data=osd(view,'VEYRA RTSS '+name+'\nREAL OSD / NR SR FG TEST',process.pid)
                if data:snapshots.append(data[-1])
                for shot in out.glob('*-ui.png'):
                    if shot.name in captured:continue
                    # The PNG path appears before Qt finishes writing its IDAT.
                    # Retry the same shot on the next tick; don't skip evidence.
                    try:
                        with Image.open(shot) as source:ui=source.convert('RGBA')
                    except OSError:
                        continue
                    assert ui.getpixel((12,80))[3]==255
                    info=native_capture(process.pid,out/(shot.stem.replace('-ui','-native')+'.png'))
                    captured.add(shot.name)
                    info['case']=shot.stem; (logs/(shot.stem+'.json')).write_text(json.dumps(info,indent=2),encoding='utf8')
                time.sleep(.15)
            code=process.wait(timeout=5)
        text=(logs/'app.log').read_text(encoding='utf8',errors='replace')
        (logs/'native-stats.json').write_text(json.dumps(snapshots,indent=2),encoding='utf8')
        assert code==0 and f'RTSS_STRESS_PASS {expected_cases}' in text and 'RTSS_STRESS_FAIL' not in text,(name,code,text[-6000:])
        assert '[ERROR]' not in text and '[FATAL]' not in text,'Product/QML error; inspect app.log'
        assert str(RTSS/'RTSSHooks64.dll').replace('/','\\') in text,'Hook module did not come from the installed directory'
        assert len(captured)==expected_cases,(name,captured)
        if values['EnableOSD']:
            assert any(x['osdFrame']>0 for x in snapshots),'OSD was not consumed by the native renderer'
            assert any(x['zoom']==values['ZoomRatio'] for x in snapshots),'Actual OSD zoom does not match profile'
            assert any(x['osdX']==values['PositionX'] and x['osdY']==values['PositionY'] and x['color']==values['BaseColor'] for x in snapshots),'Native OSD position/color do not match profile'
        cases=[json.loads(line.split('RTSS_STRESS_CASE ',1)[1]) for line in text.splitlines() if 'RTSS_STRESS_CASE ' in line]
        assert len(cases)==expected_cases
        results.append({'rtssProfile':actual,'cases':cases,'screenshots':len(captured),'rtssNativeSnapshots':snapshots[::20],'exit':code})
        (logs/'native-stats.json').write_text(json.dumps(snapshots,indent=2),encoding='utf8')
        osd(view,'',process.pid,True);k.UnmapViewOfFile(view);view=None;k.CloseHandle(handle);handle=None
        stop();print('INSTALLED RTSS PARAMETER PASS',name,len(cases),flush=True)
finally:
    main.write_bytes(original)
    if view:
        osd(view,'',0,True);k.UnmapViewOfFile(view)
    if handle:k.CloseHandle(handle)
    for p in owned:
        if p.poll() is None and p.args[0]==str(APP/'veyra_qml_ui.exe'):p.terminate();p.wait(timeout=5)
    stop()
    dll.DeleteProfile(PROFILE)
    for name,data in before.items():(RTSS/'Profiles'/name).write_bytes(data)
    restored={p.name:p.read_bytes() for p in (RTSS/'Profiles').iterdir() if p.is_file()}
    assert restored==before,'Installed RTSS profiles were not restored byte for byte'
    (LOGS/'cleanup.json').write_text(json.dumps({'helpersStopped':cleanup,'remainingOwnedHelpers':len(helpers()),'profilesRestored':True,'profileSHA256':{n:hashlib.sha256(v).hexdigest() for n,v in restored.items()}},indent=2),encoding='utf8')
(LOGS/'summary.json').write_text(json.dumps({'rtssExecutable':str(RTSS/'RTSS.exe'),'rtssSHA256':hashlib.sha256((RTSS/'RTSS.exe').read_bytes()).hexdigest(),'results':results},indent=2),encoding='utf8')
print('INSTALLED RTSS REAL OSD PASS',len(results),LOGS)
