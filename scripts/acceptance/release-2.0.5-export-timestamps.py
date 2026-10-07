"""Decode every generated output frame and verify its actual CFR timestamps."""
from pathlib import Path
import hashlib,json,shutil,subprocess,os
from fractions import Fraction
BASE=Path('E:/项目/Veyra');TASK='release-2.0.5-20261007'
paths=[BASE/'tests'/TASK/label/'result.mp4' for label in ('fsr-h264','fsr-nr-hevc','nr-stack-hevc')]
paths+=list((BASE/'tests'/TASK/'gui-fsr-export-second/outputs').glob('*.mp4'))
assert len(paths)==4
env=os.environ.copy();env.update(TEMP=str(BASE/'tmp'/TASK),TMP=str(BASE/'tmp'/TASK))
results=[]
for path in paths:
    args=[shutil.which('ffprobe'),'-v','error','-select_streams','v:0','-show_streams','-show_frames',
      '-show_entries','stream=width,height,r_frame_rate,duration:frame=best_effort_timestamp_time','-of','json',str(path)]
    probe=json.loads(subprocess.check_output(args,timeout=45,env=env))
    pts=[float(row['best_effort_timestamp_time']) for row in probe['frames']];stream=probe['streams'][0]
    fps=float(Fraction(stream['r_frame_rate']));expected=60 if 'nr-stack-hevc' in path.parts else 120
    assert len(pts)==expected and abs(pts[0])<0.000005 and all(abs(b-a-1/fps)<0.000005 for a,b in zip(pts,pts[1:])),str(path)
    args=[shutil.which('ffmpeg'),'-v','error','-xerror','-i',str(path),'-map','0:v:0','-f','null','-']
    decoded=subprocess.run(args,capture_output=True,timeout=60,env=env)
    assert decoded.returncode==0,decoded.stderr.decode('utf8',errors='replace')
    with path.open('rb') as f:identity=hashlib.file_digest(f,'sha256').hexdigest()
    results.append(dict(path=str(path),passed=True,frames=len(pts),fps=fps,width=stream['width'],height=stream['height'],
        firstPts=pts[0],lastPts=pts[-1],fullDecodePassed=True,strictCfrTimestamps=True,sha256=identity))
(BASE/'logs'/TASK/'export-timestamps.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
print('PASS',len(results),'outputs: complete frame decode and strict CFR timestamps')
