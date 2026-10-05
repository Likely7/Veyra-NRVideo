"""Decode actual owned OBS recordings; corroborate moving video, no GPU timing."""
from pathlib import Path
import datetime, hashlib, json, os, re, subprocess, sys
from PIL import Image, ImageChops, ImageStat

BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
label=sys.argv[1];assert label.replace('-','').isalnum()
review_label=sys.argv[2] if len(sys.argv)>2 else 'v1';assert review_label.replace('-','').isalnum()
root=BASE/'logs'/TASK/label
runs=json.loads((root/'completed.json').read_text(encoding='utf-8'))
out=root/('record-review-'+review_label);out.mkdir(exist_ok=False)
tmp=BASE/'tmp'/TASK/(label+'-record-review-'+review_label);tmp.mkdir(exist_ok=False)
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
    # Source injection is asynchronous. Align the movie checks with the six
    # actual completed screenshot observations, rather than assuming capture
    # was ready two seconds after StartRecord. Preserve the initial frames in
    # the complete decode; no frame is dropped from that integrity check.
    starts=[]
    for obs_log in (BASE/'tests'/TASK/label/'obs-portable/config/obs-studio/logs').glob('*.txt'):
        for line in obs_log.read_text(encoding='utf-8',errors='replace').splitlines():
            if 'Writing file' in line and clip.name in line:
                match=re.match(r'(\d\d:\d\d:\d\d\.\d+):',line);assert match,line
                starts.append(datetime.datetime.fromisoformat(obs_log.name[:10]+'T'+match[1]).timestamp())
    assert len(starts)==1,starts
    elapsed={}
    for shot in row['shots']:
        saved=Path(shot['image']);assert digest(saved)==shot['sha256']
        elapsed[shot['stage']]=round(saved.stat().st_mtime-starts[0],3)
    full_end=elapsed['fullscreen']
    moments=[(second,stage) for stage,second in elapsed.items()]
    moments += [(round(full_end-offset,3),'fullscreen-motion') for offset in (2,1.5,1)]
    shots=[];full=[]
    for second,stage in sorted(moments):
        assert 0<second<float(metadata['format']['duration']),('Observation outside recording',stage,second)
        shot=out/(row['mode']+'-t'+str(second)+'.png')
        with (out/(row['mode']+'-extract.log')).open('ab') as log:
            subprocess.run(['ffmpeg','-nostdin','-v','error','-ss',str(second),'-i',str(clip),'-map','0:v:0','-an',
                '-frames:v','1',str(shot)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=45,check=True)
        image=Image.open(shot).convert('RGB');crop=image.crop((220,180,660,430))
        variation=ImageStat.Stat(crop).stddev;required=row['mode']=='compat' or stage.startswith('fullscreen')
        assert not required or max(variation)>12,(row['mode'],second,'recorded movie missing',variation)
        shots.append({'second':second,'stage':stage,'path':str(shot),'sha256':digest(shot),'videoStddev':variation,'movieRequired':required})
        if stage=='fullscreen-motion':full.append(crop.copy())
    changes=[ImageStat.Stat(ImageChops.difference(a,b)).mean for a,b in zip(full,full[1:])]
    assert all(max(change)>.2 for change in changes),'Recorded fullscreen movie does not advance'
    results.append({'mode':row['mode'],'recording':str(clip),'recordingSha256':digest(clip),'metadata':metadata,
        'wholeDecodedFrames':len(decoded),'wholeFrameMd5':str(md5),'wholeFrameMd5Sha256':digest(md5),
        'shots':shots,'fullscreenVideoDifferenceMeans':changes,'softwareDecode':True,'passed':True,
        'timing':'OBS recording-start log and completed source screenshot file times; approximate scene alignment',
        'actualStageElapsedSeconds':elapsed})
(out/'review.json').write_text(json.dumps({'runs':results,'passed':True,'physicalDisplayTiming':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('R0_OBS_RECORD_REVIEW',label,[(row['mode'],row['wholeDecodedFrames']) for row in results],flush=True)
