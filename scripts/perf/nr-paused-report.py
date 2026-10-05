"""Paired full-frame hashes, baseline repeatability and three-run timing."""
from pathlib import Path
import csv
import json
import statistics
import sys
BASE=Path('E:/项目/Veyra/logs/perf-nr-20261004');labels=sys.argv[1:4];assert len(labels)==3
result={'labels':labels,'groups':{},'passed':True}
for mode in ('nr','srnr','temporal','layers'):
    groups={};reference=None
    for label in labels:
        summaries=[];times=[];counts=[];differences=[];noise=[]
        for repeat in range(1,4):
            folder=BASE/f'{label}-{mode}-r{repeat}';receipt=json.loads((folder/'result.json').read_text(encoding='utf-8'))
            rows=list(csv.DictReader((folder/'edits.csv').open()));sequence=[r['sha256'] for r in rows]
            if label==labels[0] and repeat==1:reference=sequence
            differences.append(sum(a!=b for a,b in zip(reference,sequence)))
            noise.append(differences[-1] if label==labels[0] else None)
            steady=[float(r['processCpuWithWaitMs']) for r in rows if int(r['edit']) not in (0,150)]
            times.append(statistics.median(steady));counts.append(receipt['nrEvaluations'])
            summaries.append({'repeat':repeat,'passed':receipt['passed'],'exeSha256':receipt['exeSha256'],'p50Ms':statistics.median(steady),'nrEvaluations':receipt['nrEvaluations'],'differentFramesFromA-r1':differences[-1]})
            result['passed'] &= receipt['passed'] and len(sequence)==len(reference)==300 and differences[-1]==0
        groups[label]={'medianOfRunMediansMs':statistics.median(times),'minRunMedianMs':min(times),'maxRunMedianMs':max(times),'nrEvaluations':counts,'differentFrames':differences,'runs':summaries}
    result['groups'][mode]=groups;print(mode,groups,flush=True)
path=BASE/(labels[1]+'-comparison.json')
with path.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2)
print('PIXEL_PARAMETER_CHECK',result['passed'],path);raise SystemExit(0 if result['passed'] else 1)
