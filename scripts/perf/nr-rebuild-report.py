"""Compare immutable per-run receipts, rebuild samples and complete frame hashes."""
from pathlib import Path
import csv
import json
import statistics
import sys

BASE=Path('E:/项目/Veyra/logs/perf-nr-20261004')
labels=sys.argv[1:4]
assert len(labels)==3 and all(x.replace('-','').isalnum() for x in labels)
groups=('nr','sr','layers','sizes')
all_runs={label:json.loads((BASE/(label+'-summary.json')).read_text(encoding='utf-8')) for label in labels}
def aggregate(samples):
    return {'medianOfRunMedians':statistics.median(samples),'minRunMedian':min(samples),'maxRunMedian':max(samples)}
report={'labels':labels,'groups':{},'measurement':'CPU creation includes necessary GPU waits; no actual Present; full-frame hashes'}
passed=True
for group in groups:
    reference=next(r for r in all_runs[labels[0]] if r['group']==group and r['repeat']==1)
    variants={}
    for label,runs in all_runs.items():
        rr=[r for r in runs if r['group']==group]
        assert len(rr)==3
        measurements=[];differing=0;checks=[]
        for run in rr:
            assert run['passed'] and run['sourceSha256']==reference['sourceSha256'] and run['nrSha256']==reference['nrSha256']
            differing+=sum(run['frameHashes'][name]!=sha for name,sha in reference['frameHashes'].items())
            out=BASE/f"{label}-{group}-r{run['repeat']}"
            rows=list(csv.DictReader((out/'rebuild.csv').open()))
            assert len(rows)==20
            # First creation remains reported in raw/all-cycle samples, but
            # a warm-switch comparison must not hide its separate cold cost.
            subsets={'all':rows,'warm':rows[1:]}
            if group=='nr':subsets.update({'nr-on':[r for r in rows if int(r['nr'])],
                                         'nr-on-warm':[r for r in rows[1:] if int(r['nr'])],
                                         'nr-off':[r for r in rows if not int(r['nr'])]})
            measurements.append({name:{key:statistics.median(float(r[key]) for r in selected)
                                   for key in ('createMs','destroyMs','totalMs','vramLive','vramAfter')}
                                 for name,selected in subsets.items()})
            engine=(out/'engine.log').read_text(encoding='utf-8',errors='replace')
            checks.append({'repeat':run['repeat'],'coreInitCalls':engine.count('core-host: Init_with_ProjectID result='),
                           'coreShutdownCalls':engine.count('core-host: Shutdown1 result='),
                           'cacheHits':engine.count('[ngx-core-cache] event=hit'),
                           'leakedParameterWarnings':engine.count('leaked parameter block'),
                           'coldCreateMs':float(rows[0]['createMs']),
                           'vramAfterLastCycleBytes':int(rows[-1]['vramAfter'])})
        metrics={name:{key:aggregate([m[name][key] for m in measurements]) for key in measurements[0][name]}
                 for name in measurements[0]}
        variants[label]={'metrics':metrics,'differingFramesAgainstA':differing,'comparedFrames':60,
                         'identicalPixelsPsnr':'inf' if differing==0 else 'requires per-pixel comparison',
                         'identicalPixelsSsim':1 if differing==0 else None,'lifecycle':checks}
        passed&=differing==0 and all(c['leakedParameterWarnings']==0 for c in checks)
    report['groups'][group]=variants
report['pixelAndParameterChecksPassed']=passed
out=BASE/(labels[1]+'-comparison.json')
with out.open('x',encoding='utf-8') as f:json.dump(report,f,ensure_ascii=False,indent=2)
for g,v in report['groups'].items():
    subset='nr-on-warm' if g=='nr' else 'warm'
    print(g,{label:{'create':r['metrics'][subset]['createMs'],'differentFrames':r['differingFramesAgainstA'],
                   'coreInitCalls':[c['coreInitCalls'] for c in r['lifecycle']]} for label,r in v.items()})
print('PIXEL_PARAMETER_CHECK',passed,out)
raise SystemExit(0 if passed else 1)
