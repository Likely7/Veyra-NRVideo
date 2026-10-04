"""Real SDK/static-input output contract, before any 1a product optimization."""
from pathlib import Path
import csv
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
variant,label=sys.argv[1:3];modes=sys.argv[3:] or ['nr','nr-temporal','srnr','sr','off']
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_nr_static_input_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
results=[]
for mode in modes:
    assert mode in ('nr','nr-temporal','srnr','sr','off','nr-reset','srnr-reset')
    for repeat in range(1,4):
        name=f'{label}-{mode}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
        for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
        env=os.environ.copy()
        for key in tuple(env):
            if key.upper().startswith('VEYRA_'):env.pop(key)
        env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_VERBOSE_FRAME_LOGS='1')
        command=[str(exe),str(matrix.SOURCES['M1']),str(out),mode]
        print('START',name,flush=True);start=time.monotonic()
        with (out/'console.log').open('xb') as log:
            try:rc=subprocess.run(command,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=280).returncode
            except subprocess.TimeoutExpired:rc=124
        text=(out/'console.log').read_text(encoding='utf-8',errors='replace')
        rows=list(csv.DictReader((out/'frames.csv').open())) if (out/'frames.csv').exists() else []
        changes=[int(r['frame']) for r in rows if r['equalFirst']=='0']
        result={'variant':variant,'mode':mode,'repeat':repeat,'exeSha256':matrix.digest(exe),
                'nrSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),'sourceSha256':matrix.digest(matrix.SOURCES['M1']),
                'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,'frameCount':len(rows),
                'differentOutputFramesToFirst':len(changes),'firstDifferentFrame':changes[0] if changes else None,
                'sha256Sequence':[r['sha256'] for r in rows],
                'safetyPassed':rc==0 and len(rows)==300 and 'RESULT safetyPass=1' in text and 'debugErrors=0 deviceRemoved=0' in text,
                'reuseFirstOutputIsByteIdentical':not changes,'measurement':'Actual product graph; static natural input; diagnostic readbacks excluded from process timing'}
        results.append(result);(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print('RESULT',name,{k:result[k] for k in ('exitCode','differentOutputFramesToFirst','firstDifferentFrame','safetyPassed')},flush=True)
        if not result['safetyPassed']:
            print('\n'.join(text.splitlines()[-30:]),flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
