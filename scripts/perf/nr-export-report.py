"""Summarize matched ordinary exports, separating complete job and processing costs."""
from pathlib import Path
import json,re,statistics,sys
BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004';label=sys.argv[1]
folder=BASE/'logs'/TASK/(label+'-summary');data=json.loads((folder/'summary.json').read_text(encoding='utf-8'));rows=[]
for run in data['runs']:
 assert run['passed'];line=run['pipeline'][-1];values=dict(re.findall(r'(\w+)=([-\w.]+)',line))
 expectedMode='async' if run['mode'] in ('async','async-events') and run['group']!='fg2' else 'serial'
 assert values['mode']==expectedMode,(run['name'],values)
 assert values['fenceEvents']==('true' if run['mode'] in ('events','async-events') else 'false'),(run['name'],values)
 assert int(values['maxInFlight'])==(2 if expectedMode=='async' else 1)
 log=(BASE/'logs'/TASK/run['name']/'engine.log').read_text(encoding='utf-8',errors='replace')
 layers=2 if run['group']=='nr2' else 1 if run['group'] in ('nr','nrsr4k') else 0
 assert log.count('snippet CreateFeature id=18 result=0x1')==layers,(run['name'],'NR creation count')
 assert '[ERROR]' not in log,run['name']
 rows.append({'name':run['name'],'group':run['group'],'mode':run['mode'],'repeat':run['repeat'],'totalMs':run['metrics']['totalMs'],
  'pipelineMs':float(values['pipelineMs']),'completionWaitMs':float(values['completionWaitMs']),'maxInFlight':int(values['maxInFlight']),
  'outputSha256':run['outputSha256'],'beforeGpu':run['beforeGpu'],'afterGpu':run['afterGpu']})
summary={}
for group in dict.fromkeys(r['group'] for r in rows):
 summary[group]={}
 for mode in dict.fromkeys(r['mode'] for r in rows if r['group']==group):
  current=[r for r in rows if r['group']==group and r['mode']==mode];summary[group][mode]={}
  for key in ('totalMs','pipelineMs','completionWaitMs'):
   values=[r[key] for r in current];summary[group][mode][key]={'median':statistics.median(values),'min':min(values),'max':max(values),'runs':values}
 for mode in summary[group]:
  if mode!='serial':
   for key in ('totalMs','pipelineMs'):
    summary[group][mode][key]['reductionPercent']=100*(1-summary[group][mode][key]['median']/summary[group]['serial'][key]['median'])
path=folder/'comparison.json';assert not path.exists();path.write_text(json.dumps({'runs':rows,'summary':summary,'payloadUnchanged':data['payloadUnchanged'],'extraGpuLoad':False,
 'note':'Whole export includes initialization/mux save. Pipeline starts after encoder/header and includes final drain. Fence events and two-in-flight are independent.'},ensure_ascii=False,indent=2),encoding='utf-8')
for group,modes in summary.items():
 for mode,v in modes.items():print(group,mode,'totalMs=',round(v['totalMs']['median'],3),'pipelineMs=',round(v['pipelineMs']['median'],3),'totalReduction=',round(v['totalMs'].get('reductionPercent',0),2),'pipelineReduction=',round(v['pipelineMs'].get('reductionPercent',0),2))
