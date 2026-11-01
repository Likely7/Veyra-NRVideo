"""Analyse bounded physical-capture A/B intervals, not sampled log counts as totals."""
from pathlib import Path
import json,re,statistics,datetime
B=Path('E:/项目/Veyra');T='fg-scheduling-repair-20261024';O=B/'reports'/T
results=[]
for receipt in sorted(O.glob('capture-*.json')):
    r=json.loads(receipt.read_text('utf8'))
    if 'case' not in r:continue
    lines=(Path(r['log'])/'engine.log').read_text('utf8',errors='replace').splitlines()
    begin=next(l[:24] for l in lines if 'CAPTURE_FG_BEGIN ' in l);end=next(l[:24] for l in lines if 'CAPTURE_FG_END ' in l)
    window=[l for l in lines if begin<=l[:24]<=end]
    def events(tag):
        return [dict(re.findall(r'(\w+)=([^\s]+)',l),time=l[:24]) for l in window if '['+tag+']' in l]
    flow=events('frame-flow');timing=events('capture-timing');admit=events('live-fg-admission');query=events('display-query')
    assert len(flow)>20 and len(timing)>20 and r['passed'],r
    first,last=flow[0],flow[-1];assert first['revision']==last['revision'] and first['session']==last['session']
    keys=['captureReceived','mailboxOverwritten','sourceAccepted','sourceSkippedBeforeGraph','realSubmitted','fgCandidate','fgSkippedBeforeEval','fgEvaluated','fgWarmup','fgSkippedForReset','fgReadyValid','fgInvalid','realPresented','generatedPresented','generatedExpiredAfterEval','cancelledBeforePresent']
    delta={k:int(last[k])-int(first[k]) for k in keys}
    rejects=[d for d in admit if d.get('admitted')=='false']
    def values(items,k):return [float(d[k]) for d in items if k in d]
    fps=values(timing,'callbackFps');elapsed=values(admit,'elapsedMs')
    configured=any('[capture] configured 2560x1440 nominalFps=60.000 upstreamSubtype=0x3231564E' in l for l in lines)
    connected=any('native ConnectDirect subtype=0x3231564E hr=0x00000000 converters=0' in l for l in lines)
    running=any('[capture] Run hr=0x0 actual=2560x1440 nominalFps=60.000' in l for l in lines)
    assert configured and connected and running,'actual NV12 capture contract missing'
    data=dict(case=r['case'],baseline=r['baseline'],half=r['half'],exeSha256=r['exeSha256'],measurementBegin=begin,measurementEnd=end,counterBegin=first['time'],counterEnd=last['time'],delta=delta,rejectionRatio=delta['fgSkippedBeforeEval']/max(1,delta['fgCandidate']),callbackFpsMin=min(fps),callbackFpsMax=max(fps),callbackFpsMean=statistics.mean(fps),sampledAdmissionCount=len(admit),sampledRejections=len(rejects),sampledElapsedMaxMs=max(elapsed),sampledRejectedElapsedMs=values(rejects,'elapsedMs'),graphCpuP95MaxMs=max(values(timing,'processCpuP95Ms')),fgGpuP95MaxMs=max(values(timing,'gpuFgBatchP95Ms')),asyncQueries=len(query),asyncVrrMaxMs=max(values(query,'vrrMs'),default=0),actualFormat='2560x1440p60 NV12, direct connection, no converters',screenshots=[str(p) for p in Path(r['profile']).glob('*.png')])
    results.append(data)
    data['counterSeconds']=(datetime.datetime.fromisoformat(last['time'].replace('Z','+00:00'))-datetime.datetime.fromisoformat(first['time'].replace('Z','+00:00'))).total_seconds()
(O/'capture-comparison.json').write_text(json.dumps(results,ensure_ascii=False,indent=1),'utf8')
for r in results:print(json.dumps({k:r[k] for k in ['case','delta','callbackFpsMean','sampledElapsedMaxMs','asyncQueries','asyncVrrMaxMs']},ensure_ascii=False))
