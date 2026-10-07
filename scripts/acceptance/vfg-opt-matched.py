"""Adjacent original/candidate controls, serial and unchanged GPU class."""
from pathlib import Path
import subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
cases=[('matched-old-medium4',1,4,['--original']),('matched-new-medium4',1,4,[]),
 ('matched-sync-medium4',1,4,['--sync-submit']),('matched-old-high2',2,2,['--original']),('matched-new-high2',2,2,[])]
for name,q,m,flags in cases:
 subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/vfg-opt-run.py'),'case',name,'--quality',str(q),'--multiplier',str(m),*flags],cwd=ROOT,timeout=160,check=True)
print('VFG ADJACENT CONTROLS PASS',len(cases),flush=True)
