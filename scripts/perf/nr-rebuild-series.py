"""Serial B / same-executable B-off suites; each native case is bounded."""
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[2]
variant,label=sys.argv[1:3]
for v,name in ((variant,label),(variant+'-off',label+'-off')):
    rc=subprocess.run([sys.executable,'-B',str(root/'scripts/perf/nr-rebuild.py'),v,name],cwd=root).returncode
    if rc:raise SystemExit(rc)
