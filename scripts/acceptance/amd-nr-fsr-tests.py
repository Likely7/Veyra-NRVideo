"""Bounded serial pixel regressions with real FSR and a test-only NR provider."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys,time,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='amd-nr-fsr-handoff-20261007'
label,phase=sys.argv[1:3];assert label.replace('-','').isalnum() and phase in ('before','after','abi','units')
BUILD=BASE/'build'/TASK;PACKAGE=BASE/'releases/publish-2.0.5-20261007/packages/Veyra-2.0.5-AMD-win64-portable'
out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK/label
for p in (out,tmp,logs):p.mkdir(parents=True,exist_ok=False)
guard=[sys.executable,'-B',str(ROOT/'scripts/acceptance/amd-nr-fsr-control.py')];subprocess.run(guard,check=True)
assert not any(p.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_amd_nr_graph_tests.exe') for p in psutil.process_iter(['name']))
for p in PACKAGE.glob('*.dll'):shutil.copyfile(p,out/p.name)
shutil.copytree(BUILD/'shaders',out/'shaders')
if phase in ('before','after'):
 targets=['veyra_amd_nr_graph_tests']
 d=out/'runtime/amd-nr';d.mkdir(parents=True);(d/'assets').mkdir()
 shutil.copyfile(BUILD/'lmxxf-test-runtime/LmxxfNrRuntime.dll',d/'LmxxfNrRuntime.dll')
 (out/'runtime/nvidia').mkdir()
 shutil.copytree(PACKAGE/'runtime_local/amd/fidelityfx',out/'runtime_local/amd/fidelityfx')
elif phase=='abi':
 targets=['veyra_lmxxf_nr_tests'];shutil.copytree(BUILD/'lmxxf-test-runtime',out/'lmxxf-test-runtime')
else:targets=['veyra_effect_chain_tests']
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),VEYRA_LOCAL_DATA_DIR=str(out/'profile'))
results=[]
for target in targets:
 exe=out/(target+'.exe');shutil.copyfile(BUILD/exe.name,exe);log=logs/(target+'.log')
 child=env.copy();child['VEYRA_LOG_FILE']=str(logs/(target+'-engine.log'));start=time.monotonic()
 with log.open('xb') as f:
  try:rc=subprocess.run([str(exe)],cwd=out,env=child,stdout=f,stderr=subprocess.STDOUT,timeout=290).returncode
  except subprocess.TimeoutExpired:rc=124
 text=log.read_text(encoding='utf8',errors='replace');passed=rc==0 and not re.search(r'^FAIL\b|\bpass=0\b',text,re.M)
 markers=re.findall(r'^AMD_NR_SR_MARKER .*',text,re.M)
 if phase=='before':
  failed=[re.search(r'case=(\S+)',m).group(1) for m in markers if 'pass=0' in m]
  passed=rc==1 and set(failed)=={'pre-fsr-one','pre-fsr-two','pre-fsr-temporal','pre-fsr-flow-file','pre-fsr-flow-xsx-1080','pre-blit-one'} and len(markers)==10 and 'D3D12_DEBUG errors=0' in text
 if phase=='after':passed=passed and len(markers)==10 and all('pass=1' in m for m in markers) and 'D3D12_DEBUG errors=0' in text
 item={'target':target,'phase':phase,'command':[str(exe)],'exit':rc,'seconds':time.monotonic()-start,'passed':bool(passed),'markers':markers,'exeSha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'providerSha256':hashlib.sha256((BUILD/'lmxxf-test-runtime/LmxxfNrRuntime.dll').read_bytes()).hexdigest(),'sourceGraphSha256':hashlib.sha256((ROOT/'src/pipeline/EnhanceGraph.cpp').read_bytes()).hexdigest(),'log':str(log)}
 results.append(item);(logs/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
 print(json.dumps(item,ensure_ascii=False),flush=True);print('\n'.join(text.splitlines()[-15:]),flush=True)
 if not passed:raise SystemExit(1)
subprocess.run(guard,check=True)
