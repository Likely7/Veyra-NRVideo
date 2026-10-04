"""Review preserved UI evidence by actual settled phases, retaining old receipts."""
from pathlib import Path
from datetime import datetime
import json,re,sys
BASE=Path('E:/项目/Veyra/logs/perf-nr-20261004')
for name in sys.argv[1:]:
 assert name.replace('-','').isalnum();folder=BASE/name
 receipt=json.loads((folder/'result.json').read_text(encoding='utf-8'));log=(folder/'player.log').read_text(encoding='utf-8',errors='replace')
 phases=[json.loads(line.split('QUEUE_UI_PHASE ',1)[1]) for line in log.splitlines() if 'QUEUE_UI_PHASE ' in line]
 lines=[line for line in log.splitlines() if '[graph-queue] selected=' in line]
 changes=[(datetime.fromisoformat(line.split()[0].replace('Z','+00:00')).timestamp()*1000,re.search(r'selected=(COMPUTE|DIRECT)',line)[1]) for line in lines]
 actual=[next((queue for at,queue in reversed(changes) if at<=phase['at']),None) for phase in phases[1:]]
 expected=['COMPUTE','DIRECT','COMPUTE','DIRECT','DIRECT','COMPUTE','DIRECT','COMPUTE']
 passed=receipt['exitCode']==0 and 'QUEUE_UI_PASS' in log and len(phases)==9 and actual==expected and not any(s in log for s in ('QUEUE_UI_FAIL','[ERROR]','[FATAL]','ReferenceError:','TypeError:','leaked parameter block'))
 result={'originalReceipt':str(folder/'result.json'),'originalPassed':receipt['passed'],'phases':phases,'queueChanges':changes,'steadyPhaseQueues':actual,'expectedPhaseQueues':expected,'passed':passed,'reason':'Two separate UI requests may create an intermediate compatible graph; audit the queue at every settled phase, not a fixed global change count. Original receipt/log unmodified.'}
 with (folder/'phase-review.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,ensure_ascii=False,indent=2)
 print(name,passed,actual,flush=True);assert passed
