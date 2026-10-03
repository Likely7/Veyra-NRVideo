"""Bounded real product regressions; SDK and all test artifacts remain outside Git."""
import json, os, re, shutil, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-integration-20261003'
BUILD=BASE/'build'/TASK;APP=BASE/'tests'/TASK/'app';QT=Path('C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64')
label=sys.argv[1];mode=sys.argv[2] if len(sys.argv)>2 else 'unit'
if not label.replace('-','').isalnum():raise SystemExit('invalid label')
out=BASE/'tests'/TASK/label;logs=BASE/'logs'/TASK/label;tmp=BASE/'tmp'/TASK/label
for p in (out,logs,tmp):p.mkdir(parents=True,exist_ok=False)
if not APP.exists():shutil.copytree(BASE/'releases/2.0.2/Veyra-2.0.2-win64-portable',APP)
names=['veyra_qml_ui','veyra_vfg_export_probe','veyra_vfg_settings_tests','veyra_preset_library_tests','veyra_repair_preset_tests','veyra_repair_contract_tests','veyra_effect_chain_tests','veyra_qml_quick_tests']
for name in names:shutil.copy2(BUILD/(name+'.exe'),APP/(name+'.exe'))
shutil.copytree(BUILD/'shaders',APP/'shaders',dirs_exist_ok=True)
shutil.copytree(ROOT/'qml',APP/'qml',dirs_exist_ok=True)
shutil.copytree(ROOT/'tests/qml/quick',APP/'qml-tests',dirs_exist_ok=True)
for name in ('Qt6Test.dll','Qt6QuickTest.dll'):shutil.copy2(QT/'bin'/name,APP/name)
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),
    VEYRA_VFG_RUNTIME=str(BASE/'deps/vfg-python-20261003/nvvfx/libs'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
results=[]
def run(name,exe,args=(),overrides=None):
    child=env.copy();child.update(overrides or {});child['VEYRA_LOG_FILE']=str(logs/(name+'-engine.log'))
    with (logs/(name+'.log')).open('xb') as log:
        try:code=subprocess.run(['.\\'+exe,*map(str,args)],executable=str(APP/exe),cwd=APP,env=child,stdout=log,stderr=subprocess.STDOUT,timeout=290).returncode
        except subprocess.TimeoutExpired:code=124
    result={'test':name,'exit':code,'log':str(logs/(name+'.log'))};results.append(result)
    (logs/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
    print(result,flush=True)
    if code:raise SystemExit('TEST FAILED '+name+'\n'+(logs/(name+'.log')).read_text(encoding='utf8',errors='replace')[-5000:])
    return result
if mode=='unit':
    for name in names[2:7]:run(name,name+'.exe',[out/'settings'] if name=='veyra_vfg_settings_tests' else [out/'repair-presets.txt'] if name=='veyra_repair_preset_tests' else [])
    run('qml-quick','veyra_qml_quick_tests.exe',['-input',APP/'qml-tests'],{'QT_QPA_PLATFORM':'offscreen','QT_QUICK_BACKEND':'software','QT_QUICK_CONTROLS_STYLE':'Basic','QT_QPA_PLATFORM_PLUGIN_PATH':str(QT/'plugins/platforms'),'PATH':str(QT/'bin')+os.pathsep+env['PATH']})
elif mode=='stage':pass
elif mode=='export':
    rate=int(sys.argv[3]) if len(sys.argv)>3 else 30;width=int(sys.argv[4]) if len(sys.argv)>4 else 1280;height=width*9//16
    media=out/'input.mp4'
    with (logs/'fixture.log').open('xb') as log:subprocess.run(['ffmpeg','-v','warning','-n','-f','lavfi','-i',f'testsrc2=size={width}x{height}:rate={rate}','-f','lavfi','-i','sine=frequency=440:sample_rate=48000','-t',str(8/rate),'-c:v','libx264','-preset','fast','-crf','12','-c:a','aac','-shortest',str(media)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90,check=True)
    cases=[(m,q) for q in range(3) for m in range(2,9)] if len(sys.argv)<6 else [(int(sys.argv[5]),int(sys.argv[6]))]
    flags=sys.argv[7:]
    for m,q in cases:
        name=f'{width}p-{rate}fps-{m}x-q{q}';file=out/(name+'.mp4')
        result=run(name,'veyra_vfg_export_probe.exe',[media,file,m,q,*flags])
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_streams','-show_format','-of','json',str(file)],env=env,timeout=30))
        video=next(s for s in probe['streams'] if s['codec_type']=='video');audio=next(s for s in probe['streams'] if s['codec_type']=='audio')
        assert video['codec_name']=='hevc' and int(video['nb_read_frames'])==8*m,probe
        num,den=map(int,video['avg_frame_rate'].split('/'));assert abs(num/den-rate*m)<rate*m*0.00001,probe
        num,den=map(int,video['r_frame_rate'].split('/'));assert num/den==rate*m,probe
        assert abs(float(video['duration'])-8/rate)<1/(rate*m) and abs(float(audio['duration'])-8/rate)<0.06,probe
        engine=(logs/(name+'.log')).read_text(encoding='utf8',errors='replace')
        assert 'backend=NVIDIA-VFG' in engine and 'nvenc' in engine.lower() and 'substituting' not in engine
        assert not re.search(r'\[ERROR\]|device removed|not alive',engine),engine[-5000:]
        result['probe']=probe;(logs/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
else:raise SystemExit('unknown mode')
print('VFG PRODUCT PASS',mode,APP,flush=True)
