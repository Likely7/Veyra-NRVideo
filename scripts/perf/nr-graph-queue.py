"""Whole natural graph/NVOF/FG pixel and timestamp equality before timing."""
from pathlib import Path
import csv,importlib.util,json,os,re,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];groups=sys.argv[3:] or ['nr','srnr','2x','3x'];matrix.assert_gpu_tests_idle()
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_nr_graph_queue_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe);results=[]
for group in groups:
 for repeat in range(1,4):
  for mode in ('direct','compute'):
   matrix.assert_gpu_tests_idle();name=f'{label}-{group}-{mode}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
   for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
   env=os.environ.copy()
   for key in tuple(env):
    if key.upper().startswith('VEYRA_'):env.pop(key)
   env.update(TEMP=str(tmp),TMP=str(tmp))
   if mode=='compute':env['VEYRA_TEST_GRAPH_COMPUTE']='1'
   command=[str(exe),str(matrix.SOURCES['M1']),str(out),group];print('START',name,flush=True);before=matrix.gpu_query();start=time.monotonic()
   with (out/'console.log').open('xb') as f:
    try:rc=subprocess.run(command,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=150).returncode
    except subprocess.TimeoutExpired:rc=124
   text=(out/'console.log').read_text(encoding='utf-8',errors='replace');line=next((s for s in text.splitlines() if s.startswith('GRAPH_QUEUE_RESULT ')),'')
   metrics={k:float(v) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',line)}
   rows=list(csv.DictReader((out/'frames.csv').open())) if (out/'frames.csv').exists() else []
   result={'group':group,'mode':mode,'repeat':repeat,'command':command,'exitCode':rc,'metrics':metrics,'frames':rows,'wallSeconds':time.monotonic()-start,
    'exeSha256':matrix.digest(exe),'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),
    'sourceSha256':matrix.digest(matrix.SOURCES['M1']),'beforeGpu':before,'afterGpu':matrix.gpu_query(),
    'passed':rc==0 and metrics.get('pass')==1 and metrics.get('debugErrors')==0,
    'measurement':'Full product graph and NVOF; all current real/generated output pixels and PTS, waits/readbacks diagnostic only; no throughput claim'}
   (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result);print('RESULT',name,result['passed'],metrics,flush=True)
   if not result['passed']:print(text[-8500:],flush=True);raise SystemExit(1)
comparisons=[]
for group in groups:
 rows=[r for r in results if r['group']==group];ref=rows[0]['frames']
 comparisons.append({'group':group,'freshNoiseDifferentRows':sum(a!=b for r in rows if r['mode']=='direct' for a,b in zip(ref,r['frames'])),
  'computeDifferentRows':sum(a!=b for r in rows if r['mode']=='compute' for a,b in zip(ref,r['frames'])),
  'sameRowCounts':all(len(r['frames'])==len(ref) for r in rows)})
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump({'results':results,'comparisons':comparisons},f,ensure_ascii=False,indent=2)
print('GRAPH_QUEUE_COMPARISONS',comparisons,flush=True)
