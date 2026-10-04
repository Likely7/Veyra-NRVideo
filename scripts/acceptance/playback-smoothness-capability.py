"""Remove/restore only an owned staged link; runtime bytes and the release stay intact."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='playback-smoothness-20261004'
env=os.environ.copy();env.update(TEMP=str(BASE/'tmp'/TASK),TMP=str(BASE/'tmp'/TASK))
proc=subprocess.Popen([sys.executable,'-B',str(ROOT/'scripts/acceptance/playback-smoothness-baseline.py'),
    'candidate','capability','capability-v1','--wait'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8')
receipt=json.loads(proc.stdout.readline());print(json.dumps(receipt),flush=True)
app=Path(receipt['app']).resolve()
assert app.is_relative_to((BASE/'tests'/TASK).resolve())
src=app/'runtime/nvidia-vfg/nvVFXVideoFrameGeneration.dll'
missing=src.with_suffix('.dll.disabled')
assert src.is_file() and not missing.exists()
before=hashlib.sha256(src.read_bytes()).hexdigest()
log=Path(receipt['logs'])/'player.log'
def wait_marker(marker):
    deadline=time.monotonic()+12
    while time.monotonic()<deadline and proc.poll() is None:
        if log.exists() and marker in log.read_text(encoding='utf8',errors='replace'):return
        time.sleep(.03)
    raise RuntimeError('Missing marker '+marker)
try:
    wait_marker('PLAYBACK_CAPABILITY_READY')
    src.rename(missing)
    wait_marker('PLAYBACK_CAPABILITY_MISSING_PASS')
    missing.rename(src)
    output=proc.communicate(timeout=45)[0];print(output.strip(),flush=True)
    assert proc.returncode==0,proc.returncode
finally:
    if missing.exists():missing.rename(src)
    assert hashlib.sha256(src.read_bytes()).hexdigest()==before
print('RUNTIME BYTES RESTORED',before)
