"""Summarize actual timer samples and focus states without claiming a root cause."""
import json
from pathlib import Path
import statistics

BASE=Path('E:/项目/Veyra');TASK='playback-smoothness-20261004'
logs=BASE/'logs'/TASK
labels=['baseline-202-plain','baseline-203-plain','candidate-plain-v1',
        'baseline-203-heavy','baseline-203-dlss6','candidate-dlss6-v1',
        'baseline-203-software','candidate-software-v2-final','candidate-vfg4-v2-final']
report={'sampleWindowSeconds':[12,140],
        'metrics':'Qt 16ms Timer interval; engine submitted FPS, not screen scanout',
        'backgroundRootCauseFound':False,
        'limitations':[
            'Baseline203 plain later samples overlapped the initial build; not a fair isolated worst-case comparison.',
            'Active/inactive warmup and unequal counts prevent attributing every difference to focus.',
            'No physical screen latency/refresh measurement or AMD real-hardware validation.',
            'Computer Use stopped by user; final Qt integration tests send no desktop input.'
        ],'comparisons':[]}
for label in labels:
    path=logs/label/'player.log'
    if not path.exists():continue
    text=path.read_text(encoding='utf8',errors='replace')
    samples=[]
    for line in text.splitlines():
        if 'SMOOTH_SAMPLE ' not in line:continue
        sample=json.loads(line.split('SMOOTH_SAMPLE ',1)[1])
        if 12<=sample['seconds']<=140 and sample['running'] and not sample['failed']:
            samples.append(sample)
    groups={}
    for active in (True,False):
        chosen=[s for s in samples if s['active']==active]
        if not chosen:continue
        groups['foreground' if active else 'background']={
            'samples':len(chosen),
            **{k:statistics.median(s[k] for s in chosen) for k in ('p50','p95','fps','fgNotifications')},
            'effects':{k:any(s[k] for s in chosen) for k in ('nr','sr','fg')},
            'visibility':sorted(set(s['visibility'] for s in chosen))}
    getter=[line.split('SMOOTH_GETTER_100 ',1)[1] for line in text.splitlines() if 'SMOOTH_GETTER_100 ' in line]
    report['comparisons'].append(dict(label=label,log=str(path),groups=groups,getter100=getter))
report['nativeTests']=json.loads((logs/'native-tests.json').read_text(encoding='utf8'))
report['functional']=json.loads((BASE/'tests'/TASK/'functional-v5-final/result.json').read_text(encoding='utf8'))
report['layoutFullscreen']=json.loads((BASE/'tests'/TASK/'layout-fullscreen-v4-final/result.json').read_text(encoding='utf8'))
report['capabilityReplacement']=json.loads((BASE/'tests'/TASK/'capability-v1/result.json').read_text(encoding='utf8'))
report['displayEvents']=json.loads((logs/'candidate-display-v1/display-result.json').read_text(encoding='utf8'))
out=logs/'acceptance-summary.json'
out.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print(json.dumps(report['comparisons'],ensure_ascii=False,indent=2))
print(out)
