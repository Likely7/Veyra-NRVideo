"""Ordinary source, full-output pixels/control/fallback proof; timing is separate."""
from pathlib import Path
import csv,importlib.util,json,os,re,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];groups=sys.argv[3:] or ['single','single-sr','2','3','2-sr','3-sr','mixed','temporal','protected','edits']
matrix.assert_gpu_tests_idle();app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
exe=app/'veyra_nr_low_chain_experiment.exe';shutil.copy2(BASE/'build'/TASK/variant/exe.name,exe);results=[]
for group in groups:
 for mode in ('off','on'):
  matrix.assert_gpu_tests_idle();name=f'{label}-{group}-{mode}';out=BASE/'logs'/TASK/name;tmp=BASE/'tmp'/TASK/name
  for path in (out,tmp):path.mkdir(parents=True,exist_ok=False)
  env=os.environ.copy()
  for key in tuple(env):
   if key.upper().startswith('VEYRA_'):env.pop(key)
  env.update(TEMP=str(tmp),TMP=str(tmp))
  if mode=='on':env['VEYRA_TEST_NR_LOW_CHAIN']='1'
  command=[str(exe),str(matrix.SOURCES['M1']),str(out),group];print('START',name,flush=True);start=time.monotonic()
  with (out/'console.log').open('xb') as stream:
   try:rc=subprocess.run(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=250).returncode
   except subprocess.TimeoutExpired:rc=124
  text=(out/'console.log').read_text(encoding='utf-8',errors='replace');line=next((s for s in text.splitlines() if s.startswith('LOW_CHAIN_RESULT ')),'')
  metrics={k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)};rows=list(csv.DictReader((out/'frames.csv').open())) if (out/'frames.csv').exists() else []
  result={'name':name,'group':group,'mode':mode,'command':command,'exitCode':rc,'metrics':metrics,'frames':rows,'wallSeconds':time.monotonic()-start,
   'exeSha256':matrix.digest(exe),'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),'sourceSha256':matrix.digest(matrix.SOURCES['M1']),
   'passed':rc==0 and metrics.get('pass')==1 and metrics.get('debugErrors')==0,'measurement':'40 full frames with PNG/readback; image/control correctness only; no timing claim or GPU competition'}
  (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result);print('RESULT',name,result['passed'],metrics,flush=True)
  (BASE/'logs'/TASK/(label+'-completed.json')).write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
  if not result['passed']:print(text[-6500:],flush=True);raise SystemExit(1)
comparisons=[]
for group in groups:
 off,on=[r for r in results if r['group']==group];row={'group':group,'equalCount':len(off['frames'])==len(on['frames']),'differentRows':sum(a!=b for a,b in zip(off['frames'],on['frames'])),'active':on['metrics']['active']}
 if not row['active']:assert row['equalCount'] and row['differentRows']==0,row
 comparisons.append(row)
(BASE/'logs'/TASK/(label+'-summary.json')).write_text(json.dumps({'results':results,'comparisons':comparisons},ensure_ascii=False,indent=2),encoding='utf-8')
print('LOW_CHAIN_COMPLETE',comparisons,flush=True)
