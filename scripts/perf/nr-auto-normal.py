"""Ordinary playback only: actual-size auto NR versus the same build off."""
from pathlib import Path
import contextlib,importlib.util,json,re,statistics,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];repeats=int(sys.argv[3]) if len(sys.argv)>3 else 3
materials=sys.argv[4:] or ['M1','M10-1080p60-synthetic'];assert 1<=repeats<=3
folder=BASE/'logs'/TASK/(label+'-summary');folder.mkdir(parents=True,exist_ok=False)
matrix.assert_gpu_tests_idle();app=matrix.stage(variant,label)
payload={p.relative_to(app).as_posix():matrix.digest(p) for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe','.dll','.qml')}
(folder/'artifacts.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
matrix.CONFIGS['AUTO-NR2']={'layers':2,'policies':[0,0],'temporal':False,'sr':False}
results=[]
for material in materials:
 for repeat in range(1,repeats+1):
  for mode in (('off','auto') if repeat%2 else ('auto','off')):
   name=f'{label}-{material}-{mode}-r{repeat}';print('START',name,flush=True)
   with (folder/(name+'-driver.log')).open('x',encoding='utf-8') as stream,contextlib.redirect_stdout(stream):
    matrix.run(variant,material,'AUTO-NR2',name,50,gpuPriority=2,stageLabel=label,
               testEnv={'VEYRA_TEST_NR_AUTO_POOL':'1'} if mode=='auto' else {})
   receiptPath=BASE/'logs'/TASK/name/'result.json';receipt=json.loads(receiptPath.read_text(encoding='utf-8'))
   lines=(receiptPath.parent/'player.log').read_text(encoding='utf-8',errors='replace').splitlines()
   pool=[line for line in lines if '[nr-auto-pool]' in line];control=[line for line in lines if '[nr-auto]' in line]
   assert bool(pool)==(mode=='auto'),pool
   if mode=='auto':assert any('event=ready' in line for line in pool)
   submits=[(i,{k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',line)}) for i,line in enumerate(lines) if '[submit]' in line]
   steady=[s for _,s in submits if 100000000<=s.get('pts100ns',-1)<=480000000]
   gaps=sorted((b['host100ns']-a['host100ns'])/10000 for a,b in zip(steady,steady[1:]));assert len(gaps)>100
   switches=[]
   for i,line in enumerate(lines):
    if '[nr-auto]' not in line or 'boundaryMs=' not in line:continue
    before=next((s for j,s in reversed(submits) if j<i),None);after=next((s for j,s in submits if j>i),None)
    switches.append({'line':line,'previousPresent':before,'nextPresent':after,
                     'presentGapMs':(after['host100ns']-before['host100ns'])/10000 if before and after else None})
   meters=json.loads((receiptPath.parent/'meters.json').read_text(encoding='utf-8'))
   row={'name':name,'material':material,'mode':mode,'repeat':repeat,'receipt':str(receiptPath),
        'sourceCommit':receipt['sourceCommit'],'exeSha256':receipt['exeSha256'],'sourceSha256':receipt['sourceSha256'],
        'metrics':{k:receipt['summary'][k]['medianOfRollingObservations'] for k in ('enhancementProcessingMs','gpuNrP95Ms','gpuFlowP95Ms','gpuReadyP95Ms')},
        'presentP99Ms':gaps[int(len(gaps)*.99)],'presentMaxMs':max(gaps),
        'maxPreviewSkipped':max([int(v) for line in lines for v in re.findall(r'previewSkipped=(\d+)',line)],default=0),
        'switches':switches,'poolEvents':pool,'controllerEvents':control,
        'wddmMemoryLines':[line for line in lines if '[vram-watch]' in line],
        'cpuOneCoreMedianPercent':statistics.median(m['cpuPercentOneCoreScale'] for m in meters),
        'stallLines':[line for line in lines if '[engine-stall]' in line]}
   results.append(row);(folder/'completed.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
   print('AUTO_NORMAL_RESULT',json.dumps({k:row[k] for k in ('name','metrics','presentP99Ms','presentMaxMs','maxPreviewSkipped','switches')},ensure_ascii=False),flush=True)
summary={}
for material in materials:
 for mode in ('off','auto'):
  rows=[r for r in results if r['material']==material and r['mode']==mode]
  values={key:[r['metrics'][key] for r in rows] for key in rows[0]['metrics']}
  values.update({key:[r[key] for r in rows] for key in ('presentP99Ms','presentMaxMs','maxPreviewSkipped','cpuOneCoreMedianPercent')})
  summary[material+'-'+mode]={k:{'median':statistics.median(v),'min':min(v),'max':max(v),'runs':v} for k,v in values.items()}
assert all(matrix.digest(app/name)==sha for name,sha in payload.items());matrix.assert_gpu_tests_idle()
(folder/'summary.json').write_text(json.dumps({'runs':results,'summary':summary,'payloadUnchanged':True,'extraGpuLoad':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('AUTO_NORMAL_COMPLETE',len(results),flush=True)
