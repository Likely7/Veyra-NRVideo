"""Public QML requests with read-only WDDM verification and persistence relaunch."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle()
gdi=ctypes.WinDLL('gdi32');query=gdi.D3DKMTGetProcessSchedulingPriorityClass
query.argtypes=[wintypes.HANDLE,ctypes.POINTER(ctypes.c_int)];query.restype=wintypes.LONG
classes={'normal':2,'high':4,'realtime':5}
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
shutil.copy2(ROOT/'scripts/perf/nr-priority-ui.qml',app/'qml/Veyra/PriorityUiProbe.qml')
main=app/'qml/Veyra/Main.qml';qml=main.read_text(encoding='utf-8');at=qml.rfind('}')
main.write_text(qml[:at]+'\n Loader {source:"PriorityUiProbe.qml";onLoaded:{const r=new XMLHttpRequest();r.open("GET","'+(app/'priority-run.json').as_uri()+'",false);r.send();const c=JSON.parse(r.responseText);item.media=c.media;item.expectedSaved=c.expectedSaved}}\n'+qml[at:],encoding='utf-8')
profile=BASE/'tests'/TASK/label/'profile';profile.mkdir(parents=True,exist_ok=False)
results=[]
for software in (False,True):
    matrix.assert_gpu_tests_idle();name=label+('-software-relaunch' if software else '-hardware')
    out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
    for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
    prefs=json.loads((profile/'qml-preferences.v1.json').read_text(encoding='utf-8')) if software else {}
    prefs.update(overlayCompat='off',obsGameCapture=software)
    (profile/'qml-preferences.v1.json').write_text(json.dumps(prefs),encoding='utf-8')
    (app/'priority-run.json').write_text(json.dumps({'media':str(matrix.SOURCES['M1']).replace('\\','/'),'expectedSaved':'realtime' if software else 'normal'}),encoding='utf-8')
    env=os.environ.copy()
    for key in tuple(env):
        if key.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND')):env.pop(key)
    env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'player.log'),QML_DISABLE_DISK_CACHE='1',
        QML_XHR_ALLOW_FILE_READ='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_VERBOSE_FRAME_LOGS='1')
    command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','110000']
    checks=[];print('START',name,flush=True);start=time.monotonic()
    with (out/'console.log').open('xb') as f:
        proc=subprocess.Popen(command,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT)
        while proc.poll() is None and time.monotonic()-start<120:
            text=(out/'player.log').read_text(encoding='utf-8',errors='replace') if (out/'player.log').exists() else ''
            requests=[json.loads(line.split('PRIORITY_UI_EFFECTIVE ',1)[1]) for line in text.splitlines() if 'PRIORITY_UI_EFFECTIVE ' in line]
            for request in requests[len(checks):]:
                actual=ctypes.c_int(-1);status=query(int(proc._handle),ctypes.byref(actual))
                checks.append(dict(request,queryStatus=hex(status&0xffffffff),actual=actual.value,applied=status==0 and actual.value==classes[request['target']]))
            time.sleep(.1)
        if proc.poll() is None:proc.kill();proc.wait(timeout=8);rc=124
        else:rc=proc.returncode
    text=(out/'player.log').read_text(encoding='utf-8',errors='replace')
    saved=json.loads((profile/'qml-preferences.v1.json').read_text(encoding='utf-8'))
    # The final process may exit before a read-only sample. Its in-process
    # verified Set/Get receipt supplies that final class, separately identified.
    final=[json.loads(line.split('PRIORITY_UI_EFFECTIVE ',1)[1]) for line in text.splitlines() if 'PRIORITY_UI_EFFECTIVE ' in line]
    applied_logs=[line for line in text.splitlines() if '[gpu-priority]' in line and 'state=1' in line and 'ntstatus=0x00000000' in line]
    result={'softwareUi':software,'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,
        'exeSha256':matrix.digest(app/'veyra_qml_ui.exe'),'checks':checks,'requests':final,'appliedLogs':applied_logs,
        'savedPriority':saved.get('gpuPriority'),'resetEvents':text.count('[reset-lifecycle]'),
        'passed':rc==0 and len(final)==20 and len(checks)>=19 and all(x['applied'] for x in checks) and
            saved.get('gpuPriority')=='realtime' and 'PRIORITY_UI_PASS' in text and len(applied_logs)>=18 and
            not any(x in text for x in ('PRIORITY_UI_FAIL','ReferenceError:','TypeError:','[ERROR]','[FATAL]','leaked parameter block')),
        'measurement':'Own process WDDM Get, public preference API, paused/play continuity; no desktop automation or game FPS'}
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
    print('RESULT',name,result['passed'],len(checks),result['resetEvents'],flush=True)
    if not result['passed']:print(text[-4500:],flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
