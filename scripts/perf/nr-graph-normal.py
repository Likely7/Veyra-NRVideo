"""Three producer/presenter modes on ordinary playback, no competing workload."""
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
variant, label = sys.argv[1:3]
assert label.replace('-', '').isalnum()
matrix.assert_gpu_tests_idle()
folder = BASE/'logs'/TASK/(label+'-summary')
folder.mkdir(exist_ok=False)
app = matrix.stage(variant, label)
payload = {str(p.relative_to(app)): matrix.digest(p) for p in app.rglob('*')
           if p.is_file() and p.suffix.lower() in ('.exe', '.dll', '.qml')}
identity = {'stage': str(app), 'payloadSha256': payload,
            'sourceSha256': matrix.digest(matrix.SOURCES['M1']),
            'sourceCommit': matrix.subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip(),
            'competition': False, 'gpuProcessPriority': 'normal',
            'measurement': 'NR: last layer rolling P95; enhancement: rolling merged-interval mean; software Present intervals, no scanout.'}
(folder/'artifacts.json').write_text(json.dumps(identity, ensure_ascii=False, indent=2), encoding='utf-8')
keys = ('gpuNrP95Ms', 'gpuSrP95Ms', 'gpuFlowP95Ms', 'gpuResidualP95Ms',
        'gpuFgBatchP95Ms', 'enhancementProcessingMs', 'gpuReadyP95Ms')
orders = (('direct-normal', 'compute-normal', 'compute-high'),
          ('compute-high', 'direct-normal', 'compute-normal'),
          ('compute-normal', 'compute-high', 'direct-normal'))
results = []
for multiplier in (2, 3):
    setting = 'S4-'+str(multiplier)
    matrix.CONFIGS[setting] = {**matrix.CONFIGS['S4'], 'multiplier': multiplier}
    for repeat, order in enumerate(orders, 1):
        for mode in order:
            name = f'{label}-{multiplier}X-{mode}-r{repeat}'
            priority = 'high' if mode.endswith('high') else 'normal'
            test_env = {'VEYRA_TEST_PRESENT_QUEUE_PRIORITY': priority}
            if mode.startswith('compute-'):
                test_env['VEYRA_TEST_GRAPH_COMPUTE'] = '1'
            print('GRAPH_NORMAL_START', name, flush=True)
            with (folder/(name+'-driver.log')).open('x', encoding='utf-8') as stream:
                with contextlib.redirect_stdout(stream):
                    matrix.run(variant, 'M1', setting, name, 50, testEnv=test_env, stageLabel=label)
            run_folder = BASE/'logs'/TASK/name
            receipt = json.loads((run_folder/'result.json').read_text(encoding='utf-8'))
            assert receipt['passed'] and receipt['exeSha256'] == payload['veyra_qml_ui.exe']
            log = (run_folder/'player.log').read_text(encoding='utf-8', errors='replace')
            actual = [int(v) for v in re.findall(r'actualType=0 actualPriority=(\d+)', log)]
            assert actual and set(actual) == ({100} if priority == 'high' else {0})
            assert 'requested=2 actual=2' in log
            compute = 'requested=COMPUTE createHr=0x0' in log
            assert compute == mode.startswith('compute-'), 'Producer mode did not apply'
            submitted = []
            ui = []
            for line in log.splitlines():
                if '[submit]' in line:
                    row = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}
                    if 100000000 <= row.get('pts100ns', -1) <= 480000000:
                        submitted.append(row)
                if 'NR_PERF_SAMPLE ' in line:
                    row = json.loads(line.split('NR_PERF_SAMPLE ', 1)[1])
                    if row['ready'] and 10 <= row['position'] <= 48:
                        ui.append(row)
            intervals = [(b['host100ns']-a['host100ns'])/10000 for a, b in zip(submitted, submitted[1:])]
            assert len(intervals) > 100 and len(ui) >= 10
            ordered = sorted(intervals)
            pct = lambda fraction: ordered[min(len(ordered)-1, int(len(ordered)*fraction))]
            skips = [int(v) for v in re.findall(r'previewSkipped=(\d+)', log)]
            expired = [int(v) for v in re.findall(r'expiredGenerated=(\d+)', log)]
            stall_lines = [line for line in log.splitlines() if '[engine-stall]' in line]
            row = {'name': name, 'mode': mode, 'repeat': repeat, 'multiplier': multiplier,
                   'receipt': str(run_folder/'result.json'), 'actualPresentQueuePriority': actual,
                   'metrics': {k: receipt['summary'][k]['medianOfRollingObservations'] for k in keys},
                   'presentIntervalsMs': {'p50': pct(.5), 'p95': pct(.95), 'p99': pct(.99), 'max': max(intervals), 'count': len(intervals)},
                   'maxLoggedPreviewSkipped': max(skips, default=0), 'maxLoggedExpiredGenerated': max(expired, default=0),
                   'uiActiveValues': sorted(set(s['active'] for s in ui)),
                   'maxUiTimerIntervalMs': max(s['uiTimerMaxMs'] for s in ui),
                   'stallLines': stall_lines}
            results.append(row)
            (folder/'completed.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
            print('GRAPH_NORMAL_RESULT', name, json.dumps({k: row[k] for k in ('metrics', 'presentIntervalsMs', 'maxLoggedPreviewSkipped', 'maxLoggedExpiredGenerated')}), flush=True)

summary = {}
for multiplier in (2, 3):
    summary[str(multiplier)] = {}
    for mode in orders[0]:
        rows = [r for r in results if r['mode'] == mode and r['multiplier'] == multiplier]
        values = {k: [r['metrics'][k] for r in rows] for k in keys}
        values.update({f'present{p}Ms': [r['presentIntervalsMs'][p] for r in rows] for p in ('p95', 'p99', 'max')})
        summary[str(multiplier)][mode] = {k: {'median': statistics.median(v), 'min': min(v), 'max': max(v), 'runs': v} for k, v in values.items()}
assert all(matrix.digest(app/name) == value for name, value in payload.items()), 'Product payload changed'
matrix.assert_gpu_tests_idle()
final = {'runs': results, 'summary': summary, 'payloadUnchanged': True, 'competition': False}
(folder/'comparison.json').write_text(json.dumps(final, ensure_ascii=False, indent=2), encoding='utf-8')
with (folder/'per-run.csv').open('x', encoding='utf-8-sig', newline='') as stream:
    fields = ['name', 'multiplier', 'mode', 'repeat', *keys, 'presentP99Ms', 'presentMaxMs', 'skipped', 'expired', 'receipt']
    writer = csv.DictWriter(stream, fieldnames=fields)
    writer.writeheader()
    for r in results:
        writer.writerow({'name': r['name'], 'multiplier': r['multiplier'], 'mode': r['mode'], 'repeat': r['repeat'], **r['metrics'],
                         'presentP99Ms': r['presentIntervalsMs']['p99'], 'presentMaxMs': r['presentIntervalsMs']['max'],
                         'skipped': r['maxLoggedPreviewSkipped'], 'expired': r['maxLoggedExpiredGenerated'], 'receipt': r['receipt']})
print('GRAPH_NORMAL_COMPLETE', len(results), flush=True)
