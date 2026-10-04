"""Bounded real-GPU native SDK/interop checks, isolated logs/TEMP on E:."""
import importlib.util, json, os, subprocess, sys, time
from pathlib import Path
BASE=Path('E:/项目/Veyra');TASK='runtime-size-20261004'
logs,tests,tmp=(BASE/p/TASK for p in ('logs','tests','tmp'))
for p in (logs,tests,tmp):p.mkdir(parents=True,exist_ok=True)
label=sys.argv[1] if len(sys.argv)>1 else 'native-v1'
if not label.replace('-','').isalnum():raise SystemExit('invalid label')
env=os.environ.copy();env['PATH']=os.pathsep.join((env['WINDIR']+'/System32',env['WINDIR'],env['WINDIR']+'/System32/Wbem'));env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'))
command=[str(BASE/'build'/TASK/'veyra_vfg_gpu_tests.exe'),str(BASE/'test-packages/runtime-size-20261004/Veyra-2.0.2-slim-20261004-win64-portable/runtime/nvidia-vfg'),*(sys.argv[2:] or ['1280','720'])]
spec=importlib.util.spec_from_file_location('modules',Path(__file__).with_name('runtime-size-modules.py'));mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
loaded=set();start=time.monotonic()
with (logs/(label+'.log')).open('x',encoding='utf8') as out:
    with subprocess.Popen(command,cwd=tests,env=env,stdout=out,stderr=subprocess.STDOUT) as child:
        while child.poll() is None:
            loaded.update(mod.modules(child.pid))
            if time.monotonic()-start>290:child.kill();child.wait();break
            time.sleep(.25)
        code=child.returncode if time.monotonic()-start<=290 else 124
(logs/(label+'.json')).write_text(json.dumps({'command':command,'returncode':code,'timeoutSeconds':290,'log':str(logs/(label+'.log')),'loadedModules':sorted(loaded)},ensure_ascii=False,indent=2),encoding='utf8')
assert not any(Path(p).name.lower().startswith('npp') for p in loaded),'Unexpected NPP module in native test'
assert any(Path(p).name.lower()=='nvvfxvideoframegeneration.dll' for p in loaded),'No VFG plugin observed'
print('NATIVE GPU TEST',code,logs/(label+'.log'))
print('\n'.join((logs/(label+'.log')).read_text(encoding='utf8',errors='replace').splitlines()[-18:]))
raise SystemExit(code)
