"""Only owned NR competition; actual independent FG presentation queue HIGH."""
from pathlib import Path
import csv,importlib.util,json,os,re,shutil,subprocess,sys,time,statistics
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle()
app=BASE/'tests'/TASK/(label+'-load-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_nr_gpu_competition_load.exe',app/'veyra_nr_gpu_competition_load.exe')
matrix.stage(variant);results=[]
for multiplier in (2,3):
 setting='S4-'+str(multiplier);matrix.CONFIGS[setting]={**matrix.CONFIGS['S4'],'multiplier':multiplier}
 for repeat,order in enumerate([('normal','high'),('high','normal'),('normal','high')],1):
  for priority in order:
   matrix.assert_gpu_tests_idle();name=f'{label}-{multiplier}X-{priority}-r{repeat}';out=BASE/'logs'/TASK/(name+'-load');tmp=BASE/'tmp'/TASK/(name+'-load')
   for p in (out,tmp):p.mkdir(parents=True,exist_ok=False)
   env=os.environ.copy()
   for key in tuple(env):
    if key.upper().startswith('VEYRA_'):env.pop(key)
   env.update(TEMP=str(tmp),TMP=str(tmp));command=[str(app/'veyra_nr_gpu_competition_load.exe'),str(matrix.SOURCES['M1']),str(out),'100']
   print('QUEUE_START',name,flush=True);started=time.monotonic()
   with (out/'console.log').open('xb') as console:
    load=subprocess.Popen(command,cwd=app,env=env,stdout=console,stderr=subprocess.STDOUT)
    try:
     while time.monotonic()-started<40 and load.poll() is None:
      if 'LOAD_READY' in (out/'console.log').read_text(encoding='utf-8',errors='replace'):break
      time.sleep(.25)
     assert load.poll() is None and 'LOAD_READY' in (out/'console.log').read_text(encoding='utf-8',errors='replace')
     matrix.run(variant,'M1',setting,name,50,allowedGpuPids=(load.pid,),testEnv={'VEYRA_TEST_PRESENT_QUEUE_PRIORITY':priority})
     assert load.poll() is None,'load ended before target';(out/'stop').write_text('owned target complete',encoding='utf-8');rc=load.wait(timeout=20)
    finally:
     if load.poll() is None:load.kill();load.wait(timeout=8)
   text=(out/'console.log').read_text(encoding='utf-8',errors='replace');assert rc==0 and 'LOAD_RESULT pass=1' in text,text[-4000:]
   target=BASE/'logs'/TASK/name;receipt=json.loads((target/'result.json').read_text(encoding='utf-8'));log=(target/'player.log').read_text(encoding='utf-8',errors='replace')
   actual=[int(x) for x in re.findall(r'actualType=0 actualPriority=(\d+)',log)];expected=100 if priority=='high' else 0
   assert actual and all(x==expected for x in actual),'Requested queue priority not applied'
   rows=[]
   for line in log.splitlines():
    if '[submit]' not in line:continue
    fields=dict(re.findall(r'(\w+)=(-?\d+)',line));pts=int(fields.get('pts100ns','-1'))
    if 100000000<=pts<=480000000:rows.append({k:int(v) for k,v in fields.items()})
   intervals=[(b['host100ns']-a['host100ns'])/10000 for a,b in zip(rows,rows[1:])];assert len(intervals)>100
   def pct(p):
    v=sorted(intervals);return v[min(len(v)-1,int(len(v)*p))]
   completed=list(csv.DictReader((out/'completed.csv').open()));ready=int(re.search(r'NR_PERF_READY (\d+)',log).group(1))/1000
   fps=[float(r['completedFps']) for r in completed if ready+10<float(r['wallUtc'])<ready+45];assert len(fps)>15
   result={'name':name,'multiplier':multiplier,'repeat':repeat,'priority':priority,'actualQueuePriorities':actual,
    'presentIntervalsMs':{'p50':pct(.5),'p95':pct(.95),'p99':pct(.99),'max':max(intervals),'count':len(intervals)},
    'loadCompletedFpsMedian':statistics.median(fps),'targetSummary':receipt['summary'],'targetExeSha256':receipt['exeSha256'],
    'loadExeSha256':matrix.digest(app/'veyra_nr_gpu_competition_load.exe'),'command':command,'passed':receipt['passed'],
    'measurement':'Actual successful software Present return intervals, two NR plus SR plus DLSS2X/3X, own three-NR competing load; no scanout'}
   (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
   print('QUEUE_RESULT',name,result['presentIntervalsMs'],result['loadCompletedFpsMedian'],flush=True)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
