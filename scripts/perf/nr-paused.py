"""Actual graph paused residual edits; bounded processes and full-frame hashes."""
from pathlib import Path
import csv
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];modes=sys.argv[3:] or ['nr','srnr','temporal','layers']
app=BASE/'tests'/TASK/(label+'-app');exe=app/'veyra_nr_paused_residual_experiment.exe'
if not app.exists():
    shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
    shutil.copy2(BASE/'build'/TASK/variant.removesuffix('-off')/exe.name,exe)
    (app/'paused-test-stage.json').write_text(json.dumps({'exeSha256':matrix.digest(exe),'sourceCommit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD']).decode().strip()}),encoding='utf-8')
else:
    assert matrix.digest(exe)==json.loads((app/'paused-test-stage.json').read_text())['exeSha256']
if modes==['--stage-only']:
    print('SEALED',app,matrix.digest(exe));raise SystemExit(0)
results=[]
for mode in modes:
    assert mode in ('nr','srnr','temporal','layers')
    for repeat in range(1,4):
        matrix.assert_gpu_tests_idle()
        name=f'{label}-{mode}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
        for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
        env=os.environ.copy()
        for key in tuple(env):
            if key.upper().startswith('VEYRA_'):env.pop(key)
        env.update(TEMP=str(tmp),TMP=str(tmp))
        if variant.endswith('-off'):env['VEYRA_TEST_DISABLE_PAUSED_NR_RESIDUAL_REUSE']='1'
        command=[str(exe),str(matrix.SOURCES['M1']),str(out),mode];before=matrix.gpu_query();start=time.monotonic();print('START',name,flush=True)
        with (out/'console.log').open('xb') as log:
            try:rc=subprocess.run(command,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=280).returncode
            except subprocess.TimeoutExpired:rc=124
        text=(out/'console.log').read_text(encoding='utf-8',errors='replace');rows=list(csv.DictReader((out/'edits.csv').open())) if (out/'edits.csv').exists() else []
        result={'variant':variant,'label':label,'mode':mode,'repeat':repeat,'exeSha256':matrix.digest(exe),'sourceSha256':matrix.digest(matrix.SOURCES['M1']),
                'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),'beforeGpu':before,'afterGpu':matrix.gpu_query(),'command':command,
                'exitCode':rc,'wallSeconds':time.monotonic()-start,'frameCount':len(rows),'nrEvaluations':sum(int(r['nrDelta']) for r in rows),'srEvaluations':sum(int(r['srDelta']) for r in rows),
                'passed':rc==0 and len(rows)==300 and 'RESULT safetyPass=1' in text and 'debugErrors=0 deviceRemoved=0' in text,
                'sha256Sequence':[r['sha256'] for r in rows],'measurement':'Diagnostic readback excluded from process timing; explicit reset on each paused edit'}
        results.append(result);(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print('RESULT',name,rc,result['passed'],result['nrEvaluations'],flush=True)
        if not result['passed']:print(text[-5000:],flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
