"""Use the existing product GUI hot-switch/export checks in a private app/profile."""
from pathlib import Path
import argparse,json,os,re,shutil,subprocess,time,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007';APP=BASE/'tests'/TASK/'app'
p=argparse.ArgumentParser();p.add_argument('label');p.add_argument('--failure-only',action='store_true');a=p.parse_args();assert a.label.replace('-','').isalnum()
subprocess.run(['python','-B',str(ROOT/'scripts/acceptance/vfg-opt-control.py')],cwd=ROOT,check=True)
assert not any(x.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_vfg_gpu_tests.exe','veyra_vfg_export_probe.exe') for x in psutil.process_iter(['name']))
out,logs,tmp=(BASE/k/TASK/a.label for k in ('tests','logs','tmp'))
for x in (out,logs,tmp):x.mkdir(parents=True,exist_ok=False)
profile=out/'profile';profile.mkdir();(profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','gpuPriority':'normal','obsGameCapture':False}),encoding='utf8')
outputs=out/'outputs';outputs.mkdir()
media=Path('E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4')
short=BASE/'tests/runtime-size-20261004/export-basic-v2/input.mp4';assert short.is_file()
shutil.copy2(BASE/'build'/TASK/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
env={k:v for k,v in os.environ.items() if not k.startswith(('VEYRA_','QSG_','QT_QUICK_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
env['PATH']=os.pathsep.join((env['WINDIR']+'/System32',env['WINDIR'],env['WINDIR']+'/System32/Wbem'))
package=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-NVIDIA-win64-portable'
original=(package/'qml/Veyra/Main.qml').read_bytes();source=original.decode('utf8');at=source.rfind('}');main=APP/'qml/Veyra/Main.qml';results=[]
try:
 for phase in (['failure'] if a.failure_only else ['run','restore','missing','failure']):
  child=env.copy();data=profile
  child['VEYRA_LOG_FILE']=str(logs/(phase+'-engine.log'))
  if phase in ('missing','failure'):
   data=out/(phase+'-profile');shutil.copytree(profile,data)
  if phase=='missing':child['VEYRA_VFG_RUNTIME']=str(tmp/'missing-runtime')
  if phase=='failure':child['VEYRA_TEST_VFG_REJECT_RUN']='3'
  qml=ROOT/'scripts/acceptance'/('vfg-opt-failure.qml' if phase=='failure' else 'vfg-ui.qml')
  settings='item.media='+json.dumps(str(media))+';'
  if phase!='failure':settings+='item.shortMedia='+json.dumps(str(short))+';item.evidence='+json.dumps(str(out))+';item.phase='+json.dumps(phase)+';'
  loader='\nLoader { source: '+json.dumps(qml.as_uri())+'; onLoaded: { '+settings+' } }\n'
  main.write_text(source[:at]+loader+source[at:],encoding='utf8')
  start=time.monotonic()
  with (logs/(phase+'.log')).open('xb') as f:
   try:rc=subprocess.run([str(APP/'veyra_qml_ui.exe'),'--page','pro','--size','1280x900','--export-out',str(outputs),'--data-dir',str(data),'--exit-after','240000'],cwd=APP,env=child,stdout=f,stderr=subprocess.STDOUT,timeout=270).returncode
   except subprocess.TimeoutExpired:rc=124
  text=(logs/(phase+'.log')).read_text(encoding='utf8',errors='replace')
  marker={'run':'VFG_UI_PASS','restore':'VFG_UI_RESTORE_PASS','missing':'VFG_UI_MISSING_PASS','failure':'VFG_FAILURE_PASS'}[phase]
  engine=(logs/(phase+'-engine.log')).read_text(encoding='utf8',errors='replace')
  results.append({'phase':phase,'exit':rc,'seconds':time.monotonic()-start,'marker':marker,'passed':rc==0 and marker in text and not re.search(r'VFG_UI_FAIL|VFG_FAILURE_FAIL',text)})
  (logs/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
  assert rc==0 and marker in text and not re.search(r'VFG_UI_FAIL|VFG_FAILURE_FAIL',text),text[-6000:]
  if phase=='failure':assert 'injected VFG Run rejection' in engine and re.search(r'runtime component=6 disabled;.*multiplier=1;',engine),engine[-6000:]
  if phase=='run':
   ids=re.findall(r'\[export-worker\] started jobId=\d+ pid=(\d+)',text);assert len(ids)==1,ids
   worker=APP/'logs'/('export-worker-'+ids[0]+'.log');worker_text=worker.read_text(encoding='utf8',errors='replace')
   assert 'backend=NVIDIA-VFG vfgQuality=2' in worker_text and 'SDK submission=graph owner' in worker_text,worker_text[-4000:]
   shutil.copy2(worker,logs/worker.name)
  print(results[-1],flush=True)
finally:main.write_bytes(original)
if not a.failure_only:
 files=list(outputs.glob('*.mp4'));assert len(files)==1,files
 probe=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-count_frames','-show_streams','-of','json',str(files[0])],timeout=30))
 video=next(x for x in probe['streams'] if x['codec_type']=='video');assert int(video['nb_read_frames'])==64 and video['r_frame_rate']=='240/1',probe
 (logs/'export-probe.json').write_text(json.dumps(probe,indent=2),encoding='utf8')
print('VFG LIFECYCLE PASS',logs,flush=True)
