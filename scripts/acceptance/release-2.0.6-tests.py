"""Fresh serial release checks; all fixtures, profiles and output stay on E:."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,time,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.6-20261007'
BUILD=BASE/'build'/TASK;APP=BASE/'tests'/TASK/'app'
def sha(p):
 with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def guard():subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.6-control.py')],check=True)
guard();phase=sys.argv[1]
assert not any(x.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_vfg_gpu_tests.exe','veyra_vfg_export_probe.exe') for x in psutil.process_iter(['name']))
if phase=='prepare':
 old=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-NVIDIA-win64-portable'
 assert not APP.exists();shutil.copytree(old,APP)
 shutil.copyfile(BUILD/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')
 for folder in ('qml','shaders'):
  source=ROOT/folder if folder=='qml' else BUILD/folder
  for p in source.rglob('*'):
   if p.is_file():
    q=APP/folder/p.relative_to(source);q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
 shutil.copyfile(ROOT/'scripts/acceptance/vfg-diagnosis.qml',APP/'qml/Veyra/VfgDiagnosis.qml')
 print('PRIVATE TEST APP',APP,sha(APP/'veyra_qml_ui.exe'),flush=True)
else:
 assert phase in ('units','gpu')
 label=phase;out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK/label
 for p in (out,tmp,logs):p.mkdir(parents=True,exist_ok=False)
 env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
 env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),VEYRA_LOCAL_DATA_DIR=str(out/'profile'))
 specs=[('veyra_capture_color_tests',[]),('veyra_capture_format_selection_tests',[]),('veyra_vfg_settings_tests',[str(out/'settings')])] if phase=='units' else [('veyra_hdr_color_tests',[]),('veyra_vfg_gpu_tests',[str(APP/'runtime/nvidia-vfg'),'1280','720','async'])]
 results=[]
 for name,args in specs:
  exe=APP/(name+'.exe');shutil.copyfile(BUILD/exe.name,exe);log=logs/(name+'.log');child=env.copy();child['VEYRA_LOG_FILE']=str(logs/(name+'-engine.log'));start=time.monotonic()
  with log.open('xb') as f:
   try:rc=subprocess.run([str(exe),*args],cwd=APP,env=child,stdout=f,stderr=subprocess.STDOUT,timeout=290).returncode
   except subprocess.TimeoutExpired:rc=124
  text=log.read_text(encoding='utf8',errors='replace');passed=rc==0 and not re.search(r'^FAIL\b|\bpass=0\b',text,re.M)
  item=dict(target=name,command=[str(exe),*args],exit=rc,seconds=time.monotonic()-start,passed=passed,exeSha256=sha(exe),log=str(log),tail=text.splitlines()[-8:]);results.append(item)
  (logs/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(item,ensure_ascii=False),flush=True)
  if not passed:raise SystemExit(1)
guard()
