"""Matched ordinary playback A/final B/B-direct; no competitor or stress fixture."""
from pathlib import Path
import contextlib, csv, importlib.util, json, re, statistics, sys

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
artifacts = {}
for name in ('A', variant):
    app = matrix.stage(name, label)
    payload = {p.relative_to(app).as_posix(): matrix.digest(p) for p in app.rglob('*')
               if p.is_file() and p.suffix.lower() in ('.exe', '.dll', '.qml')}
    artifacts[name] = {'stage': str(app), 'payloadSha256': payload}
assert artifacts['A']['payloadSha256']['veyra_qml_ui.exe'] == 'b7081f0e6045bc791105bc80de364247e169e3522891ef50cf9f62bbbfc10eb4'
for artifact in artifacts.values():
    assert artifact['payloadSha256']['runtime/experimental/nvngx_dlssnr.dll'] == 'f95feb54137ea11979f9b4ec4f00afd84b5c98a5624d3388fbf6a87714a39fcc'
identity = {'artifacts': artifacts, 'sourceSha256': matrix.digest(matrix.SOURCES['M1']),
            'baselineProductCommit': '8cdc612120cbf23ba116a33c3cb0a53e2043f718',
            'finalProductCommit': 'e8f0bd1082293c88a3ca1db851667162e4cef7a9',
            'competition': False, 'gpuProcessPriority': 'normal', 'secondsPerRun': 50,
            'measurement': 'Merged GPU interval and last NR rolling observations; actual successful software Present, no scanout.'}
(folder/'artifacts.json').write_text(json.dumps(identity, ensure_ascii=False, indent=2), encoding='utf-8')
keys = ('gpuNrP95Ms', 'gpuSrP95Ms', 'gpuFlowP95Ms', 'gpuResidualP95Ms',
        'gpuFgBatchP95Ms', 'enhancementProcessingMs', 'gpuReadyP95Ms')
results = []
for setting in ('S1', 'S2-720', 'S3', 'S4', 'S5-existing'):
    orders = (('A', 'B'), ('B', 'A'), ('A', 'B'))
    if setting == 'S4':
        orders = (('A', 'B', 'B-direct'), ('B-direct', 'B', 'A'), ('B', 'A', 'B-direct'))
    for repeat, order in enumerate(orders, 1):
        for mode in order:
            actual_variant = 'A' if mode == 'A' else variant
            name = f'{label}-{setting}-{mode}-r{repeat}'
            env = {'VEYRA_TEST_GRAPH_DIRECT': '1'} if mode == 'B-direct' else {}
            print('R0_NORMAL_START', name, flush=True)
            with (folder/(name+'-driver.log')).open('x', encoding='utf-8') as stream:
                with contextlib.redirect_stdout(stream):
                    matrix.run(actual_variant, 'M1', setting, name, 50, gpuPriority=2,
                               testEnv=env, stageLabel=label)
            run_folder = BASE/'logs'/TASK/name
            receipt = json.loads((run_folder/'result.json').read_text(encoding='utf-8'))
            assert receipt['passed'] and receipt['gpuPriority']['applied']
            assert receipt['exeSha256'] == artifacts[actual_variant]['payloadSha256']['veyra_qml_ui.exe']
            log = (run_folder/'player.log').read_text(encoding='utf-8', errors='replace')
            compute = 'requested=COMPUTE createHr=0x0' in log
            assert compute == (mode == 'B' and setting == 'S4'), (name, 'wrong producer queue')
            ui, submitted = [], []
            for line in log.splitlines():
                if '[submit]' in line:
                    row = {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', line)}
                    if 100000000 <= row.get('pts100ns', -1) <= 480000000:
                        submitted.append(row)
                if 'NR_PERF_SAMPLE ' in line:
                    row = json.loads(line.split('NR_PERF_SAMPLE ', 1)[1])
                    if row['ready'] and 10 <= row['position'] <= 48:
                        ui.append(row)
            intervals = sorted((b['host100ns']-a['host100ns'])/10000 for a, b in zip(submitted, submitted[1:]))
            assert len(intervals) > 100 and len(ui) >= 10
            pct = lambda fraction: intervals[min(len(intervals)-1, int(len(intervals)*fraction))]
            meters = json.loads((run_folder/'meters.json').read_text(encoding='utf-8'))
            skips = [int(v) for v in re.findall(r'previewSkipped=(\d+)', log)]
            expired = [int(v) for v in re.findall(r'expiredGenerated=(\d+)', log)]
            row = {'name': name, 'mode': mode, 'setting': setting, 'repeat': repeat,
                   'receipt': str(run_folder/'result.json'), 'actualCompute': compute,
                   'metrics': {k: receipt['summary'][k]['medianOfRollingObservations'] for k in keys},
                   'presentIntervalsMs': {'p50': pct(.5), 'p95': pct(.95), 'p99': pct(.99), 'max': max(intervals), 'count': len(intervals)},
                   'maxLoggedPreviewSkipped': max(skips, default=0), 'maxLoggedExpiredGenerated': max(expired, default=0),
                   'uiActiveValues': sorted(set(s['active'] for s in ui)),
                   'maxUiTimerIntervalMs': max(s['uiTimerMaxMs'] for s in ui),
                   'maxTotalGpuMemoryMiB': max(float(s['gpu'].get('memoryMiB', 0)) for s in meters),
                   'medianProcessCpuOneCoreScale': statistics.median(s['cpuPercentOneCoreScale'] for s in meters if 10 <= s['elapsed'] <= 48),
                   'stallLines': [line for line in log.splitlines() if '[engine-stall]' in line]}
            results.append(row)
            (folder/'completed.json').write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding='utf-8')
            print('R0_NORMAL_RESULT', name, json.dumps({k: row[k] for k in ('metrics', 'presentIntervalsMs', 'maxLoggedPreviewSkipped', 'maxLoggedExpiredGenerated')}), flush=True)

summary = {}
for setting in ('S1', 'S2-720', 'S3', 'S4', 'S5-existing'):
    summary[setting] = {}
    for mode in ('A', 'B', 'B-direct'):
        rows = [r for r in results if r['setting'] == setting and r['mode'] == mode]
        if not rows:
            continue
        values = {k: [r['metrics'][k] for r in rows] for k in keys}
        values.update({f'present{p}Ms': [r['presentIntervalsMs'][p] for r in rows] for p in ('p95', 'p99', 'max')})
        values.update({k: [r[k] for r in rows] for k in ('maxLoggedPreviewSkipped', 'maxLoggedExpiredGenerated', 'maxUiTimerIntervalMs', 'maxTotalGpuMemoryMiB', 'medianProcessCpuOneCoreScale')})
        summary[setting][mode] = {k: {'median': statistics.median(v), 'min': min(v), 'max': max(v), 'runs': v} for k, v in values.items()}
for artifact in artifacts.values():
    app = Path(artifact['stage'])
    assert all(matrix.digest(app/name) == sha for name, sha in artifact['payloadSha256'].items())
matrix.assert_gpu_tests_idle()
final = {'runs': results, 'summary': summary, 'payloadUnchanged': True, 'competition': False}
(folder/'comparison.json').write_text(json.dumps(final, ensure_ascii=False, indent=2), encoding='utf-8')
with (folder/'per-run.csv').open('x', encoding='utf-8-sig', newline='') as stream:
    fields = ['name', 'setting', 'mode', 'repeat', *keys, 'presentP99Ms', 'presentMaxMs', 'skipped', 'expired', 'receipt']
    writer = csv.DictWriter(stream, fieldnames=fields)
    writer.writeheader()
    for r in results:
        writer.writerow({'name': r['name'], 'setting': r['setting'], 'mode': r['mode'], 'repeat': r['repeat'], **r['metrics'],
                         'presentP99Ms': r['presentIntervalsMs']['p99'], 'presentMaxMs': r['presentIntervalsMs']['max'],
                         'skipped': r['maxLoggedPreviewSkipped'], 'expired': r['maxLoggedExpiredGenerated'], 'receipt': r['receipt']})
print('R0_NORMAL_COMPLETE', len(results), flush=True)
