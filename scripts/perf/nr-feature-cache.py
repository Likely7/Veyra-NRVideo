"""Bounded real feature-cache native experiment, hashes and raw memory curves."""
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
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py')
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];app=BASE/'tests'/TASK/(label+'-app')
assert not app.exists();matrix.assert_gpu_tests_idle()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_nr_recent_cache_experiment.exe'
shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
results=[]
for repeat in range(1,4):
    for disabled in (True,False):
        matrix.assert_gpu_tests_idle();name=f'{label}-'+('off' if disabled else 'on')+f'-r{repeat}'
        out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
        for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
        env=os.environ.copy()
        for key in tuple(env):
            if key.upper().startswith('VEYRA_'):env.pop(key)
        env.update(TEMP=str(tmp),TMP=str(tmp))
        if disabled:env['VEYRA_TEST_DISABLE_RECENT_GRAPH_CACHE']='1'
        command=[str(exe),str(matrix.SOURCES['M1']),str(out)];before=matrix.gpu_query();start=time.monotonic()
        print('START',name,flush=True)
        with (out/'console.log').open('xb') as f:
            try:rc=subprocess.run(command,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=280).returncode
            except subprocess.TimeoutExpired:rc=124
        text=(out/'console.log').read_text(encoding='utf-8',errors='replace')
        rows=list(csv.DictReader((out/'cache.csv').open())) if (out/'cache.csv').exists() else []
        hits=sum(int(r['hit']) for r in rows)
        creates=[float(r['createMs']) for r in rows if int(r['nr']) and int(r['cycle'])>0]
        result={'variant':variant,'label':label,'disabled':disabled,'repeat':repeat,'exeSha256':matrix.digest(exe),
                'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),
                'sourceSha256':matrix.digest(matrix.SOURCES['M1']),'beforeGpu':before,'afterGpu':matrix.gpu_query(),
                'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,'frameCount':len(rows),
                'hits':hits,'warmCreateMedianMs':statistics.median(creates) if creates else None,
                'sha256Sequence':[r['sha256'] for r in rows],
                'finalVram':int(re.search(r'afterFinalClose=(\d+)',text).group(1)) if 'afterFinalClose=' in text else None,
                'passed':rc==0 and len(rows)==50 and 'CACHE_RESULT pass=1' in text and
                         'debugErrors=0 deviceRemoved=0' in text and hits==(0 if disabled else 24),
                'measurement':'Actual graph creation/reactivation CPU with GPU completion, full output SHA; no display scanout'}
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        results.append(result);print('RESULT',name,result['passed'],hits,result['warmCreateMedianMs'],flush=True)
        if not result['passed']:print(text[-6500:],flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
