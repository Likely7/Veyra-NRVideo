"""Generate authored comma/multiline subtitles, an MKV and a PCM tempo fixture."""
from pathlib import Path
import os
import shutil
import subprocess

BASE=Path('E:/项目/Veyra'); TASK='playback-smoothness-20261004'
OUT=BASE/'tests'/TASK/'fixtures'; TMP=BASE/'tmp'/TASK/'fixtures'; LOGS=BASE/'logs'/TASK
for p in (OUT,TMP,LOGS):p.mkdir(parents=True,exist_ok=True)
text=['It is over, Sauron.','Ka-Zar, I warn you!','Your reign of terror over\nthe Savage Land ends now!',
    'I am Zaladane,\nHigh Priestess of\nthe Sun God, Garokk.','中文，第一行\nEnglish, second line\n第三行, 原样保留',
    'A, B, C, D, E, F, G, H, I, J, K.']
def clock(s,sep=','):return f'00:00:{s:06.3f}'.replace('.',sep)
srt='\n\n'.join(f'{i+1}\n{clock(i*3)} --> {clock(i*3+2.5)}\n{t}' for i,t in enumerate(text))+'\n'
(OUT/'golden.srt').write_text(srt,encoding='utf8')
(OUT/'golden.vtt').write_text('WEBVTT\n\n'+srt.replace(',000','.000').replace(',500','.500'),encoding='utf8')
header='[Script Info]\nScriptType: v4.00+\nPlayResX: 1920\nPlayResY: 1080\n[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\nStyle: Default,Arial,28,&H00FFFFFF,&H000000FF,&H00000000,&H80000000,0,0,0,0,100,100,0,0,1,2,0,2,10,10,10,1\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n'
events=[]
for i,t in enumerate(text):events.append(f'Dialogue: 0,0:00:{i*3:05.2f},0:00:{i*3+2.5:05.2f},Default,,0,0,0,,'+t.replace('\n',r'\N'))
(OUT/'golden.ass').write_text(header+'\n'.join(events)+'\n',encoding='utf8')
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP))
ffmpeg=shutil.which('ffmpeg'); assert ffmpeg
with (LOGS/'fixtures.log').open('x',encoding='utf8') as out:
    args=['-hide_banner','-y','-i',str(BASE/'tests/rtss-restart-loop-20261004/rtss-stress-2k30.mp4'),'-i',str(OUT/'golden.srt'),'-i',str(OUT/'golden.ass'),'-t','20','-map','0:v','-map','1:s','-map','2:s','-c','copy',str(OUT/'golden.mkv')]
    subprocess.run([ffmpeg,*args],env=env,stdout=out,stderr=subprocess.STDOUT,timeout=60,check=True)
    subprocess.run([ffmpeg,'-hide_banner','-y','-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=5','-c:a','pcm_s16le',str(OUT/'tone.wav')],env=env,stdout=out,stderr=subprocess.STDOUT,timeout=30,check=True)
print(OUT)
