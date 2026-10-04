"""Start the sealed package with no SDK/path overrides; keep all runtime data outside."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

BASE=Path('E:/项目/Veyra');TASK='playback-smoothness-20261004'
OUT=BASE/'test-packages'/TASK
APP=OUT/'Veyra-2.0.3-smoothfix-NVIDIA-win64-portable'
manifest=json.loads((APP/'package-manifest.json').read_text(encoding='utf8'))
results=[]
for mode in ('gpu','software'):
    run=BASE/'tests'/TASK/('sealed-'+mode);run.mkdir(parents=True,exist_ok=False)
    tmp=BASE/'tmp'/TASK/('sealed-'+mode);tmp.mkdir(parents=True,exist_ok=False)
    logs=BASE/'logs'/TASK/('sealed-'+mode);logs.mkdir(parents=True,exist_ok=False)
    (run/'preferences.json').write_text(json.dumps({'language':'zh-CN','overlayCompat':'off'}),encoding='utf8')
    env=os.environ.copy()
    for name in list(env):
        if name.startswith(('VEYRA_','QML_','QT_','QSG_')):env.pop(name)
    env.update(TEMP=str(tmp),TMP=str(tmp),PATH=os.environ['WINDIR']+'/System32;'+os.environ['WINDIR'],
               VEYRA_LOG_FILE=str(logs/'player.log'))
    if mode=='software':env['QT_QUICK_BACKEND']='software'
    with (logs/'console.log').open('x',encoding='utf8') as log:
        code=subprocess.run([str(APP/'veyra_qml_ui.exe'),'--data-dir',str(run),'--page','pro',
                             '--exit-after','12000',str(BASE/'tests'/TASK/'fixtures/golden.mkv')],
                            cwd=APP,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=35).returncode
    text=(logs/'player.log').read_text(encoding='utf8',errors='replace')
    assert code==0 and '2.0.3-smoothfix' in text,(mode,code)
    assert 'the Sun God, Garokk.' in text and 'Sauron.' in text,('actual embedded subtitle',mode)
    assert not any(x in text for x in ('[ERROR]','[FATAL]','ReferenceError:','TypeError:')),mode
    for row in manifest['files']:
        f=APP/row['path']
        with f.open('rb') as stream:sha=hashlib.file_digest(stream,'sha256').hexdigest()
        assert f.stat().st_size==row['size'] and sha==row['sha256'],('Payload changed',row['path'])
    names={f.relative_to(APP).as_posix() for f in APP.rglob('*') if f.is_file()}
    assert names=={r['path'] for r in manifest['files']}|{'package-manifest.json'},('Unlisted files',names)
    results.append(dict(mode=mode,exitCode=code,passed=True,logs=str(logs),verifiedPayloadFiles=len(manifest['files'])))
    print('SEALED PACKAGE SMOKE PASS',mode,len(manifest['files']),flush=True)
(OUT/'package-smoke.json').write_text(json.dumps(results,indent=2),encoding='utf8')
