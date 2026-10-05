"""Isolated exact GPU comparison and actual WGC of an owned pattern window."""
from pathlib import Path
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py')
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3]
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_duplicate_gpu_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
shader=app/'DuplicateFrameCS.hlsl';shutil.copy2(ROOT/'tests/perf/DuplicateFrameCS.hlsl',shader)
results=[]
for mode,repeat in [('compare',1),('compare',2),('compare',3),('capture',1)]:
    matrix.assert_gpu_tests_idle()
    name=f'{label}-{mode}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
    for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
    env=os.environ.copy()
    for key in tuple(env):
        if key.upper().startswith('VEYRA_'):env.pop(key)
    env.update(TEMP=str(tmp),TMP=str(tmp))
    command=[str(exe),str(shader),str(out),mode];before=matrix.gpu_query();start=time.monotonic()
    print('START',name,flush=True)
    with (out/'console.log').open('xb') as log:
        try:rc=subprocess.run(command,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=280).returncode
        except subprocess.TimeoutExpired:rc=124
    text=(out/'console.log').read_text(encoding='utf-8',errors='replace')
    result={'variant':variant,'mode':mode,'repeat':repeat,'exeSha256':matrix.digest(exe),'shaderSha256':matrix.digest(shader),
            'beforeGpu':before,'afterGpu':matrix.gpu_query(),'command':command,'exitCode':rc,'wallSeconds':time.monotonic()-start,
            'passed':rc==0 and 'RESULT failures=0 debugErrors=0 deviceRemoved=0' in text,
            'capturePrivacy':'Only the owned GDI pattern window; never a user desktop/application or capture card'}
    results.append(result);(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('RESULT',name,rc,result['passed'],flush=True)
    if not result['passed']:print(text[-6000:],flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
