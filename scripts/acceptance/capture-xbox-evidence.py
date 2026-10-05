"""Summarize immutable field inputs without publishing SDP, credentials or the dump."""
from pathlib import Path
from datetime import datetime
import hashlib, json, re, statistics

BASE=Path('E:/项目/Veyra'); TASK='capture-xbox-field-20261005'
LOG=BASE/'logs'/TASK; INPUT=LOG/'input'
inputs={
 'logs(4).7z':'bb7fc5ff2663a0553415b3af7bdd7564a2de3af8bb52bb014496c45956487efb',
 'veyra-qml(26).log':'111743001597ff1ba82442f68e9a557e651505df46022a49ded8448e6255d9a2',
 'veyra-qml(28).log':'458848912364339a592190f86b7ee772016ce7b691a52ed7c5db20c38981180a',
 'archive/logs/veyra-qml.log':'cc9af8a15440a62d26608f0cb92a71c0b2a0cbf2921fec7597e0a3af21d8cba8'}
manifest=[]
for rel,expected in inputs.items():
    p=INPUT/rel; digest=hashlib.sha256(p.read_bytes()).hexdigest()
    assert digest==expected,rel
    manifest.append({'path':rel,'bytes':p.stat().st_size,'sha256':digest})
rx=re.compile(r'^(\S+) t=(\d+) \[(.*?)\] \[([^]]+)\] (.*)$')
kv=re.compile(r'(\w+)=([^\s]+)')
def number(v):
    try:return float(v) if '.' in v else int(v)
    except ValueError:return v
def rows(path):
    session=0
    for n,line in enumerate(path.read_text(encoding='utf8',errors='replace').splitlines(),1):
        m=rx.match(line)
        if not m:continue
        stamp,thread,level,tag,message=m.groups()
        if 'Veyra QML session started' in message:session+=1
        yield {'line':n,'stamp':stamp,'session':session,'level':level.strip(),'tag':tag,
               'message':message,**{k:number(v) for k,v in kv.findall(message)}}
def median(data,key):return statistics.median(r[key] for r in data if isinstance(r.get(key),(int,float)))
capture=list(rows(INPUT/'archive/logs/veyra-qml.log'))
timings=[r for r in capture if r['tag']=='capture-timing' and r['session']==2 and r.get('revision')==16]
rates=[r for r in capture if r['tag']=='frame-rate' and r['session']==2 and r.get('revision')==16]
callbacks=[r for r in capture if r['tag']=='capture-callback']
assert len(timings)==len(rates)==59
a,b=timings[3],timings[-1]
seconds=(datetime.fromisoformat(b['stamp'].replace('Z','+00:00'))-datetime.fromisoformat(a['stamp'].replace('Z','+00:00'))).total_seconds()
delta={key:b[key]-a[key] for key in ('received','processed','dropped')}
capture_summary={
 'windows':len(timings),'windowFrom':timings[0]['stamp'],'windowTo':b['stamp'],
 'callbackFpsMedian':median(timings,'callbackFps'),'gpuCompletedFpsMedian':median(rates,'gpuCompletedFps'),
 'rollingGpuReadyP95MedianMs':median(timings,'gpuReadyP95Ms'),
 'callbackArrivalMedianMs':median(callbacks,'arrivalDeltaMs'),'sourcePtsDeltaMedianMs':median(callbacks,'ptsDeltaMs'),
 'sampleDurationMedianMs':median(callbacks,'sampleDurationMs'),
 'counterWindow':{'first':{key:a[key] for key in ('line','stamp','received','processed','dropped')},
                  'last':{key:b[key] for key in ('line','stamp','received','processed','dropped')},
                  'seconds':seconds,'deltas':delta,'receivedPerSecond':delta['received']/seconds,
                  'processedPerSecond':delta['processed']/seconds},
 'conclusion':'The source continued near 60 Hz; enhancement consumption was below source rate. P95 values are not mean per-frame times.'}
xbox=list(rows(INPUT/'veyra-qml(28).log'))
errors=[r for r in xbox if r['level']=='ERROR']
decoder_errors=[r for r in errors if 'decoder: send_packet failed code=-22 text=Invalid argument' in r['message']]
assert len(errors)==len(decoder_errors)==487
safe=lambda r:{key:r[key] for key in ('line','stamp','tag','message')}
fg=[safe(r) for r in xbox if r['tag'] in ('fsr-fg','xess-fg') and any(s in r['message'] for s in ('independent context','initialized','created','version='))]
stalls=[safe(r) for r in xbox if r['tag']=='engine-stall' and r.get('totalMs',0)>=1000]
xbox_summary={'hardDecodeErrors':len(errors),'firstError':safe(errors[0]),'lastError':safe(errors[-1]),
 'fgProviderEvidence':fg[:16],'stallEvidence':stalls[:8],
 'conclusion':'The field log proves persistent H.264 decode failure and failed recovery. It does not establish which AMD driver/FG/bitstream condition triggered EINVAL.'}
result={'inputs':manifest,'capture':capture_summary,'xbox':xbox_summary,
        'scope':'28 is the confirmed Xbox report; 26 is retained only as superseded evidence.'}
(LOG/'field-evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print(json.dumps({'capture':capture_summary,'xbox':{k:v for k,v in xbox_summary.items() if k not in ('fgProviderEvidence','stallEvidence')}},ensure_ascii=False,indent=2))
