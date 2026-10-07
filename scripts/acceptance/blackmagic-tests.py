"""Bounded serial source contracts and real GPU pixel checks; no signal claims."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,time,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='blackmagic-capture-20261007'
label=sys.argv[1];assert label.replace('-','').isalnum()
BUILD=BASE/'build'/TASK;PACKAGE=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-NVIDIA-win64-portable'
out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK/label
for p in (out,tmp,logs):p.mkdir(parents=True,exist_ok=False)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/blackmagic-control.py')],cwd=ROOT,check=True)
assert not any(p.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_hdr_color_tests.exe') for p in psutil.process_iter(['name']))
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'))
env['PATH']=os.pathsep.join(map(str,(BUILD,PACKAGE,Path(env['WINDIR'])/'System32',Path(env['WINDIR']),Path(env['WINDIR'])/'System32/Wbem')))
cases=[('color','veyra_capture_color_tests.exe',[]),('selection','veyra_capture_format_selection_tests.exe',[]),('gpu','veyra_hdr_color_tests.exe',[]),('devices','veyra_capture_tests.exe',['--list'])]
results=[]
for name,exe,args in cases:
 log=logs/(name+'.log');child=env.copy();child['VEYRA_LOG_FILE']=str(logs/(name+'-engine.log'))
 cmd=[str(BUILD/exe),*args];start=time.monotonic()
 with log.open('xb') as f:
  try:rc=subprocess.run(cmd,cwd=out,env=child,stdout=f,stderr=subprocess.STDOUT,timeout=290).returncode
  except subprocess.TimeoutExpired:rc=124
 text=log.read_text(encoding='utf8',errors='replace');passed=rc==0 and not re.search(r'^FAIL\b|\bpass=0\b',text,re.M)
 if name=='color':passed=passed and 'WDM connects matching output' in text and 'HDYC SD dimensions keep BT709' in text
 if name=='selection':passed=passed and 'capture format selection failures=0' in text
 if name=='gpu':passed=passed and len(re.findall(r'BLACKMAGIC_HDYC_GPU flip=[01] identical709=1.*pass=1',text))==2 and len(re.findall(r'CAPTURE_GPU subtype=43594448.*pass=1',text))==2
 if name=='devices':passed=rc in (0,1) and not re.search(r'^FAIL\b',text,re.M)
 item={'case':name,'command':cmd,'exit':rc,'seconds':time.monotonic()-start,'passed':bool(passed),'checksPrinted':len(re.findall(r'^PASS\b|\bpass=1\b',text,re.M)),'exeSha256':hashlib.sha256((BUILD/exe).read_bytes()).hexdigest(),'log':str(log)}
 results.append(item);(logs/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps(item,ensure_ascii=False),flush=True)
 print('\n'.join(text.splitlines()[-5:]),flush=True)
 if not passed:raise SystemExit(1)
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/blackmagic-control.py')],cwd=ROOT,check=True)
