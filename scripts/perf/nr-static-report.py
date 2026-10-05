"""Verify A-A noise before judging a frozen-output duplicate reuse candidate."""
from pathlib import Path
import csv
import json
import statistics
import sys
base=Path('E:/项目/Veyra/logs/perf-nr-20261004');label=sys.argv[1]
runs=json.loads((base/(label+'-summary.json')).read_text(encoding='utf-8'))
report={'label':label,'modes':{},'candidate':'Reuse first accepted complete output on exact identical inputs; no product change'}
for mode in sorted(set(r['mode'] for r in runs)):
    rr=[r for r in runs if r['mode']==mode];assert len(rr)==3 and all(r['safetyPassed'] for r in rr)
    noise=[sum(a!=b for a,b in zip(rr[0]['sha256Sequence'],r['sha256Sequence'])) for r in rr]
    rows=list(csv.DictReader((base/f'{label}-{mode}-r1'/'frames.csv').open()))
    report['modes'][mode]={'framesPerRun':300,'repeatCount':3,'differentFramesAcrossIdenticalARepeats':noise,
                          'candidateDifferentFramesToA':[r['differentOutputFramesToFirst'] for r in rr],
                          'last100ChangedFromPrevious':sum(r['equalPrevious']=='0' for r in rows[-100:]),
                          'maeToFirstMedianByteScale':statistics.median(float(r['maeToFirst']) for r in rows),
                          'exeSha256':rr[0]['exeSha256'],'nrSha256':rr[0]['nrSha256'],'sourceSha256':rr[0]['sourceSha256']}
assert all(not any(x['differentFramesAcrossIdenticalARepeats']) for x in report['modes'].values())
report['unchangedOutputContractPassed']=all(not any(x['candidateDifferentFramesToA']) for x in report['modes'].values())
with (base/(label+'-comparison.json')).open('x',encoding='utf-8') as f:json.dump(report,f,ensure_ascii=False,indent=2)
print(json.dumps(report,ensure_ascii=False,indent=2))
# A valid counterexample is a successfully measured experiment.
