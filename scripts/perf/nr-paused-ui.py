"""Real Qt pause/30s idle/residual/model/resume/seek, isolated loader and profiles."""
from pathlib import Path
import importlib.util
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
import psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
gpu_spec=importlib.util.spec_from_file_location('gpu_meter',ROOT/'scripts/perf/gpu-process-meter.py');gpu_module=importlib.util.module_from_spec(gpu_spec);gpu_spec.loader.exec_module(gpu_module)
variant,label=sys.argv[1:3];groups=sys.argv[3:] or ['nr','srnr','temporal']
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant.removesuffix('-off')/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
shutil.copy2(ROOT/'scripts/perf/nr-paused-ui.qml',app/'qml/Veyra/NrPausedUiProbe.qml')
main=app/'qml/Veyra/Main.qml';text=main.read_text(encoding='utf-8');at=text.rfind('}')
loader='\n Loader {source:"NrPausedUiProbe.qml";onLoaded:{const r=new XMLHttpRequest();r.open("GET","'+(app/'paused-run.json').as_uri()+'",false);r.send();const c=JSON.parse(r.responseText);item.media=c.media;item.group=c.group}}\n'
main.write_text(text[:at]+loader+text[at:],encoding='utf-8');results=[]
for group in groups:
    matrix.assert_gpu_tests_idle()
    assert group in ('nr','srnr','temporal')
    name=label+'-'+group;out=BASE/'logs'/TASK/name;profile=BASE/'tests'/TASK/name/'profile';tmp=BASE/'tmp'/TASK/name
    for p in (out,profile,tmp):p.mkdir(parents=True,exist_ok=False)
    (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','language':'zh_CN'}),encoding='utf-8')
    (app/'paused-run.json').write_text(json.dumps({'media':str(matrix.SOURCES['M1']).replace('\\','/'),'group':group}),encoding='utf-8')
    env=os.environ.copy()
    for key in tuple(env):
        if key.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND')):env.pop(key)
    env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'player.log'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',QML_XHR_ALLOW_FILE_READ='1')
    if variant.endswith('-off'):env['VEYRA_TEST_DISABLE_PAUSED_NR_RESIDUAL_REUSE']='1'
    command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','270000']
    print('START',name,flush=True);before=matrix.gpu_query();start=time.monotonic();samples=[]
    with (out/'console.log').open('xb') as log:
        proc=subprocess.Popen(command,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT);meter=psutil.Process(proc.pid);meter.cpu_percent(None);gpu_meter=gpu_module.GpuProcessMeter(proc.pid)
        try:
            while proc.poll() is None and time.monotonic()-start<280:
                samples.append({'elapsed':time.monotonic()-start,'wallUtc':time.time(),'cpuPercent':meter.cpu_percent(None),'gpu':matrix.gpu_query(),'processGpu':gpu_meter.sample()});time.sleep(1)
            if proc.poll() is None:proc.kill();proc.wait(timeout=8);rc=124
            else:rc=proc.returncode
        finally:
            if proc.poll() is None:proc.kill();proc.wait(timeout=8)
            gpu_meter.close()
    text=(out/'player.log').read_text(encoding='utf-8',errors='replace')
    events={event:[json.loads(line.split(event+' ',1)[1]) for line in text.splitlines() if event+' ' in line] for event in ('PAUSED_UI_IDLE_BEGIN','PAUSED_UI_IDLE_PASS','PAUSED_UI_EDITS_PASS','PAUSED_UI_RESUME_PASS','PAUSED_UI_SEEK_PASS')}
    result={'variant':variant,'group':group,'exeSha256':matrix.digest(app/'veyra_qml_ui.exe'),'testQmlSha256':matrix.digest(app/'qml/Veyra/NrPausedUiProbe.qml'),
            'sourceSha256':matrix.digest(matrix.SOURCES['M1']),'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,'beforeGpu':before,'afterGpu':matrix.gpu_query(),
            'events':events,'reusedNr':text.count('[paused-nr-cache] event=reuse'),
            'passed':rc==0 and 'PAUSED_UI_PASS' in text and all(events.values()) and not any(s in text for s in ('PAUSED_UI_FAIL','[ERROR]','[FATAL]','leaked parameter block','ReferenceError:','TypeError:')),
            'measurement':'Real product NR counters and media position; PDH is owned PID engine utilization, nvidia-smi is whole-card utilization; neither is scanout'}
    idle_start=events['PAUSED_UI_IDLE_BEGIN'][0]['at']/1000 if events['PAUSED_UI_IDLE_BEGIN'] else 0
    idle=[s['processGpu'] for s in samples if idle_start+2<s['wallUtc']<idle_start+27 and s['processGpu']['measured']]
    result['idleProcessGpu']={'sampleCount':len(idle),'maxEnginePercent':{'median':statistics.median(s['maxEnginePercent'] for s in idle),'max':max(s['maxEnginePercent'] for s in idle)}} if idle else {'sampleCount':0,'unavailable':True}
    (out/'meters.json').write_text(json.dumps(samples,ensure_ascii=False,indent=2),encoding='utf-8');(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
    print('RESULT',name,rc,result['passed'],result['reusedNr'],events,flush=True)
    if not result['passed']:print(text[-6000:],flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
