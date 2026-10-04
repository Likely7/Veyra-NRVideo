"""Interleaved ordinary-player off/on comparisons after low-chain pixel proof."""
from pathlib import Path
import importlib.util,json,statistics,sys
import re
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];groups=sys.argv[3:] or ['2-720','3-720','2-sr','3-sr'];assert all(g in ('2-720','3-720','2-sr','3-sr') for g in groups)
folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False);matrix.assert_gpu_tests_idle();app=matrix.stage(variant,label)
payload={str(path.relative_to(app)):matrix.digest(path) for path in app.rglob('*') if path.is_file() and path.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8');results=[]
for group in groups:
 setting='LOW-'+group;sr=group.endswith('-sr');count=int(group[0]);matrix.CONFIGS[setting]={'layers':count,'policies':[0 if sr else 3]*count,'temporal':False,'sr':sr}
 for repeat in range(1,4):
  for mode in (('off','on') if repeat%2 else ('on','off')):
   name=f'{label}-{group}-{mode}-r{repeat}';print('START',name,flush=True)
   matrix.run(variant,'M1',setting,name,50,gpuPriority=2,testEnv={'VEYRA_TEST_NR_LOW_CHAIN':'1'} if mode=='on' else {},stageLabel=label)
   receipt=json.loads((BASE/'logs'/TASK/name/'result.json').read_text(encoding='utf-8'))
   log=(BASE/'logs'/TASK/name/'player.log').read_text(encoding='utf-8',errors='replace')
   if mode=='on':assert f'[nr-chain] lowResolution=true layers={count}' in log
   keys=('gpuNrP95Ms','gpuResidualP95Ms','enhancementProcessingMs','gpuReadyP95Ms')
   skips=[int(value) for value in re.findall(r'previewSkipped=(\d+)',log)]
   submits=[{k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)} for line in log.splitlines() if '[submit]' in line]
   submits=[s for s in submits if 100000000<=s.get('pts100ns',-1)<=480000000]
   intervals=[(b['host100ns']-a['host100ns'])/10000 for a,b in zip(submits,submits[1:])];ordered=sorted(intervals);assert len(ordered)>100
   row={'name':name,'group':group,'mode':mode,'repeat':repeat,'receipt':str(BASE/'logs'/TASK/name/'result.json'),'maxPreviewSkipped':max(skips,default=0),
    'presentIntervalsMs':{'p99':ordered[int(len(ordered)*.99)],'max':max(ordered),'count':len(ordered)},'stallLines':[line for line in log.splitlines() if '[engine-stall]' in line],
    'metrics':{k:receipt['summary'][k]['medianOfRollingObservations'] for k in keys}}
   results.append(row);(folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print('LOW_CHAIN_NORMAL_RESULT',json.dumps(row),flush=True)
summary={}
for group in groups:
 summary[group]={}
 for mode in ('off','on'):
  rows=[r for r in results if r['group']==group and r['mode']==mode]
  summary[group][mode]={key:{'median':statistics.median(values),'min':min(values),'max':max(values),'runs':values} for key in rows[0]['metrics'] for values in [[r['metrics'][key] for r in rows]]}
assert all(matrix.digest(app/name)==sha for name,sha in payload.items()),'Product payload mutated';matrix.assert_gpu_tests_idle()
(folder/'comparison.json').write_text(json.dumps({'runs':results,'summary':summary,'payloadUnchanged':True,'extraGpuLoad':False},ensure_ascii=False,indent=2),encoding='utf-8');print('LOW_CHAIN_NORMAL_COMPLETE',len(results),flush=True)
