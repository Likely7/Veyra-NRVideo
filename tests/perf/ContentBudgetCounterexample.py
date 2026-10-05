"""Admission-model counterexample; measured inputs, no new GPU measurement."""
from pathlib import Path
import hashlib
import json

base = Path('E:/项目/Veyra/logs/perf-nr-20261004')
source = base / 'baseline-M1-summary.json'
baseline = json.loads(source.read_text(encoding='utf-8'))
cost = next(g for g in baseline['groups'] if g['setting'] == 'S4')['metrics']['enhancementProcessingMs']['medianOfThreeRuns']
assert 20 < cost < 23
transport, content, safety = 60, 30, .8
real_budget, inflated_budget = 1000 / transport * safety, 1000 / content * safety
assert real_budget < cost < inflated_budget
# If work is still evaluated on every transport input, a constant-cost stream
# with this recorded admission estimate accumulates queue debt. This is a
# constructed scheduling trace, NOT a new 60 Hz GPU throughput experiment.
finish = 0.
debt = []
for index in range(300):
    arrival = index * 1000 / transport
    finish = max(finish, arrival) + cost
    debt.append(finish - (arrival + 1000 / transport))
result = {'kind': 'constructed admission trace using recorded rolling-P95 estimate',
          'baselineSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
          'setting': 'S4', 'costEstimateMs': cost, 'transportHz': transport,
          'contentHz': content, 'trueBudgetMs': real_budget,
          'inflatedBudgetMs': inflated_budget, 'wouldAdmitByContent': cost < inflated_budget,
          'wouldAdmitByTransport': cost < real_budget,
          'modelCompletionRateHz': 1000 / cost, 'modelQueueDebtAt300Ms': debt[-1],
          'premise': '1a rejected: identical pixels do not make NR/SR output identical',
          'productionChange': False}
with (base / '1b-budget-counterexample-v1.json').open('x', encoding='utf-8') as f:
    json.dump(result, f, ensure_ascii=False, indent=2)
print(json.dumps(result, ensure_ascii=False))
