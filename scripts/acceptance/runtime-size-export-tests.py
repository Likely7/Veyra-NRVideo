"""All 21 VFG combinations through the real shared graph and D3D12 NVENC."""
import json, os, re, shutil, subprocess, sys
from pathlib import Path
BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004';APP=BASE/'tests'/TASK/'app'
label=sys.argv[1];out,logs,tmp=(BASE/p/TASK/label for p in ('tests','logs','tmp'))
for p in (out,logs,tmp):p.mkdir(parents=True,exist_ok=False)
probe=shutil.which('ffprobe');assert probe
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),
    VEYRA_VFG_RUNTIME=str(APP/'runtime/nvidia-vfg'))
env['PATH']=os.pathsep.join((env['WINDIR']+'/System32',env['WINDIR'],env['WINDIR']+'/System32/Wbem'))
source=BASE/'tests'/TASK/'export-basic-v2/input.mp4';assert source.exists()
results=[]
for quality in range(3):
    for multiplier in range(2,9):
        name=f'720p30-{multiplier}x-q{quality}';file=out/(name+'.mp4')
        with (logs/(name+'.log')).open('xb') as log:
            code=subprocess.run(['.\\veyra_vfg_export_probe.exe',str(source),str(file),str(multiplier),str(quality)],executable=str(APP/'veyra_vfg_export_probe.exe'),cwd=APP,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=290).returncode
        text=(logs/(name+'.log')).read_text(encoding='utf8',errors='replace')
        assert code==0 and 'backend=NVIDIA-VFG' in text and 'nvenc' in text.lower(),text[-5000:]
        assert not re.search(r'\[ERROR\]|device removed|not alive|substituting',text),text[-5000:]
        data=json.loads(subprocess.check_output([probe,'-v','error','-count_frames','-show_streams','-of','json',str(file)],env=env,timeout=30))
        video=next(s for s in data['streams'] if s['codec_type']=='video');audio=next(s for s in data['streams'] if s['codec_type']=='audio')
        assert video['codec_name']=='hevc' and int(video['nb_read_frames'])==8*multiplier,data
        for key in ('avg_frame_rate','r_frame_rate'):
            n,d=map(int,video[key].split('/'));assert abs(n/d-30*multiplier)<.00001*30*multiplier,data
        assert abs(float(video['duration'])-8/30)<1/(30*multiplier) and abs(float(audio['duration'])-8/30)<.06,data
        results.append({'case':name,'exit':code,'frames':8*multiplier,'probe':data})
        (logs/'summary.json').write_text(json.dumps(results,indent=2),encoding='utf8')
        print('EXPORT PASS',name,flush=True)
print('ALL VFG EXPORTS PASS',len(results),flush=True)
