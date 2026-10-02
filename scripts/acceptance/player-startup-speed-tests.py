"""Isolated, bounded product regression. All artifacts live under the task root."""
import json, math, os, shutil, struct, subprocess, sys, wave
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='player-startup-speed-20261003'
BUILD=BASE/'build'/TASK;TESTS=BASE/'tests'/TASK;APP=TESTS/'app'
QT=Path('C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64')
def main():
    label=sys.argv[1]
    if not label.replace('-','').isalnum():raise ValueError('label')
    out=TESTS/label;out.mkdir(exist_ok=False)
    tmp=BASE/'tmp'/TASK/label;tmp.mkdir(exist_ok=False)
    env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),
        PATH=str(QT/'bin')+os.pathsep+'C:/veyra-deps/ffmpeg-ps5-dav1d-installed/bin'+os.pathsep+env['PATH'])
    if not APP.exists():
        shutil.copytree(BASE/'tests/field-2.0.1-20261002/app',APP)
    for f in BUILD.glob('*.exe'):shutil.copy2(f,APP/f.name)
    shutil.copytree(ROOT/'qml',APP/'qml',dirs_exist_ok=True)
    shutil.copytree(ROOT/'tests/qml/quick',APP/'qml-tests',dirs_exist_ok=True)
    for name in ('Qt6Test.dll','Qt6QuickTest.dll'):shutil.copy2(QT/'bin'/name,APP/name)
    tone=out/'tone.wav'
    with wave.open(str(tone),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(48000)
        f.writeframes(b''.join(struct.pack('<hh',*(2*[int(12000*math.sin(2*math.pi*440*i/48000))])) for i in range(240000)))
    env.update(QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',QT_QUICK_CONTROLS_STYLE='Basic',
        QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_EXPORT_TEST_ARTIFACTS=str(out/'qml-images'))
    (out/'qml-images').mkdir()
    results=[]
    for name in ('audio_playback_rate','live_timing','xbox','repair_contract','qml_data','ui_i18n','qml_quick'):
        args=[str(tone)] if name=='audio_playback_rate' else [str(out/'data')] if name=='qml_data' else []
        with (out/(name+'.log')).open('xb') as log:
            try:code=subprocess.run([str(APP/('veyra_'+name+'_tests.exe')),*args],cwd=APP,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=290).returncode
            except subprocess.TimeoutExpired:code=124
        results.append({'name':name,'exit':code});print(results[-1],flush=True)
    (out/'summary.json').write_text(json.dumps(results,indent=2),encoding='utf8')
    raise SystemExit(any(r['exit'] for r in results))
if __name__=='__main__':main()
