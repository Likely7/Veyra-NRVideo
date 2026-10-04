"""Launch one owned, isolated real-player comparison; no desktop input is sent."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra'); TASK='playback-smoothness-20261004'
sources={
    '202':BASE/'releases/2.0.2/Veyra-2.0.2-win64-portable',
    '203':BASE/'releases/release-2.0.3-20261004/Veyra-2.0.3-NVIDIA-win64-portable',
    'fixed':BASE/'test-packages/rtss-restart-loop-20261004/Veyra-2.0.3-rtssfix-NVIDIA-win64-portable',
    'candidate':BASE/'releases/release-2.0.3-20261004/Veyra-2.0.3-NVIDIA-win64-portable',
}
version,mode,label=sys.argv[1:4]
run=BASE/'tests'/TASK/label; logs=BASE/'logs'/TASK/label; tmp=BASE/'tmp'/TASK/label
for p in (run,logs,tmp):p.mkdir(parents=True,exist_ok=False)
app=run/'app'; source=sources[version]
def copy(src,dst):
    # Only immutable dependencies are linked. Mutable QML/config/log/EXE files
    # always have independent bytes, never overwrite published files via a link.
    path=Path(src)
    if path.suffix.lower() in {'.dll','.hsaco','.ptx','.onnx','.f16','.bin','.cubin'}:
        os.link(src,dst)
    else:shutil.copy2(src,dst)
    return dst
shutil.copytree(source,app,copy_function=copy,ignore=shutil.ignore_patterns('*.log','logs','data','profiles'))
if version=='candidate':
    shutil.copy2(BASE/'build'/TASK/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
    shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
main=app/'qml/Veyra/Main.qml'; text=main.read_text(encoding='utf8'); at=text.rfind('}')
media=BASE/'tests/rtss-restart-loop-20261004/rtss-stress-2k30.mp4'
probe=(ROOT/'scripts/acceptance/playback-smoothness-probe.qml').as_uri()
if mode=='functional':
    probe=(ROOT/'scripts/acceptance/playback-smoothness-functional.qml').as_uri()
    media=BASE/'tests'/TASK/'fixtures/golden.mkv'
if mode=='layout':
    probe=(ROOT/'scripts/acceptance/playback-smoothness-layout.qml').as_uri()
if mode=='capability':
    probe=(ROOT/'scripts/acceptance/playback-smoothness-capability.qml').as_uri()
limit='; item.limit=75' if 'short' in sys.argv[4:] and mode not in ('functional','layout') else ''
loader='\n Loader { source: '+json.dumps(probe)+'; onLoaded: { item.media='+json.dumps(str(media).replace('\\','/'))+'; item.mode='+json.dumps(mode)+limit+' } }\n'
main.write_text(text[:at]+loader+text[at:],encoding='utf8')
profile=run/'profile'; profile.mkdir()
(profile/'preferences.json').write_text(json.dumps({'language':'zh-CN','overlayCompat':'off'}),encoding='utf8')
env=os.environ.copy()
for k in ('QSG_RHI_BACKEND','QT_QUICK_BACKEND','VEYRA_DATA_DIR','VEYRA_RUNTIME_DIR','VEYRA_QML_UI_PROBE'):
    env.pop(k,None)
env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logs/'player.log'),
           QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
if 'software' in sys.argv[4:]:env['QT_QUICK_BACKEND']='software'
with (logs/'console.log').open('xb') as out:
    proc=subprocess.Popen([str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),
                           '--page','pro','--exit-after','270000'],cwd=app,env=env,stdout=out,stderr=subprocess.STDOUT)
receipt={'pid':proc.pid,'version':version,'mode':mode,'app':str(app),'logs':str(logs),'label':label}
helper=ROOT/'scripts/acceptance/playback-smoothness-background.py'
with (logs/'helper.log').open('xb') as out:
    helper_proc=subprocess.Popen([sys.executable,'-B',str(helper)],cwd=tmp,env=env,stdout=out,stderr=subprocess.STDOUT)
receipt['helperPid']=helper_proc.pid
(run/'launch.json').write_text(json.dumps(receipt,indent=2),encoding='utf8')
print(json.dumps(receipt),flush=True)
if '--wait' in sys.argv[4:]:
    try:code=proc.wait(timeout=280)
    except subprocess.TimeoutExpired:
        proc.kill();proc.wait(timeout=5);code=124
    finally:
        if helper_proc.poll() is None:helper_proc.terminate();helper_proc.wait(timeout=5)
    receipt['exitCode']=code
    log=(logs/'player.log').read_text(encoding='utf8',errors='replace')
    marker={'functional':'PLAYBACK_FUNCTION_PASS','layout':'PLAYBACK_LAYOUT_FULLSCREEN_PASS','capability':'PLAYBACK_CAPABILITY_PASS'}.get(mode)
    receipt['passed']=code==0 and (marker is None or marker in log) and not any(x in log for x in ('PLAYBACK_FUNCTION_FAIL','PLAYBACK_LAYOUT_FAIL','PLAYBACK_CAPABILITY_FAIL','ReferenceError:','TypeError:'))
    (run/'result.json').write_text(json.dumps(receipt,indent=2),encoding='utf8')
    print(json.dumps(receipt),flush=True)
    raise SystemExit(0 if receipt['passed'] else 1)
