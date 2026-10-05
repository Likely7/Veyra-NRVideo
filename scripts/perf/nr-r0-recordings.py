"""Decode actual owned OBS recordings; corroborate moving video, no GPU timing."""
from pathlib import Path
import hashlib, json, os, subprocess, sys
from PIL import Image, ImageChops, ImageStat

BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
label=sys.argv[1];assert label.replace('-','').isalnum()
root=BASE/'logs'/TASK/label
runs=json.loads((root/'completed.json').read_text(encoding='utf-8'))
out=root/'record-review';out.mkdir(exist_ok=False)
tmp=BASE/'tmp'/TASK/(label+'-record-review');tmp.mkdir(exist_ok=False)
env={**os.environ,'TEMP':str(tmp),'TMP':str(tmp)};results=[]
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
for row in runs:
    clip=Path(row['recording']).resolve();assert clip.is_relative_to((BASE/'tests'/TASK/label).resolve())
    assert digest(clip)==row['recordingSha256']
    probe=subprocess.run(['ffprobe','-v','error','-select_streams','v:0','-count_frames','-show_entries',
        'stream=codec_name,width,height,r_frame_rate,nb_read_frames','-show_entries','format=duration','-of','json',str(clip)],
        env=env,capture_output=True,timeout=90,check=True)
    metadata=json.loads(probe.stdout);stream=metadata['streams'][0]
    assert (stream['width'],stream['height'])==(1280,800) and int(stream['nb_read_frames'])>650
    md5=out/(row['mode']+'-whole-frames.md5')
    with (out/(row['mode']+'-decode.log')).open('xb') as log:
        subprocess.run(['ffmpeg','-nostdin','-v','error','-i',str(clip),'-map','0:v:0','-an','-f','framemd5',str(md5)],
            env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90,check=True)
    decoded=[line for line in md5.read_text(encoding='utf-8').splitlines() if line and not line.startswith('#')]
    assert len(decoded)==int(stream['nb_read_frames'])
    shots=[];full=[]
    for second in (2,6,10,14,17.5,18.5,19.5,22):
        shot=out/(row['mode']+'-t'+str(second)+'.png')
        with (out/(row['mode']+'-extract.log')).open('ab') as log:
            subprocess.run(['ffmpeg','-nostdin','-v','error','-ss',str(second),'-i',str(clip),'-map','0:v:0','-an',
                '-frames:v','1',str(shot)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45,check=True)
        image=Image.open(shot).convert('RGB');crop=image.crop((220,180,660,430))
        variation=ImageStat.Stat(crop).stddev;required=row['mode']=='compat' or 17<=second<=20
        assert not required or max(variation)>12,(row['mode'],second,'recorded movie missing',variation)
        shots.append({'second':second,'path':str(shot),'sha256':digest(shot),'videoStddev':variation,'movieRequired':required})
        if 17<=second<=20:full.append(crop.copy())
    changes=[ImageStat.Stat(ImageChops.difference(a,b)).mean for a,b in zip(full,full[1:])]
    assert all(max(change)>.2 for change in changes),'Recorded fullscreen movie does not advance'
    results.append({'mode':row['mode'],'recording':str(clip),'recordingSha256':digest(clip),'metadata':metadata,
        'wholeDecodedFrames':len(decoded),'wholeFrameMd5':str(md5),'wholeFrameMd5Sha256':digest(md5),
        'shots':shots,'fullscreenVideoDifferenceMeans':changes,'softwareDecode':True,'passed':True})
(out/'review.json').write_text(json.dumps({'runs':results,'passed':True,'physicalDisplayTiming':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('R0_OBS_RECORD_REVIEW',label,[(row['mode'],row['wholeDecodedFrames']) for row in results],flush=True)
