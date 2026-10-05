"""Serial E2 smoke and bounded-duration concurrent Feature18 creation trials."""
from pathlib import Path
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py')
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
label=sys.argv[1];assert label.replace('-','').isalnum()
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
package=BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable'
shutil.copytree(package,app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/'E1/veyra_nr_concurrent_create_experiment.exe',app/'veyra_nr_concurrent_create_experiment.exe')
runtime=app/'runtime/experimental'
assert matrix.digest(runtime/'nvngx_dlssnr.dll')=='f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc'
cases=[('serial-smoke',12,10,'serial'),('concurrent-smoke',12,10,'concurrent')]
if '--smoke-only' not in sys.argv:
    cases.extend((f'concurrent-long-{i}',240,50,'concurrent') for i in range(1,4))
results=[]
for case,seconds,count,mode in cases:
    name=label+'-'+case;out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
    out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
    env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp))
    cmd=[str(app/'veyra_nr_concurrent_create_experiment.exe'),str(runtime),str(out),str(seconds),str(count),mode]
    print('START',name,flush=True);start=time.monotonic()
    with (out/'console.log').open('xb') as log:
        try:rc=subprocess.run(cmd,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=280).returncode
        except subprocess.TimeoutExpired:rc=124
    text=(out/'console.log').read_text(encoding='utf-8',errors='replace')
    line=next((x for x in text.splitlines() if x.startswith('RESULT ')), '')
    metrics={k:float(v) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',line)}
    files={p.name:matrix.digest(p) for p in out.glob('*.rgba')}
    r={'case':case,'command':cmd,'exeSha256':matrix.digest(app/'veyra_nr_concurrent_create_experiment.exe'),
       'runtimeSha256':matrix.digest(runtime/'nvngx_dlssnr.dll'),'exitCode':rc,'wallSeconds':time.monotonic()-start,
       'metrics':metrics,'staticOutputHashes':files,'log':str(out/'console.log'),
       'passed':rc==0 and metrics.get('pass')==1 and metrics.get('debugErrors')==0}
    results.append(r);(out/'result.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
    print('RESULT',name,json.dumps(r,ensure_ascii=False),flush=True)
    if not r['passed']:
        print('\n'.join(text.splitlines()[-25:]),flush=True)
        raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:
    json.dump(results,f,ensure_ascii=False,indent=2)
print('E2_COMPLETE',label,flush=True)
