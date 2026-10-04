"""Serial comparison orchestration; individual player tests remain <=300s."""
from pathlib import Path
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
BASE = Path('E:/项目/Veyra')
variant = sys.argv[1]
settings = sys.argv[2:] or ['S1', 'S2-720', 'S3', 'S4', 'S5-existing']
logs = BASE / 'logs/perf-nr-20261004'
for setting in settings:
    for repeat in range(1, 4):
        label = f'{variant}-M1-{setting}-r{repeat}'
        result = logs / label / 'result.json'
        if result.exists():
            assert json.loads(result.read_text(encoding='utf-8'))['passed'], f'failed receipt: {result}'
            print('ALREADY_PASS', label, flush=True)
            continue
        print('START', label, flush=True)
        out = logs / (label + '-driver.log')
        with out.open('x', encoding='utf-8') as f:
            p = subprocess.run([sys.executable, '-B', str(ROOT / 'scripts/perf/nr-matrix.py'),
                                variant, 'M1', setting, label], cwd=ROOT, stdout=f,
                               stderr=subprocess.STDOUT, timeout=299)
        if p.returncode:
            print('FAILED', label, p.returncode, out, flush=True)
            raise SystemExit(p.returncode)
        data = json.loads(result.read_text(encoding='utf-8'))
        print('PASS', label, 'processingMs=', data['summary']['enhancementProcessingMs']['medianOfRollingObservations'],
              'nrP95Ms=', data['summary']['gpuNrP95Ms']['medianOfRollingObservations'], flush=True)
