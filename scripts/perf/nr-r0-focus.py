"""Ordinary A/B focus changes using one owned empty GDI window, no GPU load."""
from pathlib import Path
from ctypes import wintypes as W
import ctypes, importlib.util, json, os, re, statistics, subprocess, sys, time

ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py')
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
user=ctypes.WinDLL('user32',use_last_error=True);kernel=ctypes.WinDLL('kernel32')
CALLBACK=ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
user.GetWindowThreadProcessId.argtypes=[W.HWND,ctypes.POINTER(W.DWORD)];user.GetWindowThreadProcessId.restype=W.DWORD
user.IsWindowVisible.argtypes=[W.HWND];user.GetWindowRect.argtypes=[W.HWND,ctypes.POINTER(W.RECT)]
user.GetForegroundWindow.restype=W.HWND;user.SetForegroundWindow.argtypes=[W.HWND];user.SetForegroundWindow.restype=W.BOOL
user.EnumWindows.argtypes=[CALLBACK,W.LPARAM];user.AttachThreadInput.argtypes=[W.DWORD,W.DWORD,W.BOOL]
user.PostMessageW.argtypes=[W.HWND,W.UINT,W.WPARAM,W.LPARAM];user.IsWindow.argtypes=[W.HWND]
user.PeekMessageW.argtypes=[ctypes.POINTER(W.MSG),W.HWND,W.UINT,W.UINT,W.UINT]
kernel.GetCurrentThreadId.restype=W.DWORD

def pid_of(hwnd):
    pid=W.DWORD();user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));return pid.value

def owned_window(pid):
    found=[]
    def enum(hwnd,unused):
        if pid_of(hwnd)==pid and user.IsWindowVisible(hwnd):
            rect=W.RECT();user.GetWindowRect(hwnd,ctypes.byref(rect))
            found.append(((rect.right-rect.left)*(rect.bottom-rect.top),hwnd))
        return True
    user.EnumWindows(CALLBACK(enum),0)
    return max(found)[1] if found else None

def focus(hwnd,expected_pid):
    assert hwnd and pid_of(hwnd)==expected_pid
    message=W.MSG();user.PeekMessageW(ctypes.byref(message),None,0,0,0)
    previous=user.GetForegroundWindow();current=kernel.GetCurrentThreadId()
    foreground_thread=user.GetWindowThreadProcessId(previous,None) if previous else 0
    attached=foreground_thread!=current and foreground_thread!=0 and bool(user.AttachThreadInput(current,foreground_thread,True))
    try:user.SetForegroundWindow(hwnd)
    finally:
        if attached:user.AttachThreadInput(current,foreground_thread,False)
    # Activation can be dispatched to the other UI thread. Verify completion,
    # rather than treating the immediately preceding HWND as a denied request.
    deadline=time.monotonic()+2
    while user.GetForegroundWindow()!=hwnd and time.monotonic()<deadline:
        time.sleep(.02)
    return user.GetForegroundWindow()==hwnd

def run():
    variant,label=sys.argv[1:3];assert label.replace('-','').isalnum();matrix.assert_gpu_tests_idle()
    out=BASE/'logs'/TASK/(label+'-summary');out.mkdir(exist_ok=False)
    tmp=BASE/'tmp'/TASK/label;tmp.mkdir(exist_ok=False)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
    env.update(TEMP=str(tmp),TMP=str(tmp))
    old_foreground=user.GetForegroundWindow();helper=None;rows=[]
    helper_source=ROOT/'scripts/acceptance/playback-smoothness-background.py'
    try:
        for repeat,order in enumerate((('A','B'),('B','A'),('A','B')),1):
            for mode in order:
                events=[];done=set();name=f'{label}-{mode}-r{repeat}'
                with (out/(name+'-helper.log')).open('xb') as helper_log:
                    helper=subprocess.Popen([sys.executable,'-B',str(helper_source)],cwd=tmp,env=env,stdout=helper_log,stderr=subprocess.STDOUT)
                deadline=time.monotonic()+15
                while not owned_window(helper.pid):
                    assert helper.poll() is None and time.monotonic()<deadline,'Owned GDI window unavailable'
                    time.sleep(.1)
                helper_window=owned_window(helper.pid)
                def poll(player,path):
                    if not path.exists():return
                    text=path.read_text(encoding='utf-8',errors='replace')
                    samples=[line.split('NR_PERF_SAMPLE ',1)[1] for line in text.splitlines() if 'NR_PERF_SAMPLE ' in line]
                    if not samples:return
                    sample=json.loads(samples[-1]);position=sample['position']
                    for start,active in ((10,True),(25,False),(40,True)):
                        if position>=start and start not in done:
                            hwnd=owned_window(player.pid) if active else helper_window
                            pid=player.pid if active else helper.pid
                            ok=focus(hwnd,pid)
                            event={'position':position,'activeRequested':active,'targetPid':pid,'foregroundPid':pid_of(user.GetForegroundWindow()),'applied':ok}
                            events.append(event);done.add(start)
                            print('R0_FOCUS_ACTION',name,json.dumps(event),flush=True)
                            assert ok,'Foreground request failed; fixture cannot establish comparison'
                actual='A' if mode=='A' else variant
                receipt=matrix.run(actual,'M1','S4',name,50,stageLabel=label,onPoll=poll)
                log=(BASE/'logs'/TASK/name/'player.log').read_text(encoding='utf-8',errors='replace')
                assert len(events)==3
                samples=[json.loads(line.split('NR_PERF_SAMPLE ',1)[1]) for line in log.splitlines() if 'NR_PERF_SAMPLE ' in line]
                submitted=[]
                for line in log.splitlines():
                    if '[pacing-submit]' in line and 'event=submitted' in line:
                        submitted.append({k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)})
                phases={}
                for phase,lo,hi,active in (('foreground-before',13,23,True),('background',28,38,False),('foreground-after',43,48,True)):
                    ui=[s for s in samples if lo<=s['position']<=hi]
                    assert len(ui)>=3 and all(s['active']==active for s in ui),(phase,'actual Qt focus does not match',ui)
                    intervals=sorted((b['host100ns']-a['host100ns'])/10000 for a,b in zip(submitted,submitted[1:])
                        if lo<=a.get('pts100ns',-1)/1e7<=hi and lo<=b.get('pts100ns',-1)/1e7<=hi)
                    assert len(intervals)>100
                    phases[phase]={'qtActive':active,'uiSamples':len(ui),'presentIntervalCount':len(intervals),
                        'softwarePresentP99Ms':intervals[min(len(intervals)-1,int(len(intervals)*.99))],
                        'softwarePresentMaxMs':max(intervals),'uiSubmitFpsMedian':statistics.median(s['fps'] for s in ui)}
                row={'name':name,'mode':mode,'repeat':repeat,'exeSha256':receipt['exeSha256'],'sourceSha256':receipt['sourceSha256'],
                    'events':events,'phases':phases,'receipt':str(BASE/'logs'/TASK/name/'result.json'),
                    'maxLoggedPreviewSkipped':max([int(x) for x in re.findall(r'previewSkipped=(\d+)',log)],default=0),
                    'gpuCompetition':False,'passed':True}
                rows.append(row);(out/'completed.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
                print('R0_FOCUS_RESULT',name,json.dumps(phases),flush=True)
                user.PostMessageW(helper_window,0x10,0,0);helper.wait(timeout=5);helper=None
        summary={mode:{phase:{'medianPresentP99Ms':statistics.median(r['phases'][phase]['softwarePresentP99Ms'] for r in rows if r['mode']==mode),
            'medianSubmitFps':statistics.median(r['phases'][phase]['uiSubmitFpsMedian'] for r in rows if r['mode']==mode)}
            for phase in ('foreground-before','background','foreground-after')} for mode in ('A','B')}
        (out/'summary.json').write_text(json.dumps({'runs':rows,'summary':summary,'gpuCompetition':False,
            'actualForegroundVerified':True,'passed':True,'claim':'Software submission only; no physical display or unrelated app workload.'},ensure_ascii=False,indent=2),encoding='utf-8')
    finally:
        if helper and helper.poll() is None:
            window=owned_window(helper.pid)
            if window:user.PostMessageW(window,0x10,0,0)
            try:helper.wait(timeout=5)
            except subprocess.TimeoutExpired:helper.terminate();helper.wait(timeout=5)
        restored=False
        if old_foreground and user.IsWindow(old_foreground):restored=bool(user.SetForegroundWindow(old_foreground))
        (out/'cleanup.json').write_text(json.dumps({'ownedHelperStopped':helper is None or helper.poll() is not None,
            'previousForegroundRestoreRequested':True,'restoreCallSucceeded':restored,'userSettingsChanged':False},indent=2),encoding='utf-8')
    matrix.assert_gpu_tests_idle();print('R0_FOCUS_COMPLETE',len(rows),flush=True)

if __name__=='__main__':run()
