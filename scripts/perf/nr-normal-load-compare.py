"""Replay sealed A/B player artifacts without owned GPU competition.

B is exactly the EXE used by B3c-present-priority-v1, not the subsequently
relinked COMPUTE experiment. The matrix's usual build/stage freshness check
is deliberately replaced by explicit immutable hashes for this replay only.
"""
from pathlib import Path
import contextlib
import csv
import importlib.util
import json
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'perf-nr-20261004'
spec = importlib.util.spec_from_file_location('matrix', ROOT/'scripts/perf/nr-matrix.py')
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)
label = sys.argv[1]
assert label.replace('-', '').isalnum()
out = BASE/'logs'/TASK/(label+'-summary')
out.mkdir(exist_ok=False)

artifacts = {
    'A': {'stage': BASE/'tests'/TASK/'app-A',
          'exeSha256': 'b7081f0e6045bc791105bc80de364247e169e3522891ef50cf9f62bbbfc10eb4',
          'productSource': '8cdc612120cbf23ba116a33c3cb0a53e2043f718'},
    'B2d': {'stage': BASE/'tests'/TASK/'app-B2d',
            'exeSha256': '97dbccbfa029e4604e308c1df4198742c8e505c1510b891715f3aaf4b9afeb34',
            'productSourceNote': 'Sealed basic 3c candidate from build-compute-priority-v2; exact previous competition EXE.'},
}
runtime = 'f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc'
source = '03b2a0dc7f682a2c70ae65809d4db69c725a95f49e60116b6e9da2e2ae4610c8'
matrix.assert_gpu_tests_idle()
assert matrix.digest(matrix.SOURCES['M1']) == source
old = json.loads((BASE/'logs'/TASK/'B3c-present-priority-v1-2X-normal-r1/result.json').read_text(encoding='utf-8'))
assert old['exeSha256'] == artifacts['B2d']['exeSha256']
assert old['runtimeSha256'] == runtime and old['sourceSha256'] == source

def sealed_stage(variant, stageLabel=None):
    assert stageLabel is None
    a = artifacts[variant]
    p = a['stage']
    assert matrix.digest(p/'veyra_qml_ui.exe') == a['exeSha256']
    assert matrix.digest(p/'runtime/experimental/nvngx_dlssnr.dll') == runtime
    assert (p/'qml/Veyra/NrPerfProbe.qml').is_file()
    return p

matrix.stage = sealed_stage
manifest = {}
for variant, artifact in artifacts.items():
    app = sealed_stage(variant)
    files = [p for p in app.rglob('*') if p.is_file() and p.suffix.lower() in ('.exe', '.dll', '.qml')]
    manifest[variant] = {**artifact, 'stage': str(app),
                         'payloadSha256': {str(p.relative_to(app)): matrix.digest(p) for p in files}}
(out/'artifacts.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
matrix.CONFIGS['S4-2'] = {**matrix.CONFIGS['S4'], 'multiplier': 2}
results = []
keys = ('gpuNrP95Ms', 'gpuSrP95Ms', 'gpuFlowP95Ms', 'gpuResidualP95Ms',
        'gpuFgBatchP95Ms', 'enhancementProcessingMs', 'gpuReadyP95Ms')
for repeat, order in enumerate((('A', 'B2d'), ('B2d', 'A'), ('A', 'B2d')), 1):
    for variant in order:
        matrix.assert_gpu_tests_idle()
        name = f'{label}-{variant}-r{repeat}'
        print('NORMAL_START', name, flush=True)
        with (out/(name+'-driver.log')).open('x', encoding='utf-8') as driver:
            with contextlib.redirect_stdout(driver):
                matrix.run(variant, 'M1', 'S4-2', name, 50,
                           testEnv={'VEYRA_TEST_PRESENT_QUEUE_PRIORITY': 'normal'})
        folder = BASE/'logs'/TASK/name
        receipt = json.loads((folder/'result.json').read_text(encoding='utf-8'))
        assert receipt['passed'] and receipt['exeSha256'] == artifacts[variant]['exeSha256']
        assert receipt['config'] == old['config']
        log = (folder/'player.log').read_text(encoding='utf-8', errors='replace')
        assert 'requested=COMPUTE' not in log
        if variant == 'B2d':
            actual = [int(v) for v in re.findall(r'actualType=0 actualPriority=(\d+)', log)]
            assert actual and set(actual) == {0}
            assert 'requested=2 actual=2' in log
        submitted = []
        for line in log.splitlines():
            if '[submit]' not in line:
                continue
            row = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}
            if 100000000 <= row.get('pts100ns', -1) <= 480000000:
                submitted.append(row)
        intervals = [(b['host100ns']-a['host100ns'])/10000 for a, b in zip(submitted, submitted[1:])]
        assert len(intervals) > 100
        ordered = sorted(intervals)
        pct = lambda fraction: ordered[min(len(ordered)-1, int(len(ordered)*fraction))]
        result = {'variant': variant, 'repeat': repeat, 'name': name,
                  'receipt': str(folder/'result.json'), 'exeSha256': receipt['exeSha256'],
                  'metrics': {k: receipt['summary'][k]['medianOfRollingObservations'] for k in keys},
                  'presentIntervalsMs': {'p50': pct(.5), 'p95': pct(.95), 'p99': pct(.99), 'max': max(intervals)},
                  'previewSkipped': receipt['summary']['previewSkipped'],
                  'expiredGenerated': receipt['summary']['expiredGenerated']}
        results.append(result)
        (out/'completed.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
        print('NORMAL_RESULT', name, json.dumps(result, ensure_ascii=False), flush=True)

summary = {}
for variant in artifacts:
    rows = [r for r in results if r['variant'] == variant]
    summary[variant] = {}
    for key in keys:
        values = [r['metrics'][key] for r in rows]
        summary[variant][key] = {'median': statistics.median(values), 'min': min(values),
                                 'max': max(values), 'runs': values}
comparison = {key: 100*(summary['B2d'][key]['median']/summary['A'][key]['median']-1)
              for key in keys if summary['A'][key]['median'] > 0}
result = {'runs': results, 'summary': summary, 'BChangePercent': comparison,
          'competition': 'No owned competition process; external apps left untouched.',
          'measurement': 'Per-run medians of steady rolling statistics, not aggregate percentiles or scanout. NR is last layer P95; enhancement is merged interval mean.',
          'previousPressureSummary': str(BASE/'logs'/TASK/'B3c-present-priority-v1-summary.json')}
(out/'comparison.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
with (out/'per-run.csv').open('x', encoding='utf-8-sig', newline='') as f:
    writer = csv.DictWriter(f, fieldnames=['variant', 'repeat', *keys, 'presentP99Ms', 'receipt'])
    writer.writeheader()
    for r in results:
        writer.writerow({'variant': r['variant'], 'repeat': r['repeat'], **r['metrics'],
                         'presentP99Ms': r['presentIntervalsMs']['p99'], 'receipt': r['receipt']})
matrix.assert_gpu_tests_idle()
print('NORMAL_COMPARISON', json.dumps({'summary': summary, 'BChangePercent': comparison}, ensure_ascii=False), flush=True)
