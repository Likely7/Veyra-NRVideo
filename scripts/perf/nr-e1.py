"""Serial bounded E1 trials; failed runtime contracts are evidence, never PASS."""
from pathlib import Path
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import importlib.util

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py')
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
label=sys.argv[1];assert label.replace('-','').isalnum()
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
package=BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable'
shutil.copytree(package,app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/'E1/veyra_nr_dynamic_size_experiment.exe',app/'veyra_nr_dynamic_size_experiment.exe')
results=[]
for runtime,expected in [('Lecram','f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc'),
                         ('original','e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e')]:
    directory=app/'runtime/experimental';directory=directory if runtime=='Lecram' else directory/'nr-original'
    assert matrix.digest(directory/'nvngx_dlssnr.dll')==expected
    for method in ('subrect','dimensions'):
        for texture in ('full','small'):
            name=f'{label}-{runtime}-{method}-{texture}'
            out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
            out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
            env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp))
            cmd=[str(app/'veyra_nr_dynamic_size_experiment.exe'),str(directory),str(out),method,texture]
            print('START',name,flush=True);start=time.monotonic()
            with (out/'console.log').open('xb') as log:
                try:rc=subprocess.run(cmd,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180).returncode
                except subprocess.TimeoutExpired:rc=124
            text=(out/'console.log').read_text(encoding='utf-8',errors='replace')
            rows=list(csv.DictReader((out/'observations.csv').open())) if (out/'observations.csv').exists() else []
            r={'runtime':runtime,'runtimeSha256':expected,'method':method,'textures':texture,'command':cmd,
               'exeSha256':matrix.digest(app/'veyra_nr_dynamic_size_experiment.exe'),
               'exitCode':rc,'wallSeconds':time.monotonic()-start,'observations':rows,
               'outputMatchesExactSizeReference':rc==0,'log':str(out/'console.log')}
            results.append(r);(out/'result.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
            print('RESULT',name,rc,rows,flush=True)
            # Actual device removal requires diagnostic follow-up, not more trials.
            if rc==124 or ('deviceRemoved=0x00000000' not in text and 'deviceRemoved=' in text):
                raise SystemExit('Trial terminated/device removed; inspect preserved evidence')
path=BASE/'logs'/TASK/(label+'-summary.json')
with path.open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
print('E1_COMPLETE',path,flush=True)
