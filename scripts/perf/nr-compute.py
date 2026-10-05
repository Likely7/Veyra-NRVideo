"""Compatibility and complete pixel equality, never execute an invalid list."""
from pathlib import Path
import csv,importlib.util,json,os,re,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle()
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_ngx_compute_queue_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe)
results=[]
for group,runtime in [('nr','Lecram'),('nr','original'),('sr','Lecram')]:
 for repeat in range(1,4):
  for mode in ('direct','compute'):
   matrix.assert_gpu_tests_idle();name=f'{label}-{group}-{runtime}-{mode}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
   for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
   directory=app/'runtime/experimental';directory=directory if runtime=='Lecram' else directory/'nr-original'
   env=os.environ.copy()
   for key in tuple(env):
    if key.upper().startswith('VEYRA_'):env.pop(key)
   env.update(TEMP=str(tmp),TMP=str(tmp));command=[str(exe),str(directory),str(out),mode,group]
   print('START',name,flush=True);before=matrix.gpu_query();start=time.monotonic()
   with (out/'console.log').open('xb') as f:
    try:rc=subprocess.run(command,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=90).returncode
    except subprocess.TimeoutExpired:rc=124
   text=(out/'console.log').read_text(encoding='utf-8',errors='replace');line=next((s for s in text.splitlines() if s.startswith('COMPUTE_RESULT ')),'')
   metrics={k:float(v) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',line)}
   rows=list(csv.DictReader((out/'frames.csv').open())) if (out/'frames.csv').exists() else []
   result={'group':group,'runtime':runtime,'mode':mode,'repeat':repeat,'command':command,'exitCode':rc,'metrics':metrics,
    'hashes':[r['sha256'] for r in rows],'frameCount':len(rows),'exeSha256':matrix.digest(exe),
    'runtimeSha256':matrix.digest(directory/'nvngx_dlssnr.dll'),'beforeGpu':before,'afterGpu':matrix.gpu_query(),'wallSeconds':time.monotonic()-start,
    'compatible':rc==0 and metrics.get('compatible')==1 and metrics.get('debugErrors')==0 and len(rows)==30,
    'measurement':'Create/Close/Evaluate through actual adapters, debug errors and full raw output SHA, authored static pattern; no performance/scanout claim'}
   (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
   print('RESULT',name,result['compatible'],metrics,flush=True)
   assert rc!=124 and metrics.get('deviceRemoved')==0,'Unexpected termination/device failure; preserve logs and stop'
   if mode=='direct':assert result['compatible'],text[-6500:]
comparisons=[]
for group,runtime in [('nr','Lecram'),('nr','original'),('sr','Lecram')]:
 rows=[r for r in results if r['group']==group and r['runtime']==runtime];ref=rows[0]['hashes']
 comparisons.append({'group':group,'runtime':runtime,'freshNoise':sum(a!=b for r in rows if r['mode']=='direct' for a,b in zip(ref,r['hashes'])),
  'computeCompatible':all(r['compatible'] for r in rows),'computeDifferent':sum(a!=b for r in rows if r['mode']=='compute' for a,b in zip(ref,r['hashes']))})
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump({'results':results,'comparisons':comparisons},f,ensure_ascii=False,indent=2)
print('COMPUTE_COMPARISONS',comparisons,flush=True)
