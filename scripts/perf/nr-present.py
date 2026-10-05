"""Owned native backbuffer idle/invalidation experiment, enabled/off paired."""
from pathlib import Path
import importlib.util
import json
import os
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle()
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_paused_present_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe);results=[];reference=None
for mode in ('enabled','off'):
    for repeat in range(1,4):
        matrix.assert_gpu_tests_idle();name=f'{label}-{mode}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
        out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
        env=os.environ.copy()
        for key in tuple(env):
            if key.upper().startswith('VEYRA_'):env.pop(key)
        env.update(TEMP=str(tmp),TMP=str(tmp))
        if mode=='off':env['VEYRA_TEST_DISABLE_PAUSED_PRESENT_REUSE']='1'
        print('START',name,flush=True)
        with (out/'console.log').open('xb') as log:
            proc=subprocess.run([str(exe),str(matrix.SOURCES['M1']),str(out)],cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=280)
        text=(out/'console.log').read_text(encoding='utf-8',errors='replace');files={p.name:matrix.digest(p) for p in out.glob('*.rgba')}
        if reference is None:reference=files
        result={'mode':mode,'repeat':repeat,'exitCode':proc.returncode,'exeSha256':matrix.digest(exe),'imagesSha256':files,
                'differentImages':sum(reference[k]!=v for k,v in files.items()),'passed':proc.returncode==0 and 'RESULT pass=1' in text and files==reference and len(files)==8}
        (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8');results.append(result);print('RESULT',name,json.dumps(result),flush=True)
        if not result['passed']:print(text[-6000:],flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,indent=2)
