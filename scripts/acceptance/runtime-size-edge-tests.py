"""Measure actual native-window coverage and composed edge pixels at fractional DPI."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import time
from PIL import ImageGrab

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'runtime-size-20261004'
APP = BASE/'tests'/TASK/'app'
REFERENCE = BASE/'test-packages/runtime-size-20261004/Veyra-2.0.2-slim-20261004-win64-portable'
BUILD = BASE/'build'/TASK
label = sys.argv[1]
phase = sys.argv[2] if len(sys.argv)>2 else 'before'
out, logs, tmp = (BASE/k/TASK/label for k in ('tests','logs','tmp'))
for p in (out,logs,tmp): p.mkdir(parents=True,exist_ok=False)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/runtime-size-control.py')],check=True)
if not APP.exists():
    APP.mkdir()
    for path in REFERENCE.rglob('*'):
        if not path.is_file():continue
        rel=path.relative_to(REFERENCE); dst=APP/rel
        dst.parent.mkdir(parents=True,exist_ok=True)
        if path.suffix.lower() in {'.exe','.qml','.js','.json','.log'} or rel.parts[0] in {'qml','shaders'}:
            shutil.copy2(path,dst)
        else:os.link(path,dst)
shutil.copy2((REFERENCE if phase=='before' else BUILD)/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
media = BASE/'tests'/TASK/'edge-cyan.mp4'
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
if not media.exists():
    with (logs/'fixture.log').open('xb') as log:
        subprocess.run(['ffmpeg','-v','warning','-n','-f','lavfi','-i','color=c=cyan:size=1280x720:rate=30','-t','8','-c:v','libx264','-pix_fmt','yuv420p','-crf','12',str(media)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45,check=True)
user32=ctypes.WinDLL('user32',use_last_error=True)
callback_type=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
user32.EnumWindows.argtypes=[callback_type,wintypes.LPARAM]
user32.EnumChildWindows.argtypes=[wintypes.HWND,callback_type,wintypes.LPARAM]
user32.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
user32.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
user32.ClientToScreen.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.POINT)]
user32.GetClassNameW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
user32.SetForegroundWindow.argtypes=[wintypes.HWND]
user32.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT]
user32.WindowFromPoint.argtypes=[wintypes.POINT]
user32.WindowFromPoint.restype=wintypes.HWND
user32.SetProcessDpiAwarenessContext.argtypes=[wintypes.HANDLE]
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
def windows(pid,parent=None):
    found=[]
    @callback_type
    def collect(hwnd,_):
        actual=wintypes.DWORD();user32.GetWindowThreadProcessId(hwnd,ctypes.byref(actual))
        cls=ctypes.create_unicode_buffer(256);user32.GetClassNameW(hwnd,cls,256)
        if actual.value==pid:found.append((hwnd,cls.value))
        return True
    if parent:user32.EnumChildWindows(parent,collect,0)
    else:user32.EnumWindows(collect,0)
    return found
def rect(hwnd):
    client=wintypes.RECT(); assert user32.GetClientRect(hwnd,ctypes.byref(client))
    origin=wintypes.POINT(); assert user32.ClientToScreen(hwnd,ctypes.byref(origin))
    return (origin.x,origin.y,client.right,client.bottom)

cases=[(1,642,'gpu'),(1.25,642,'gpu'),(1.5,643,'gpu'),(1.75,641,'gpu'),(2,643,'gpu'),(1.25,642,'software')]
behaviors=[]
if len(sys.argv)>3 and sys.argv[3]=='full':
    cases=[(scale,width,mode) for mode in ('gpu','software') for scale in (1,1.25,1.5,1.75,2) for width in (641,642,643)]
if len(sys.argv)>3 and sys.argv[3]=='behaviors':
    behaviors=['resize','page-cycle','menu','native-pixels','fullscreen','pro']*2
    cases=[(2 if behavior=='native-pixels' else 1.25,643 if behavior=='native-pixels' else 642,mode)
           for mode in ('gpu','software') for behavior in behaviors[:6]]
if len(sys.argv)>3 and sys.argv[3]=='page-cycle':
    behaviors=['page-cycle','page-cycle']
    cases=[(1.25,642,mode) for mode in ('gpu','software')]
results=[]
main_path=APP/'qml/Veyra/Main.qml'
main_source=(ROOT/'qml/Veyra/Main.qml').read_bytes()
for index,(scale,width,mode) in enumerate(cases):
    behavior=behaviors[index] if behaviors else 'fit'
    name=f'{index}-{mode}-dpr{scale}-w{width}-{behavior}'
    child=env.copy()
    for key in ('QT_QPA_PLATFORM','QT_QUICK_BACKEND','VEYRA_UI_RHI','QT_SCALE_FACTOR','VEYRA_VFG_RUNTIME'):child.pop(key,None)
    child.update(QT_SCALE_FACTOR=str(scale),VEYRA_LOG_FILE=str(logs/(name+'-engine.log')))
    if mode=='gpu':child['VEYRA_UI_RHI']='d3d12'
    # The C++ --size override runs after QML startup and can overwrite the
    # cinema height. Drive the real QML fit after the source has opened instead.
    qml_text=main_source.decode('utf-8'); at=qml_text.rfind('}')
    fixture='\nTimer { interval: 1300; running: true; onTriggered: { root.page="min"; root.widthBeforeNarrow=0; root.width='+str(width)+'; root.fitToFilm(16/9); root.height=Math.round(root.width/(16/9)); root.x=Math.max(0, root.screenArea.x+40); root.y=Math.max(0, root.screenArea.y+40); veyra.logUi("edge-test", "settled="+root.width+"x"+root.height) } }\n'
    def timer(interval,statement):return '\nTimer { interval: '+str(interval)+'; running: true; onTriggered: { '+statement+' } }\n'
    if behavior=='resize':
        fixture+=timer(1700,'root.width=641;')+timer(2100,'root.width=643;')+timer(2500,'root.width=642;')
    elif behavior=='page-cycle':
        fixture+=timer(1700,'root.page="pro";')+timer(2300,'root.page="min"; Qt.callLater(function() { root.widthBeforeNarrow=0; root.width=642; root.fitToFilm(16/9); root.height=Math.round(root.width/(16/9)); });')
    elif behavior=='menu':fixture+=timer(1800,'minPage.openSourceMenuForTest(); veyra.logUi("edge-test", "source-menu="+minPage.sourceMenuOpen);')
    elif behavior=='native-pixels':fixture+=timer(1800,'veyra.aspectMode=1;')
    elif behavior=='fullscreen':fixture+=timer(1800,'root.toggleFullscreen(); veyra.aspectMode=5;')
    elif behavior=='pro':fixture+=timer(1800,'root.page="pro"; root.height=700;')
    main_path.write_text(qml_text[:at]+fixture+qml_text[at:],encoding='utf-8')
    args=[str(APP/'veyra_qml_ui.exe'),'--page','min','--reduced-motion','--data-dir',str(out/(name+'-profile')),'--exit-after','7000' if behaviors else '5000',str(media)]
    if mode=='software':args.append('--obs-game-capture')
    with (logs/(name+'.log')).open('xb') as log:
        proc=subprocess.Popen(args,cwd=APP,env=child,stdout=log,stderr=subprocess.STDOUT)
        deadline=time.monotonic()+3
        root=video=None
        while time.monotonic()<deadline:
            for hwnd,cls in windows(proc.pid):
                if cls.startswith('Qt') and not cls.endswith('ToolSaveBits'):
                    children=windows(proc.pid,hwnd)
                    candidate=next((h for h,c in children if c=='VeyraQmlVideoHost'),None)
                    if candidate:root,video=hwnd,candidate;break
            if video:break
            time.sleep(.08)
        # Raise only our transient test HWND, without activating another app.
        # Foreground-lock restrictions otherwise let unrelated user windows
        # occlude its pixels. Never capture an occluded client.
        if root:assert user32.SetWindowPos(root,ctypes.c_void_p(-1),0,0,0,0,0x13)
        time.sleep(4.2 if behaviors else 2.0)
        assert root and video, name+' missing native windows'
        root_rect,video_rect=rect(root),rect(video)
        x,y,w,h=video_rect if behavior=='pro' else root_rect
        for px,py in ((x+4,y+4),(x+w-5,y+4),(x+4,y+h-5),(x+w-5,y+h-5),(x+w//2,y+h//2)):
            owner=wintypes.DWORD()
            user32.GetWindowThreadProcessId(user32.WindowFromPoint(wintypes.POINT(px,py)),ctypes.byref(owner))
            assert owner.value==proc.pid,name+' test client is occluded; capture skipped'
        # Only the test process's own client, never the user's full desktop.
        image=ImageGrab.grab(bbox=(x,y,x+w,y+h),all_screens=True).convert('RGB')
        image.save(out/(name+'.png'))
        # The cinema pill and restart dialog may cover the middle/lower picture.
        # Sample a vertical run above them, away from rounded top corners.
        # Central rows avoid the top dock. All sample columns are in the test
        # HWND; a native picture at the centre proves the capture is meaningful.
        rows=range(max(96,h//3),min(h-80,h//3+96))
        edges={side: [int(statistics.median(image.getpixel((col,row))[channel] for row in rows)) for channel in range(3)] for side,col in [('left',0),('right',w-1),('rightInner',w-3),('center',w//2)]}
        edges['top']=list(image.getpixel((w//2,3)))
        edges['bottom']=list(image.getpixel((w//2,h-4)))
        code=proc.wait(timeout=12)
    main_path.write_bytes(main_source)
    item={'case':name,'scaleFactor':scale,'mode':mode,'behavior':behavior,'rootClient':root_rect,'videoClient':video_rect,'rightGap':root_rect[0]+root_rect[2]-video_rect[0]-video_rect[2],'bottomGap':root_rect[1]+root_rect[3]-video_rect[1]-video_rect[3],'edges':edges,'exit':code}
    results.append(item)
    (logs/'summary.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print(json.dumps(item),flush=True)
    assert code==0,name+' abnormal exit'
    picture=edges['rightInner'] if behavior=='menu' else edges['center']
    # In pro, contain places the image in the vertical centre of the viewport.
    if behavior=='pro':picture=list(image.getpixel((w//2,h//2)))
    assert picture[1]>120 and picture[2]>120,name+' native picture was not captured'
    if phase!='before':
        if behavior=='page-cycle':assert root_rect[2:]==(803,451),name+' cinema transition not settled'
        if behavior!='pro':assert item['rightGap']==0 and item['bottomGap']==0,name+' uncovered client edge'
        if behavior in ('native-pixels','fullscreen'):
            assert max(edges['right'])<20,name+' intentional side bar lost'
        elif behavior=='pro':assert max(edges['top'])<20 and max(edges['bottom'])<20,name+' intentional letterbox lost'
        else:assert edges['right'][1]>120 and edges['right'][2]>120,name+' black content at client edge'
print('MINIMAL EDGE MEASUREMENTS COMPLETE',phase,len(results),flush=True)
