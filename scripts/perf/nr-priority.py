"""Controlled, intentionally concurrent GPU competition; only owned processes."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import csv
import importlib.util
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
gdi=ctypes.WinDLL('gdi32',use_last_error=True)
get=gdi.D3DKMTGetProcessSchedulingPriorityClass;get.argtypes=[wintypes.HANDLE,ctypes.POINTER(ctypes.c_int)];get.restype=wintypes.LONG
set_priority=gdi.D3DKMTSetProcessSchedulingPriorityClass;set_priority.argtypes=[wintypes.HANDLE,ctypes.c_int];set_priority.restype=wintypes.LONG
def set_owned(proc,priority):
    before=ctypes.c_int(-1);after=ctypes.c_int(-1)
    a=get(int(proc._handle),ctypes.byref(before));b=set_priority(int(proc._handle),priority);c=get(int(proc._handle),ctypes.byref(after))
    return {'pid':proc.pid,'beforeStatus':hex(a&0xffffffff),'beforeClass':before.value,'setStatus':hex(b&0xffffffff),'afterStatus':hex(c&0xffffffff),'afterClass':after.value,'applied':a==b==c==0 and after.value==priority}
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle()
app=BASE/'tests'/TASK/(label+'-load-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_nr_gpu_competition_load.exe',app/'veyra_nr_gpu_competition_load.exe')
matrix.stage(variant);results=[]
# Counterbalanced order avoids using an increasing GPU temperature as a benefit.
orders=[('normal','high','realtime'),('realtime','normal','high'),('high','realtime','normal')]
classes={'normal':2,'high':4,'realtime':5}
for repeat,order in enumerate(orders,1):
    for name in order:
        matrix.assert_gpu_tests_idle();run_label=f'{label}-{name}-r{repeat}'
        out=BASE/'logs'/TASK/(run_label+'-load');tmp=BASE/'tmp'/TASK/(run_label+'-load')
        out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
        env=os.environ.copy()
        for key in tuple(env):
            if key.upper().startswith('VEYRA_'):env.pop(key)
        env.update(TEMP=str(tmp),TMP=str(tmp))
        command=[str(app/'veyra_nr_gpu_competition_load.exe'),str(matrix.SOURCES['M1']),str(out),'100']
        print('LOAD_START',run_label,flush=True);started=time.monotonic()
        with (out/'console.log').open('xb') as console:
            load=subprocess.Popen(command,cwd=app,env=env,stdout=console,stderr=subprocess.STDOUT)
            priority=None
            try:
                while time.monotonic()-started<40 and load.poll() is None:
                    if 'LOAD_READY' in (out/'console.log').read_text(encoding='utf-8',errors='replace'):break
                    time.sleep(.25)
                assert load.poll() is None and 'LOAD_READY' in (out/'console.log').read_text(encoding='utf-8',errors='replace'),'competition load did not start'
                priority=set_owned(load,2)
                assert priority['applied'], 'Owned GPU load priority was not actually applied'
                target_begin=time.monotonic()
                matrix.run(variant,'M1','S3',run_label,50,gpuPriority=classes[name],allowedGpuPids=(load.pid,))
                target_end=time.monotonic()
                assert load.poll() is None,'competition load ended before target'
                (out/'stop').write_text('owned fixture completed',encoding='utf-8');rc=load.wait(timeout=20)
            finally:
                if load.poll() is None:load.kill();load.wait(timeout=8)
        text=(out/'console.log').read_text(encoding='utf-8',errors='replace');assert rc==0 and 'LOAD_RESULT pass=1' in text,text[-4000:]
        target=BASE/'logs'/TASK/run_label;receipt=json.loads((target/'result.json').read_text(encoding='utf-8'))
        log=(target/'player.log').read_text(encoding='utf-8',errors='replace');timestamps=[]
        for line in log.splitlines():
            if '[submit]' not in line:continue
            fields=dict(re.findall(r'(\w+)=(-?\d+)',line));pts=int(fields.get('pts100ns','-1'))
            if 100000000<=pts<=480000000:timestamps.append(int(fields['host100ns']))
        intervals=[(b-a)/10000 for a,b in zip(timestamps,timestamps[1:])];assert len(intervals)>100
        def percentile(values,p):
            v=sorted(values);return v[min(len(v)-1,int(len(v)*p))]
        with (out/'completed.csv').open(encoding='utf-8') as f:rows=list(csv.DictReader(f))
        ready_utc=int(re.search(r'NR_PERF_READY (\d+)',log).group(1))/1000
        stable=[float(row['completedFps']) for row in rows if ready_utc+10<float(row['wallUtc'])<ready_utc+45];assert len(stable)>15
        result={'label':run_label,'priorityRequested':name,'loadPriority':priority,'targetPriority':receipt['gpuPriority'],
                'loadExeSha256':matrix.digest(app/'veyra_nr_gpu_competition_load.exe'),'targetExeSha256':receipt['exeSha256'],
                'loadCompletedFpsMedian':statistics.median(stable),'loadCompletedFpsRange':[min(stable),max(stable)],
                'presentIntervalsMs':{'p50':percentile(intervals,.5),'p95':percentile(intervals,.95),'p99':percentile(intervals,.99),'max':max(intervals),'count':len(intervals)},
                'targetSummary':receipt['summary'],'loadRawLog':str(out),'targetRawLog':str(target),
                'passed':receipt['passed'] and priority['applied'] and receipt['gpuPriority']['applied'],'measurement':'Actual successful Present software return intervals; load completed NR graphs; neither is display scanout'}
        assert receipt['gpuPriority']['applied'], 'Player GPU priority was not actually applied; measurements invalid'
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
        print('PRIORITY_RESULT',json.dumps({k:result[k] for k in ('label','targetPriority','loadCompletedFpsMedian','presentIntervalsMs','passed')},ensure_ascii=False),flush=True)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
