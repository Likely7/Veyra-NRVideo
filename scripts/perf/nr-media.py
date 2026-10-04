"""Own CPU-only synthetic timing stimuli, with provenance and decoded hashes."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

base=Path('E:/项目/Veyra');task='perf-nr-20261004'
out=base/'tests/perf-matrix/media';out.mkdir(parents=True,exist_ok=True)
logs=base/'logs'/task/'authored-media-v1';logs.mkdir(exist_ok=False)
tmp=base/'tmp'/task/'authored-media-v1';tmp.mkdir(parents=True,exist_ok=False)
corpus=Path('C:/Users/123/Desktop/Veyra DLSS Video Player/loop/local/fixed_clips/corpus')
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp))
records=[]
for name,original in [('M10-1080p60-synthetic',corpus/'translation_1080p60.mp4'),('M2',corpus/'translation_4k60.mp4')]:
    target=out/(name+'.mkv');assert not target.exists()
    command=['ffmpeg','-v','warning','-nostdin','-n','-stream_loop','-1','-i',str(original),
             '-t','60','-vf','setpts=N/(60*TB)','-an','-c:v','libx264','-preset','ultrafast','-crf','18',
             '-threads','4','-pix_fmt','yuv420p','-color_range','tv','-colorspace','bt709',
             '-color_primaries','bt709','-color_trc','bt709',str(target)]
    with (logs/(name+'-encode.log')).open('xb') as f:
        subprocess.run(command,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=280)
    probe=subprocess.run(['ffprobe','-v','error','-count_frames','-show_entries',
                         'stream=width,height,r_frame_rate,avg_frame_rate,nb_read_frames:format=duration',
                         '-of','json',str(target)],env=env,capture_output=True,text=True,check=True,timeout=120)
    info=json.loads(probe.stdout);assert int(info['streams'][0]['nb_read_frames'])==3600
    md5=logs/(name+'-first600.framemd5')
    subprocess.run(['ffmpeg','-v','error','-nostdin','-n','-i',str(target),'-frames:v','600',
                    '-an','-f','framemd5',str(md5)],env=env,check=True,timeout=180)
    hashes=[s.split(',')[-1].strip() for s in md5.read_text().splitlines() if s and not s.startswith('#')]
    assert len(hashes)==600
    adjacent=sum(a==b for a,b in zip(hashes,hashes[1:]))
    record={'id':name,'path':str(target),'sha256':hashlib.file_digest(target.open('rb'),'sha256').hexdigest(),
            'source':str(original),'sourceSha256':hashlib.file_digest(original.open('rb'),'sha256').hexdigest(),
            'provenance':'Existing authored synthetic translation corpus; repeated 10-second motion, re-timed to exact 60Hz. Not real footage or console capture.',
            'command':command,'ffprobe':info,'checkedDecodedFrames':600,'adjacentDuplicateCount':adjacent}
    records.append(record);print(json.dumps(record,ensure_ascii=False),flush=True)
with (logs/'manifest.json').open('x',encoding='utf-8') as f:json.dump(records,f,ensure_ascii=False,indent=2)
