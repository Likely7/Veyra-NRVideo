"""Full graph pixels/fence monotonicity across ordinary compute/direct migrations."""
from pathlib import Path
import csv,importlib.util,json,os,re,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle();app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_nr_queue_migration_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe);results=[]
for repeat in range(1,4):
 for mode in ('direct','auto','creation-fallback'):
  matrix.assert_gpu_tests_idle();name=f'{label}-{mode}-r{repeat}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
  for path in (out,tmp):path.mkdir(parents=True,exist_ok=False)
  env=os.environ.copy()
  for key in tuple(env):
   if key.upper().startswith('VEYRA_'):env.pop(key)
  env.update(TEMP=str(tmp),TMP=str(tmp))
  if mode=='direct':env['VEYRA_TEST_GRAPH_DIRECT']='1'
  if mode=='creation-fallback':env['VEYRA_TEST_GRAPH_CREATE_FAIL']='1'
  command=[str(exe),str(matrix.SOURCES['M1']),str(out)];print('START',name,flush=True);start=time.monotonic()
  with (out/'console.log').open('xb') as stream:
   try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=150).returncode
   except subprocess.TimeoutExpired:rc=124
  text=(out/'console.log').read_text(encoding='utf-8',errors='replace');line=next((s for s in text.splitlines() if s.startswith('QUEUE_MIGRATION_RESULT ')),'')
  metrics={k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)}
  rows=list(csv.DictReader((out/'frames.csv').open())) if (out/'frames.csv').exists() else []
  result={'name':name,'mode':mode,'repeat':repeat,'command':command,'testEnv':{k:v for k,v in env.items() if k.startswith('VEYRA_TEST_')},'exitCode':rc,'metrics':metrics,'frames':rows,'wallSeconds':time.monotonic()-start,
   'exeSha256':matrix.digest(exe),'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),'sourceSha256':matrix.digest(matrix.SOURCES['M1']),
   'passed':rc==0 and metrics.get('pass')==1 and metrics.get('debugErrors')==0 and metrics.get('migrations')==(4 if mode=='auto' else 0),
   'measurement':'Full pixels/PTS across queue rebuilds with diagnostic waits/readback; not throughput; no extra competing process'}
  (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result);print('RESULT',name,result['passed'],metrics,flush=True)
  if not result['passed']:print(text[-4500:],flush=True);raise SystemExit(1)
reference=results[0]['frames'];comparisons=[{'name':r['name'],'rowCount':len(r['frames']),'differentRows':sum(a!=b for a,b in zip(reference,r['frames'])),'equalCount':len(reference)==len(r['frames'])} for r in results]
passed=all(r['equalCount'] and r['differentRows']==0 for r in comparisons)
(BASE/'logs'/TASK/(label+'-summary.json')).write_text(json.dumps({'results':results,'comparisons':comparisons,'passed':passed},ensure_ascii=False,indent=2),encoding='utf-8')
print('QUEUE_MIGRATION_COMPLETE',passed,comparisons,flush=True);raise SystemExit(0 if passed else 1)
