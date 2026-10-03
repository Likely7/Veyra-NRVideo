"""Bounded real-GPU native SDK/interop checks, isolated logs/TEMP on E:."""
import json, os, subprocess, sys
from pathlib import Path
BASE=Path('E:/项目/Veyra');TASK='vfg-integration-20261003'
logs,tests,tmp=(BASE/p/TASK for p in ('logs','tests','tmp'))
for p in (logs,tests,tmp):p.mkdir(parents=True,exist_ok=True)
label=sys.argv[1] if len(sys.argv)>1 else 'native-v1'
if not label.replace('-','').isalnum():raise SystemExit('invalid label')
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'))
command=[str(BASE/'build'/TASK/'veyra_vfg_gpu_tests.exe'),str(BASE/'deps/vfg-python-20261003/nvvfx/libs'),*(sys.argv[2:] or ['1280','720'])]
with (logs/(label+'.log')).open('x',encoding='utf8') as out:
    try:code=subprocess.run(command,cwd=tests,env=env,stdout=out,stderr=subprocess.STDOUT,timeout=290).returncode
    except subprocess.TimeoutExpired:code=124
(logs/(label+'.json')).write_text(json.dumps({'command':command,'returncode':code,'timeoutSeconds':290,'log':str(logs/(label+'.log'))},ensure_ascii=False,indent=2),encoding='utf8')
print('NATIVE GPU TEST',code,logs/(label+'.log'))
print('\n'.join((logs/(label+'.log')).read_text(encoding='utf8',errors='replace').splitlines()[-18:]))
raise SystemExit(code)
