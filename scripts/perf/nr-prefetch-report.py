"""Calibrated cross-queue dependency overlap, separate from untraced performance."""
from pathlib import Path
import json,re,statistics as st,sys
BASE=Path('E:/项目/Veyra/logs/perf-nr-20261004');label=sys.argv[1]
summary=json.loads((BASE/(label+'-summary/summary.json')).read_text(encoding='utf-8'));results=[]
for run in summary['runs']:
 log=(BASE/run['name']/'engine.log').read_text(encoding='utf-8',errors='replace');clocks={};timings=[]
 for line in log.splitlines():
  fields=dict(re.findall(r'(\w+)=(\S+)',line))
  if '[prefetch-clock]' in line:
   assert fields['frequencyHr']==fields['calibrationHr']=='0x0';clocks.setdefault(fields['role'],[]).append({k:int(fields[k]) for k in ('frequency','gpu','cpu','qpcFrequency','uncertaintyTicks')})
  if '[prefetch-gpu]' in line:timings.append({'role':fields['role'],**{k:int(fields[k]) for k in ('source','stage','begin','end','frequency')}})
 assert all(len(clocks.get(role,[]))==2 for role in ('producer','consumer')),clocks
 origin=min(c[0]['cpu'] for c in clocks.values());uncertainty={};drift={}
 for role,pair in clocks.items():
  a,b=pair;assert a['frequency']==b['frequency'] and a['qpcFrequency']==b['qpcFrequency'];assert a['frequency']>0 and a['qpcFrequency']>0
  drift[role]=abs((b['gpu']-a['gpu'])/a['frequency']-(b['cpu']-a['cpu'])/a['qpcFrequency'])*1000
  uncertainty[role]=sum(c['uncertaintyTicks']/c['qpcFrequency']*1000 for c in pair)+drift[role]
 def mapped(item,key):
  c=clocks[item['role']][0];assert item['frequency']==c['frequency']
  return (c['cpu']-origin)/c['qpcFrequency']*1000+(item[key]-c['gpu'])/c['frequency']*1000
 flow={t['source']:t for t in timings if t['role']=='producer' and t['stage']==2}
 enhanced={}
 # Sr=1, NrLayer0=15, NrLayer1=16 in the current declared enum. No NR-last shorthand.
 for t in timings:
  if t['role']=='consumer' and t['stage'] in (1,15,16):enhanced.setdefault(t['source'],[]).append(t)
 bound=sum(uncertainty.values());pairs=[]
 for source,f in flow.items():
  prior=enhanced.get(source-1)
  if not prior:continue
  a,b=mapped(f,'begin'),mapped(f,'end');overlap=sum(max(0,min(b,mapped(n,'end'))-max(a,mapped(n,'begin'))) for n in prior)
  pairs.append({'nextSource':source,'previousSource':source-1,'flowDependencyMs':b-a,'overlapMs':overlap,'exceedsCalibrationBound':overlap>bound})
 assert len(pairs)>80,len(pairs)
 results.append({'name':run['name'],'group':run['group'],'calibration':clocks,'driftMs':drift,'estimatedCalibrationBoundMs':bound,
  'pairs':pairs,'measuredPairs':len(pairs),'positivePairsBeyondBound':sum(p['exceedsCalibrationBound'] for p in pairs),
  'medianOverlapMs':st.median(p['overlapMs'] for p in pairs),'maxOverlapMs':max(p['overlapMs'] for p in pairs),
  'note':'GPU Flow dependency interval includes submit/queue-wait gaps; overlap is not an isolated NVOF hardware-kernel timing. Trace wall-time excluded from performance comparison.'})
report={'traceLabel':label,'runs':results,'noPressure':True,'source':'https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing'}
dest=BASE/(label+'-summary/overlap.json');assert not dest.exists();dest.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
for r in results:print(json.dumps({k:v for k,v in r.items() if k not in ('pairs','calibration')},ensure_ascii=False))
