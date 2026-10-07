"""Bounded serial VFG GPU interop/pixel tests; all outputs stay on E:."""
from pathlib import Path
import argparse,json,os,subprocess,time,psutil
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-optimization-20261007'
p=argparse.ArgumentParser();p.add_argument('label');p.add_argument('--width',type=int,default=1280);p.add_argument('--height',type=int,default=720);p.add_argument('--async-submit',action='store_true');p.add_argument('--pair-cache-baseline',action='store_true');a=p.parse_args()
assert a.label.replace('-','').isalnum()
subprocess.run(['python','-B',str(ROOT/'scripts/acceptance/vfg-opt-control.py')],cwd=ROOT,check=True)
assert not any(x.info['name'].lower() in ('veyra.exe','veyra_qml_ui.exe','veyra_vfg_gpu_tests.exe','veyra_vfg_export_probe.exe') for x in psutil.process_iter(['name']))
log=BASE/'logs'/TASK/(a.label+'.log');out=BASE/'tests'/TASK/a.label;tmp=BASE/'tmp'/TASK/a.label
for x in (out,tmp):x.mkdir(parents=True,exist_ok=False)
env={k:v for k,v in os.environ.items() if not k.startswith(('VEYRA_','QSG_','QT_QUICK_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'))
env['PATH']=os.pathsep.join((env['WINDIR']+'/System32',env['WINDIR'],env['WINDIR']+'/System32/Wbem'))
exe=BASE/('tests/'+TASK+'/bin/pair-cache-v1' if a.pair_cache_baseline else 'build/'+TASK)/'veyra_vfg_gpu_tests.exe'
cmd=[str(exe),str(BASE/'tests'/TASK/'app/runtime/nvidia-vfg'),str(a.width),str(a.height)]
if a.async_submit:cmd.append('async')
start=time.monotonic()
with log.open('x',encoding='utf8') as f:
 try:rc=subprocess.run(cmd,cwd=out,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=290).returncode
 except subprocess.TimeoutExpired:rc=124
receipt={'command':cmd,'exit':rc,'seconds':time.monotonic()-start,'log':str(log)}
log.with_suffix('.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf8')
print(receipt);print('\n'.join(log.read_text(encoding='utf8',errors='replace').splitlines()[-15:]));raise SystemExit(rc)
