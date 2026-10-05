"""Matched normal playback: existing per-layer dimensions, no test GPU load."""
from pathlib import Path
import importlib.util,json,statistics,sys,re
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3]
folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
matrix.assert_gpu_tests_idle();app=matrix.stage(variant,label)
payload={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
results=[]
for count in (2,3):
 for repeat in range(1,4):
  for mode in (('native','mixed') if repeat%2 else ('mixed','native')):
   group=f'{count}-{mode}';setting='MIX-'+group
   policies=[0]*count if mode=='native' else ([3,0] if count==2 else [2,3,0])
   matrix.CONFIGS[setting]={'layers':count,'policies':policies,'temporal':False,'sr':False}
   name=f'{label}-{group}-r{repeat}';print('START',name,flush=True)
   matrix.run(variant,'M1',setting,name,50,gpuPriority=2,stageLabel=label)
   receiptPath=BASE/'logs'/TASK/name/'result.json';receipt=json.loads(receiptPath.read_text(encoding='utf-8'))
   log=(receiptPath.parent/'player.log').read_text(encoding='utf-8',errors='replace')
   assert receipt['gpuPriority']['applied'] and not receipt['testEnv']
   configLine=next(line for line in log.splitlines() if 'NR_PERF_CONFIG ' in line)
   actual=json.loads(configLine.split('NR_PERF_CONFIG ',1)[1]);assert [n['sizePolicy'] for n in actual['layers']]==policies
   keys=('gpuNrP95Ms','gpuResidualP95Ms','gpuFlowP95Ms','enhancementProcessingMs','gpuReadyP95Ms')
   skips=[int(value) for value in re.findall(r'previewSkipped=(\d+)',log)]
   submits=[{k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)} for line in log.splitlines() if '[submit]' in line]
   submits=[s for s in submits if 100000000<=s.get('pts100ns',-1)<=480000000]
   intervals=sorted((b['host100ns']-a['host100ns'])/10000 for a,b in zip(submits,submits[1:]));assert len(intervals)>100
   meters=json.loads((receiptPath.parent/'meters.json').read_text(encoding='utf-8'))
   row={'name':name,'group':group,'repeat':repeat,'receipt':str(receiptPath),'maxPreviewSkipped':max(skips,default=0),
        'presentIntervalsMs':{'p99':intervals[int(len(intervals)*.99)],'max':max(intervals),'count':len(intervals)},
        'gpuMemoryMiBPeak':max(float(m['gpu']['memoryMiB']) for m in meters if 'memoryMiB' in m['gpu']),
        'cpuOneCoreMedianPercent':statistics.median(m['cpuPercentOneCoreScale'] for m in meters),
        'actualLayers':actual['layers'],'stallLines':[line for line in log.splitlines() if '[engine-stall]' in line],
        'metrics':{k:receipt['summary'][k]['medianOfRollingObservations'] for k in keys}}
   results.append(row);(folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
   print('MIXED_NORMAL_RESULT',json.dumps(row,ensure_ascii=False),flush=True)
summary={}
for group in ('2-native','2-mixed','3-native','3-mixed'):
 rows=[r for r in results if r['group']==group]
 values={key:[r['metrics'][key] for r in rows] for key in rows[0]['metrics']}
 values.update(presentP99Ms=[r['presentIntervalsMs']['p99'] for r in rows],presentMaxMs=[r['presentIntervalsMs']['max'] for r in rows],
               gpuMemoryMiBPeak=[r['gpuMemoryMiBPeak'] for r in rows],cpuOneCoreMedianPercent=[r['cpuOneCoreMedianPercent'] for r in rows],
               maxPreviewSkipped=[r['maxPreviewSkipped'] for r in rows])
 summary[group]={key:{'median':statistics.median(v),'min':min(v),'max':max(v),'runs':v} for key,v in values.items()}
assert all(matrix.digest(app/name)==sha for name,sha in payload.items()),'Product payload mutated';matrix.assert_gpu_tests_idle()
(folder/'comparison.json').write_text(json.dumps({'runs':results,'summary':summary,'payloadUnchanged':True,'extraGpuLoad':False,
 'comparison':'Same unchanged product, different user-selected layer dimensions; this is not a new algorithm speedup'},ensure_ascii=False,indent=2),encoding='utf-8')
print('MIXED_NORMAL_COMPLETE',len(results),flush=True)
