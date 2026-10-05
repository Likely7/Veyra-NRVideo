"""Matched 20-cycle product-graph rebuild runs, preserving raw image outputs."""
from pathlib import Path
import csv
import importlib.util
import json
import os
import shutil
import statistics
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py')
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];assert all(x.replace('-','').isalnum() for x in (variant,label))
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
package=BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable'
shutil.copytree(package,app,copy_function=matrix.copy_dependency)
build_variant=variant.removesuffix('-off')
exe=app/'veyra_nr_rebuild_benchmark.exe';shutil.copy2(BASE/'build'/TASK/build_variant/exe.name,exe)
results=[]
def percentile(values,p):
    values=sorted(values);at=(len(values)-1)*p/100;low=int(at);high=min(low+1,len(values)-1)
    return values[low]+(values[high]-values[low])*(at-low)
groups=sys.argv[3:] or ['nr','sr','layers','sizes']
for group in groups:
    assert group in ('nr','sr','layers','sizes')
    for repeat in range(1,4):
        name=f'{label}-{group}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
        out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
        env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_VERBOSE_FRAME_LOGS='1')
        for key in tuple(env):
            if key.upper().startswith('VEYRA_') and key!='VEYRA_VERBOSE_FRAME_LOGS':env.pop(key)
        if variant.endswith('off'):env['VEYRA_TEST_DISABLE_NGX_CORE_REUSE']='1'
        cmd=[str(exe),str(matrix.SOURCES['M1']),str(out),group]
        print('START',name,flush=True);gpu_before=matrix.gpu_query();start=time.monotonic();meters=[]
        with (out/'console.log').open('xb') as log:
            proc=subprocess.Popen(cmd,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT)
            while proc.poll() is None and time.monotonic()-start<280:
                meters.append({'elapsed':time.monotonic()-start,'gpu':matrix.gpu_query()});time.sleep(.7)
            if proc.poll() is None:proc.kill();proc.wait(timeout=8);rc=124
            else:rc=proc.returncode
        (out/'meters.json').write_text(json.dumps(meters,ensure_ascii=False,indent=2),encoding='utf-8')
        text=(out/'console.log').read_text(encoding='utf-8',errors='replace')
        rows=list(csv.DictReader((out/'rebuild.csv').open())) if (out/'rebuild.csv').exists() else []
        metrics={k:{'p50':statistics.median(float(r[k]) for r in rows),'p95':percentile([float(r[k]) for r in rows],95),
                    'min':min(float(r[k]) for r in rows),'max':max(float(r[k]) for r in rows)}
                 for k in ('createMs','destroyMs','totalMs','vramLive','vramAfter')} if rows else {}
        r={'variant':variant,'group':group,'repeat':repeat,'command':cmd,'exitCode':rc,'wallSeconds':time.monotonic()-start,
            'exeSha256':matrix.digest(exe),'sourceSha256':matrix.digest(matrix.SOURCES['M1']),
            'gpuBefore':gpu_before,'gpuAfter':matrix.gpu_query(),
           'nrSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),'rowCount':len(rows),'metrics':metrics,
           'frameHashes':{p.name:matrix.digest(p) for p in sorted(out.glob('frame-*.rgba'))},
           'passed':rc==0 and len(rows)==20 and 'RESULT pass=1 debugErrors=0 deviceRemoved=0' in text,
           'measurement':'Native product graph, no actual Present; CPU create/destroy includes required GPU waits'}
        results.append(r);(out/'result.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
        print('RESULT',name,rc,metrics,flush=True)
        if not r['passed']:
            print('\n'.join(text.splitlines()[-30:]),flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:
    json.dump(results,f,ensure_ascii=False,indent=2)
print('REBUILD_COMPLETE',label,flush=True)
