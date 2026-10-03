"""Real GUI hot switches, persistence and process-isolated VFG export; each run <300s."""
import json, os, shutil, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='vfg-integration-20261003'
APP=BASE/'tests'/TASK/'app';BUILD=BASE/'build'/TASK
FFPROBE=shutil.which('ffprobe');assert FFPROBE
label=sys.argv[1];out=BASE/'tests'/TASK/label;logs=BASE/'logs'/TASK/label;tmp=BASE/'tmp'/TASK/label
packaged=len(sys.argv)>2 and sys.argv[2]=='package'
if packaged:
    APP=Path(sys.argv[3]).resolve();assert APP.is_relative_to(BASE.resolve()) and (APP/'vfg-runtime-manifest.json').is_file()
for p in (out,logs,tmp):p.mkdir(parents=True,exist_ok=False)
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),CUDA_CACHE_PATH=str(tmp/'cuda-cache'),VEYRA_VFG_RUNTIME=str(BASE/'deps/vfg-python-20261003/nvvfx/libs'),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
for key in ('QT_QPA_PLATFORM','QT_QUICK_BACKEND','VEYRA_UI_RHI'):env.pop(key,None)
if packaged:
    env.pop('VEYRA_VFG_RUNTIME',None)
    windir=Path(env['WINDIR']);env['PATH']=os.pathsep.join(map(str,(windir/'System32',windir,windir/'System32/Wbem')))
else:
    for name in ('veyra_qml_ui.exe',):shutil.copy2(BUILD/name,APP/name)
    shutil.copytree(ROOT/'qml',APP/'qml',dirs_exist_ok=True)
media=out/'preview-720p30.mp4';short=BASE/'tests'/TASK/'export-basic-v2/input.mp4'
previous=BASE/'tests'/TASK/'ui-v1/preview-720p30.mp4'
if previous.exists() and label!='ui-v1':media=previous
else:
    with (logs/'fixture.log').open('xb') as log:subprocess.run(['ffmpeg','-v','warning','-n','-f','lavfi','-i','testsrc2=size=1280x720:rate=30','-t','90','-c:v','libx264','-preset','ultrafast','-crf','28','-an',str(media)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90,check=True)
profile=out/'profile';outputs=out/'outputs';outputs.mkdir()
phases=('run','restore','missing')
if len(sys.argv)>2 and not packaged:
    assert sys.argv[2]=='missing-only'
    phases=('missing',)
    shutil.copytree(BASE/'tests'/TASK/'ui-v2/profile',profile)
main=APP/'qml/Veyra/Main.qml';original=main.read_bytes();source=original.decode('utf8');at=source.rfind('}')
results=[]
try:
    for phase in phases:
        child=env.copy();child['VEYRA_LOG_FILE']=str(logs/(phase+'-engine.log'))
        data=profile
        if phase=='missing':
            data=out/'missing-profile';shutil.copytree(profile,data);child['VEYRA_VFG_RUNTIME']=str(tmp/'missing-runtime')
        loader='\nLoader { source: '+json.dumps((ROOT/'scripts/acceptance/vfg-ui.qml').as_uri())+'; onLoaded: { item.media='+json.dumps(str(media))+'; item.shortMedia='+json.dumps(str(short))+'; item.evidence='+json.dumps(str(out))+'; item.phase='+json.dumps(phase)+' } }\n'
        main.write_text(source[:at]+loader+source[at:],encoding='utf8')
        with (logs/(phase+'.log')).open('xb') as log:
            code=subprocess.run(['.\\veyra_qml_ui.exe','--page','pro','--size','1280x900','--reduced-motion','--export-out',str(outputs),'--data-dir',str(data),'--exit-after','240000'],executable=str(APP/'veyra_qml_ui.exe'),cwd=APP,env=child,stdout=log,stderr=subprocess.STDOUT,timeout=270).returncode
        text=(logs/(phase+'.log')).read_text(encoding='utf8',errors='replace')
        marker={'run':'VFG_UI_PASS','restore':'VFG_UI_RESTORE_PASS','missing':'VFG_UI_MISSING_PASS'}[phase]
        assert code==0 and marker in text and 'VFG_UI_FAIL' not in text,text[-5000:]
        results.append({'phase':phase,'returncode':code,'marker':marker});print(results[-1],flush=True)
finally:main.write_bytes(original)
if 'run' in phases:
    files=list(outputs.glob('*.mp4'));assert len(files)==1,files
    probe=json.loads(subprocess.check_output([FFPROBE,'-v','error','-count_frames','-show_streams','-of','json',str(files[0])],env=env,timeout=30))
    v=next(s for s in probe['streams'] if s['codec_type']=='video');assert int(v['nb_read_frames'])==64 and v['r_frame_rate']=='240/1',probe
    results.append({'export':str(files[0]),'probe':probe})
(logs/'summary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf8')
print('VFG UI PRODUCT PASS',out)
