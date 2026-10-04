"""E2 extension: every successful Present carries a newly evaluated NR output."""
from pathlib import Path
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
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle()
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_nr_concurrent_create_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
results=[]
for repeat in range(1,4):
    for mode in ('serial','concurrent'):
        matrix.assert_gpu_tests_idle();name=f'{label}-{mode}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
        for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
        env=os.environ.copy()
        for key in tuple(env):
            if key.upper().startswith('VEYRA_'):env.pop(key)
        env.update(TEMP=str(tmp),TMP=str(tmp))
        command=[str(exe),str(app/'runtime/experimental'),str(out),'60','50',mode,'present']
        start=time.monotonic();print('START',name,flush=True);meters=[]
        with (out/'console.log').open('xb') as f:
            proc=subprocess.Popen(command,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT)
            while proc.poll() is None and time.monotonic()-start<280:
                meters.append({'elapsed':time.monotonic()-start,'gpu':matrix.gpu_query()});time.sleep(1)
            if proc.poll() is None:proc.kill();proc.wait(timeout=8);rc=124
            else:rc=proc.returncode
        text=(out/'console.log').read_text(encoding='utf-8',errors='replace')
        line=next((s for s in text.splitlines() if s.startswith('RESULT ')),'')
        metrics={k:float(v) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',line)}
        rows=list(csv.DictReader((out/'present.csv').open()));intervals=[float(r['intervalMs']) for r in rows[1:]]
        def percentile(p):
            values=sorted(intervals);at=(len(values)-1)*p/100;lo=int(at);hi=min(lo+1,len(values)-1)
            return values[lo]+(values[hi]-values[lo])*(at-lo)
        pass_fresh=all(int(r['sequence'])==int(r['presentCount'])==i+1 for i,r in enumerate(rows))
        result={'repeat':repeat,'mode':mode,'command':command,'exeSha256':matrix.digest(exe),
            'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),'exitCode':rc,'metrics':metrics,
            'wallSeconds':time.monotonic()-start,'presentSamples':len(rows),'freshEveryPresent':pass_fresh,
            'intervals':{'p50':statistics.median(intervals),'p95':percentile(95),'p99':percentile(99),'max':max(intervals)},
            'gapsAboveTwo60HzIntervals':sum(x>1000/30 for x in intervals),
            'safetyPassed':rc==0 and metrics.get('pass')==1 and metrics.get('debugErrors')==0 and len(rows)>1000 and pass_fresh,
            'seamlessPassed':max(intervals)<1000/30,
            'measurement':'Actual newly evaluated Feature18 output CopyResource and successful Present return; static authored source, no scanout or AV drift proof'}
        (out/'meters.json').write_text(json.dumps(meters,ensure_ascii=False,indent=2),encoding='utf-8')
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
        print('RESULT',name,result['safetyPassed'],result['intervals'],result['gapsAboveTwo60HzIntervals'],flush=True)
        if not result['safetyPassed']:print(text[-5000:],flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
