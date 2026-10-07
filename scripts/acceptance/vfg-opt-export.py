"""Serial real graph/NVENC export checks, including frame counts and PTS."""
from pathlib import Path
import argparse,json,os,re,shutil,subprocess,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007';APP=BASE/'tests'/TASK/'app'
p=argparse.ArgumentParser();p.add_argument('label');a=p.parse_args();assert a.label.replace('-','').isalnum()
subprocess.run(['python','-B',str(ROOT/'scripts/acceptance/vfg-opt-control.py')],cwd=ROOT,check=True)
assert not any(x.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_vfg_gpu_tests.exe','veyra_vfg_export_probe.exe') for x in psutil.process_iter(['name']))
out,logs,tmp=(BASE/k/TASK/a.label for k in ('tests','logs','tmp'))
for x in (out,logs,tmp):x.mkdir(parents=True,exist_ok=False)
probe=shutil.which('ffprobe');ffmpeg=shutil.which('ffmpeg');assert probe and ffmpeg
shutil.copy2(BASE/'build'/TASK/'veyra_vfg_export_probe.exe',APP/'veyra_vfg_export_probe.exe')
env={k:v for k,v in os.environ.items() if not k.startswith(('VEYRA_','QSG_','QT_QUICK_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),VEYRA_VFG_RUNTIME=str(APP/'runtime/nvidia-vfg'))
env['PATH']=os.pathsep.join((env['WINDIR']+'/System32',env['WINDIR'],env['WINDIR']+'/System32/Wbem'))
source=BASE/'tests/runtime-size-20261004/export-basic-v2/input.mp4';assert source.is_file();results=[]
def run(name,media,m,q,flags=(),expected='success'):
 file=out/(name+'.mp4');child=env.copy()
 if expected=='missing':child['VEYRA_VFG_RUNTIME']=str(tmp/'missing-runtime')
 log=logs/(name+'.log');cmd=[str(APP/'veyra_vfg_export_probe.exe'),str(media),str(file),str(m),str(q),*flags]
 with log.open('xb') as f:
  try:rc=subprocess.run(cmd,cwd=APP,env=child,stdout=f,stderr=subprocess.STDOUT,timeout=290).returncode
  except subprocess.TimeoutExpired:rc=124
 text=log.read_text(encoding='utf8',errors='replace');item={'case':name,'command':cmd,'exit':rc,'expected':expected}
 if expected!='success':
  assert rc==(1 if expected=='missing' else 0) and not file.exists(),text[-5000:]
  assert ('cancelled=1' if expected=='cancel' else 'ok=0') in text,text[-5000:]
 else:
  assert rc==0 and 'backend=NVIDIA-VFG' in text and 'SDK submission=graph owner' in text and 'nvenc' in text.lower(),text[-5000:]
  assert not re.search(r'\[ERROR\s*\]|\[FATAL\s*\]|device removed|not alive|substituting',text),text[-5000:]
  data=json.loads(subprocess.check_output([probe,'-v','error','-count_frames','-show_streams','-of','json',str(file)],timeout=30))
  v=next(x for x in data['streams'] if x['codec_type']=='video')
  assert v['codec_name']=='hevc' and int(v['nb_read_frames'])==8*m,data
  assert all(abs(int(v[k].split('/')[0])/int(v[k].split('/')[1])-30*m)<.00001 for k in ('avg_frame_rate','r_frame_rate')),data
  assert abs(float(v['duration'])-8/30)<1/(30*m),data
  for audio in (x for x in data['streams'] if x['codec_type']=='audio'):assert abs(float(audio['duration'])-8/30)<.06,data
  packets=json.loads(subprocess.check_output([probe,'-v','error','-select_streams','v','-show_entries','packet=pts_time','-of','json',str(file)],timeout=30))['packets']
  pts=[float(x['pts_time']) for x in packets];assert len(pts)==8*m and all(abs(b-a-1/(30*m))<.000003 for a,b in zip(pts,pts[1:])),pts
  if '--nr' in flags:assert 'nr=1' in text and (v['width'],v['height'])==(3840,2160),text[-5000:]
  item.update(frames=8*m,probe=data,pts=pts)
 results.append(item);(logs/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8');print('VFG EXPORT PASS',name,flush=True)
for quality in range(3):
 for multiplier in range(2,9):run(f'720p30-q{quality}-{multiplier}x',source,multiplier,quality)
gta=out/'gta-4k30-eight-frames.mp4'
with (logs/'fixture.log').open('xb') as f:
 subprocess.run([ffmpeg,'-v','warning','-n','-ss','42','-i','E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4','-map','0:v:0','-frames:v','8','-an','-c:v','libx264','-threads','6','-preset','fast','-crf','18',str(gta)],env=env,stdout=f,stderr=subprocess.STDOUT,timeout=90,check=True)
run('gta-4k30-native-nr-vfg4',gta,4,1,('--nr',))
run('cancel-vfg8',source,8,1,('--cancel',),expected='cancel')
run('missing-runtime-vfg8',source,8,1,expected='missing')
print('VFG EXPORT SUITE PASS',len(results),logs,flush=True)
