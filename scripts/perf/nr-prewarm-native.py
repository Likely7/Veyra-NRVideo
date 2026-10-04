"""Never-evaluated prepared features versus fresh features: three full outputs."""
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
exe=app/'veyra_nr_prewarm_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
results=[]
for group in ('nr','srnr','sr'):
    for repeat in range(1,4):
        for mode in ('off','on'):
            matrix.assert_gpu_tests_idle();name=f'{label}-{group}-{mode}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
            for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
            env=os.environ.copy()
            for key in tuple(env):
                if key.upper().startswith('VEYRA_'):env.pop(key)
            env.update(TEMP=str(tmp),TMP=str(tmp));command=[str(exe),str(matrix.SOURCES['M1']),str(out),mode,group]
            print('START',name,flush=True);before=matrix.gpu_query();start=time.monotonic()
            with (out/'console.log').open('xb') as f:
                try:rc=subprocess.run(command,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=90).returncode
                except subprocess.TimeoutExpired:rc=124
            text=(out/'console.log').read_text(encoding='utf-8',errors='replace')
            metrics={k:float(v) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',next((s for s in text.splitlines() if s.startswith('PREWARM_RESULT ')),''))}
            hashes={p.name:matrix.digest(p) for p in out.glob('*.rgba')}
            result={'group':group,'repeat':repeat,'mode':mode,'command':command,'exitCode':rc,'metrics':metrics,
                'exeSha256':matrix.digest(exe),'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),
                'sourceSha256':matrix.digest(matrix.SOURCES['M1']),'wallSeconds':time.monotonic()-start,
                'gpuBefore':before,'gpuAfter':matrix.gpu_query(),'hashes':hashes,
                'passed':rc==0 and metrics.get('pass')==1 and metrics.get('debugErrors')==0 and len(hashes)==3,
                'measurement':'Product PreviewGpuSession and actual natural GPU output; CPU create/adoption/first GPU-completion, no Present/scanout'}
            (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
            print('RESULT',name,result['passed'],metrics.get('firstGpuCompleteMs'),flush=True)
            if not result['passed']:print(text[-4500:],flush=True);raise SystemExit(1)
comparisons=[]
for group in ('nr','srnr','sr'):
    runs=[r for r in results if r['group']==group];reference=runs[0]['hashes']
    comparison={'group':group,'freshNoiseDifferentFrames':sum(a['hashes'][f]!=reference[f] for a in runs if a['mode']=='off' for f in reference),
        'prewarmDifferentFrames':sum(a['hashes'][f]!=reference[f] for a in runs if a['mode']=='on' for f in reference)}
    if group!='sr':
        legacy_group='nr' if group=='nr' else 'sr'
        legacy=[matrix.digest(BASE/'logs'/TASK/f'B2a-rebuild-v2-{legacy_group}-r{r}'/'frame-0.rgba') for r in range(1,4)]
        comparison['matchesSealedPriorNodeFirstThreeFrames']=all(a['hashes']['frame-2.rgba']==h for a in runs for h in legacy)
    comparisons.append(comparison)
summary={'results':results,'comparisons':comparisons,'passed':all(c['freshNoiseDifferentFrames']==0 and c['prewarmDifferentFrames']==0 and c.get('matchesSealedPriorNodeFirstThreeFrames',True) for c in comparisons)}
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(summary,f,ensure_ascii=False,indent=2)
print('PIXEL_COMPARISONS',comparisons,flush=True);raise SystemExit(0 if summary['passed'] else 1)
