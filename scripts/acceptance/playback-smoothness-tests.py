"""Bounded native rate, subtitle, overlay, language and hardware-map regressions."""
import json
import os
from pathlib import Path
import subprocess
import sys

BASE=Path('E:/项目/Veyra');TASK='playback-smoothness-20261004'
build=BASE/'build'/TASK;logs=BASE/'logs'/TASK;tmp=BASE/'tmp'/TASK
env=os.environ.copy()
env.update(TEMP=str(tmp),TMP=str(tmp),PATH=';'.join([
    'C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64/bin',
    'C:/veyra-deps/ffmpeg-ps5-dav1d-installed/bin',
    'C:/veyra-deps/tools-installed/x64-windows/bin',env['PATH']]))
fixtures=BASE/'tests'/TASK/'fixtures'
cases=[('audio-rates-v3','veyra_audio_playback_rate_tests',[str(fixtures/'tone.wav')]),
       ('subtitle-after-v2','veyra_subtitle_text_tests',[str(fixtures)]),
       ('overlay-v3','veyra_subtitle_overlay_tests',[]),
       ('i18n-v2','veyra_ui_i18n_tests',[]),
       ('availability-v1','veyra_effect_availability_tests',[])]
results=[]
for label,target,args in cases:
    log=logs/(label+'.log')
    with log.open('x',encoding='utf8') as out:
        try:code=subprocess.run([str(build/(target+'.exe')),*args],env=env,cwd=tmp,stdout=out,stderr=subprocess.STDOUT,timeout=250).returncode
        except subprocess.TimeoutExpired:code=124
    print(label,code,log,flush=True)
    results.append(dict(test=target,log=str(log),exitCode=code,passed=code==0))
assert not (logs/'native-tests.json').exists()
(logs/'native-tests.json').write_text(json.dumps(results,indent=2),encoding='utf8')
raise SystemExit(0 if all(r['passed'] for r in results) else 1)
