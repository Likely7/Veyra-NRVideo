"""Real shared graph/NVENC edge cases. Each product invocation is bounded to 290s."""
import json, os, re, subprocess, sys
from pathlib import Path
BASE=Path('E:/项目/Veyra');TASK='vfg-integration-20261003';APP=BASE/'tests'/TASK/'app'
label=sys.argv[1];out=BASE/'tests'/TASK/label;logs=BASE/'logs'/TASK/label;tmp=BASE/'tmp'/TASK/label
for p in (out,logs,tmp):p.mkdir(parents=True,exist_ok=False)
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),VEYRA_VFG_RUNTIME=str(BASE/'deps/vfg-python-20261003/nvvfx/libs'))
results=[]
def fixture(rate):
    file=out/f'720p{rate}.mp4'
    with (logs/f'fixture-{rate}.log').open('xb') as log:
        subprocess.run(['ffmpeg','-v','warning','-n','-f','lavfi','-i',f'testsrc2=size=1280x720:rate={rate}','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t',str(8/rate),'-c:v','libx264','-preset','fast','-crf','12','-c:a','aac','-shortest',str(file)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90,check=True)
    return file
def run(name,source,flags=(),missing=False):
    child=env.copy()
    if missing:child['VEYRA_VFG_RUNTIME']=str(tmp/'missing-runtime')
    file=out/(name+'.mp4');logfile=logs/(name+'.log')
    with logfile.open('xb') as log:
        code=subprocess.run(['.\\veyra_vfg_export_probe.exe',str(source),str(file),'8','1',*flags],executable=str(APP/'veyra_vfg_export_probe.exe'),cwd=APP,env=child,stdout=log,stderr=subprocess.STDOUT,timeout=290).returncode
    text=logfile.read_text(encoding='utf8',errors='replace')
    if missing:
        assert code==1 and not file.exists() and '[vfg]' in text and 'ok=0' in text,text[-6000:]
    elif '--cancel' in flags:
        assert code==0 and not file.exists() and 'cancelled=1' in text,text[-6000:]
    else:
        assert code==0 and 'backend=NVIDIA-VFG' in text and 'quality=1' in text and 'substituting' not in text,text[-6000:]
        assert not re.search(r'\[ERROR\]|device removed|not alive',text),text[-6000:]
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_streams','-of','json',str(file)],env=env,timeout=30))
        v=next(s for s in probe['streams'] if s['codec_type']=='video')
        sourceprobe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_streams','-of','json',str(source)],env=env,timeout=30))
        sv=next(s for s in sourceprobe['streams'] if s['codec_type']=='video');n,d=map(int,sv['r_frame_rate'].split('/'))
        on,od=map(int,v['r_frame_rate'].split('/'))
        assert int(v['nb_read_frames'])==int(sv['nb_read_frames'])*8 and on/od==n/d*8,probe
        assert abs(float(v['duration'])-int(sv['nb_read_frames'])/(n/d))<1/(on/od),probe
        if '--sr' in flags:assert (v['width'],v['height'])==(3840,2160) and 'nr=1' in text and 'sr=1' in text,text[-6000:]
        (logs/(name+'-probe.json')).write_text(json.dumps(probe,indent=2),encoding='utf8')
    result={'test':name,'exit':code,'status':'pass','log':str(logfile)};results.append(result)
    (logs/'summary.json').write_text(json.dumps(results,indent=2),encoding='utf8');print(result,flush=True)
source=BASE/'tests/field-upgrade-20261003/production-rtss/2k30.avi'
assert source.exists(),source
run('2k30-avi-nr-highest-sr4k-vfg8',source,('--nr','--sr'))
for rate in (24,60):run(f'720p{rate}-vfg8',fixture(rate))
short=BASE/'tests'/TASK/'export-basic-v2/input.mp4'
run('cancel-native8',short,('--cancel',))
run('missing-runtime-export8',short,missing=True)
print('VFG COMBINATIONS PASS',out)
