"""Read-only analysis of production VFG logs; no GPU work or app mutations."""
from pathlib import Path
from datetime import datetime
import json, re, statistics

BASE=Path('E:/项目/Veyra/logs/vfg-diagnosis-20261007')
def fields(text):
 result={}
 for k,v in re.findall(r'([A-Za-z][A-Za-z0-9]*)=([^\s]+)',text):result.setdefault(k,v)
 return result
def numbers(row):
 result={}
 for k,v in row.items():
  try:result[k]=float(v)
  except ValueError:result[k]=v
 return result
def percentile(values,q):
 return sorted(values)[min(len(values)-1,int((len(values)-1)*q))] if values else None

output=[]
for path in sorted(BASE.glob('*/result.json')):
 result=json.loads(path.read_text(encoding='utf8'))
 samples=json.loads(path.with_name('samples.json').read_text(encoding='utf8'))
 if not samples:continue
 first,last=samples[0]['wall'],samples[-1]['wall']
 records=[];present=[];timing=[];batches={};offsets=[];last_pacing=None;closed={};admissions=[]
 for line in path.with_name('player.log').read_text(encoding='utf8',errors='replace').splitlines():
  try:utc=datetime.fromisoformat(line[:24].replace('Z','+00:00')).timestamp()*1000
  except ValueError:continue
  row=numbers(fields(line));row['utcMs']=utc
  if '[pacing-sample]' in line:last_pacing=row
  if '[submit]' in line and 'host100ns' in row:
   offsets.append(utc-row['host100ns']/10000)
   if last_pacing and row['pts100ns']==last_pacing.get('pts'):
    last_pacing['batch']=int(row['batch']);present.append(last_pacing);last_pacing=None
  if '[frame-batch]' in line:batches[int(row['batch'])]=row
  if first<=utc<=last:
   if '[player-timing]' in line:timing.append(row)
   if '[live-fg-admission]' in line:admissions.append(row)
  if '[frame-flow]' in line and row.get('state')=='closed':closed=row
 offset=statistics.median(offsets) if offsets else 0
 starts={int(p['batch']):p['process'] for p in present}
 intervals=[]
 for batch,start in starts.items():
  if batch in batches:
   b=batches[batch];end=(b['utcMs']-offset)*10000
   if first<=b['utcMs']<=last:intervals.append({'batch':batch,'start':start,'end':end,'ms':(end-start)/10000})
 steady=[p for p in present if first<=p['utcMs']<=last]
 gaps=[];examples=[]
 target=1000/(30*result['config']['multiplier'])
 for a,b in zip(steady,steady[1:]):
  gap=(b['begin']-a['begin'])/10000;gaps.append(gap)
  if gap<target*2:continue
  matches=[]
  for i in intervals:
   overlap=max(0,min(b['begin'],i['end'])-max(a['end'],i['start']))/10000
   if overlap>gap*.8 and 0<b['ready']<i['start']:
    matches.append({'processingBatch':i['batch'],'processingMs':i['ms'],'overlapMs':overlap,'nextFrameReadyBeforeProcessMs':(i['start']-b['ready'])/10000})
  if matches:examples.append({'presentGapMs':gap,'previousPtsMs':a['pts']/10000,'nextPtsMs':b['pts']/10000,'presentedBatch':b['batch'],'nextLateMs':b.get('lateMs'),**max(matches,key=lambda m:m['overlapMs'])})
 timing_summary={}
 if len(timing)>1:
  a,b=timing[0],timing[-1];seconds=(b['utcMs']-a['utcMs'])/1000
  timing_summary={'seconds':seconds,'medianReportedWindowP95Ms':{k:statistics.median([r[k] for r in timing]) for k in ('decodeP95Ms','graphSubmitP95Ms','gpuReadyP95Ms','presentP95Ms','entryAbsP95Ms')},'cumulativeDeltas':{k:b[k]-a[k] for k in ('displaySubmits','expiredGenerated','previewSkipped','processed','slotWaits')},'playbackSpeedRange':[min(r['playbackSpeed'] for r in timing),max(r['playbackSpeed'] for r in timing)]}
 admission={k:[min(r[k] for r in admissions if k in r),max(r[k] for r in admissions if k in r)] for k in ('admittedPairs','rejectedPairs','predictedMs','remainingDeadlineMs','firstDeadlineMs') if any(k in r for r in admissions)}
 meters=json.loads(path.with_name('meters.json').read_text(encoding='utf8'))
 analysis={'label':path.parent.name,'result':result,'closedCounters':closed,'timing':timing_summary,'admissionRanges':admission,'foregroundOwnedSamples':sum(bool(m['foregroundAlreadyOwned']) for m in meters),'foregroundSamples':len(meters),'submissionCadenceMs':{'target':target,'median':statistics.median(gaps) if gaps else None,'p95':percentile(gaps,.95),'max':max(gaps) if gaps else None},'graphCpuIntervalMsApproxUtcMapping':{'samples':len(intervals),'median':statistics.median(i['ms'] for i in intervals) if intervals else None,'p95':percentile([i['ms'] for i in intervals],.95)},'longGapsWithReadyFrameBlockedByNextGraph':len(examples),'examples':examples[:5]}
 path.with_name('analysis.json').write_text(json.dumps(analysis,ensure_ascii=False,indent=2),encoding='utf8')
 output.append(analysis)
 print(path.parent.name, 'fps',result['fps'],'total',round(result['totalMs'],2),'graphCpu',timing_summary.get('medianReportedWindowP95Ms',{}).get('graphSubmitP95Ms'),'blockedReadyGaps',len(examples))
(BASE/'analysis.json').write_text(json.dumps(output,ensure_ascii=False,indent=2),encoding='utf8')
