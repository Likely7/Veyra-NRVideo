"""Seal the unchanged product baseline and summarize three repeated GPU runs."""
from pathlib import Path
import importlib.util
import json
import shutil
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
TASK = 'perf-nr-20261004'
spec = importlib.util.spec_from_file_location('perf_matrix', ROOT / 'scripts/perf/nr-matrix.py')
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)
subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/perf/nr-control.py'), 'guard'], check=True)
runs = []
for setting in matrix.CONFIGS:
    for repeat in range(1, 4):
        path = BASE / 'logs' / TASK / f'A-M1-{setting}-r{repeat}' / 'result.json'
        result = json.loads(path.read_text(encoding='utf-8'))
        assert result['passed'], path
        runs.append((path, result))
assert len({r['exeSha256'] for _, r in runs}) == 1
assert len({r['runtimeSha256'] for _, r in runs}) == 1
assert len({r['sourceSha256'] for _, r in runs}) == 1
columns = ('enhancementProcessingMs', 'gpuNrP95Ms', 'gpuSrP95Ms', 'gpuFlowP95Ms',
           'gpuResidualP95Ms', 'gpuReadyP95Ms', 'presentP95Ms', 'vramUsageMiB',
           'previewSkipped', 'expiredGenerated', 'slotWaits')
summary = []
for setting in matrix.CONFIGS:
    group = [r for _, r in runs if r['setting'] == setting]
    data = {}
    for key in columns:
        values = [r['summary'][key]['medianOfRollingObservations'] for r in group if key in r['summary']]
        if values:
            data[key] = {'medianOfThreeRuns': statistics.median(values), 'min': min(values), 'max': max(values), 'runValues': values}
    summary.append({'setting': setting, 'actualConfig': matrix.CONFIGS[setting], 'metrics': data})
package = BASE / 'test-packages' / TASK / 'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable'
assert not package.exists()
shutil.copytree(matrix.PACKAGE, package, copy_function=matrix.copy_dependency,
                ignore=shutil.ignore_patterns('*.log', 'logs', 'user-data-2.0.0', '*.dmp'))
shutil.copy2(BASE / 'build' / TASK / 'A/veyra_qml_ui.exe', package / 'veyra_qml_ui.exe')
assert matrix.digest(package / 'veyra_qml_ui.exe') == runs[0][1]['exeSha256']
main = (package / 'qml/Veyra/Main.qml').read_text(encoding='utf-8')
assert 'NrPerfProbe' not in main and not (package / 'qml/Veyra/NrPerfProbe.qml').exists()
for path in (package / 'qml/Veyra').glob('*.qml'):
    relative = path.relative_to(package).as_posix()
    before = subprocess.check_output(['git', '-C', str(ROOT), 'show', '8cdc612120cbf23ba116a33c3cb0a53e2043f718:' + relative])
    assert path.read_text(encoding='utf-8').replace('\r\n', '\n') == before.decode('utf-8').replace('\r\n', '\n'), relative
files = [{'path': p.relative_to(package).as_posix(), 'size': p.stat().st_size, 'sha256': matrix.digest(p)}
         for p in sorted(package.rglob('*')) if p.is_file() and p.name != 'local-package-manifest.json']
manifest = {'purpose': 'Unchanged product baseline A; local-only',
            'productSource': '8cdc612120cbf23ba116a33c3cb0a53e2043f718',
            'buildSourceWithDocs': '37bc0c090918566f7ebebc9f5edb24ba60c5f5a3',
            'exeSha256': runs[0][1]['exeSha256'], 'files': files}
(package / 'local-package-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
receipt = {'productSource': manifest['productSource'], 'buildSourceWithDocs': manifest['buildSourceWithDocs'],
           'package': str(package), 'payloadFiles': len(files), 'source': str(matrix.SOURCES['M1']),
           'sourceSha256': runs[0][1]['sourceSha256'], 'runtimeSha256': runs[0][1]['runtimeSha256'],
           'exeSha256': manifest['exeSha256'], 'driver': runs[0][1]['beforeGpu']['driver'],
           'measurement': 'Median/range of each run median of rolling observations; not global percentiles or scanout',
           'window': '1280x800', 'groups': summary,
           'rawResults': [{'path': str(p), 'sha256': matrix.digest(p)} for p, _ in runs]}
path = BASE / 'logs' / TASK / 'baseline-M1-summary.json'
with path.open('x', encoding='utf-8') as out:
    json.dump(receipt, out, ensure_ascii=False, indent=2)
print(json.dumps({k: receipt[k] for k in ('package', 'payloadFiles', 'driver', 'groups')}, ensure_ascii=False))
