"""Read display events for this owned player only; no input, injection or elevation."""
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='playback-smoothness-20261004'
logs=BASE/'logs'/TASK
tool=BASE/'deps/rtss-overlay-20261002/Plugins/Client/PresentMonDataProvider/PresentMon-2.3.1-x64.exe'
env=os.environ.copy();env.update(TEMP=str(BASE/'tmp'/TASK),TMP=str(BASE/'tmp'/TASK))
launcher=subprocess.Popen([sys.executable,'-B',str(ROOT/'scripts/acceptance/playback-smoothness-baseline.py'),
    'candidate','dlss6','candidate-display-v1','short','--wait'],cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf8')
line=launcher.stdout.readline();print(line.strip(),flush=True)
receipt=json.loads(line)
csv_path=logs/'candidate-display-v1/frames.csv'
args=[str(tool),'--process_id',str(receipt['pid']),'--session_name','VeyraSmooth-'+str(os.getpid()),
      '--timed','35','--terminate_after_timed','--terminate_on_proc_exit','--output_file',str(csv_path),
      '--no_console_stats','--no_track_input','--date_time']
with (logs/'candidate-display-v1/presentmon-console.log').open('x',encoding='utf8') as out:
    try:code=subprocess.run(args,env=env,cwd=BASE/'tmp'/TASK,stdout=out,stderr=subprocess.STDOUT,timeout=45).returncode
    except subprocess.TimeoutExpired:code=124
tail=launcher.communicate(timeout=280)[0];print(tail.strip(),flush=True)
rows=[]
if csv_path.exists():
    with csv_path.open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f))
result=dict(player=receipt,playerLauncherExit=launcher.returncode,tool=str(tool),
            toolSHA256=hashlib.sha256(tool.read_bytes()).hexdigest(),toolExit=code,
            rows=len(rows),columns=list(rows[0]) if rows else [],
            firstRows=rows[:3],csv=str(csv_path),
            limitation='ETW display events are not physical screen latency; no desktop input or elevation requested.')
(logs/'candidate-display-v1/display-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(result,ensure_ascii=False,indent=2))
raise SystemExit(launcher.returncode or (0 if code==0 and rows else 1))
