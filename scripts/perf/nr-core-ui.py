"""20 real UI requests per switch family, isolated profiles and raw evidence."""
from pathlib import Path
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py')
matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];groups=sys.argv[3:] or ['nr','sr','layers','sizes','fg','runtime']
assert all(x.replace('-','').isalnum() for x in (variant,label))
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant.removesuffix('-off')/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
shutil.copy2(ROOT/'scripts/perf/nr-core-ui.qml',app/'qml/Veyra/NrCoreUiProbe.qml')
main=app/'qml/Veyra/Main.qml';text=main.read_text(encoding='utf-8');at=text.rfind('}')
loader='\n Loader {source:"NrCoreUiProbe.qml"; onLoaded:{const r=new XMLHttpRequest();r.open("GET","'+(app/'core-run.json').as_uri()+'",false);r.send();const c=JSON.parse(r.responseText);item.media=c.media;item.group=c.group}}\n'
main.write_text(text[:at]+loader+text[at:],encoding='utf-8')
results=[]
for group in groups:
    assert group in ('nr','sr','layers','sizes','fg','runtime')
    name=label+'-'+group;out=BASE/'logs'/TASK/name;profile=BASE/'tests'/TASK/name/'profile';tmp=BASE/'tmp'/TASK/name
    for p in (out,profile,tmp):p.mkdir(parents=True,exist_ok=False)
    (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off'}),encoding='utf-8')
    (app/'core-run.json').write_text(json.dumps({'media':str(matrix.SOURCES['M1']).replace('\\','/'),'group':group}),encoding='utf-8')
    env=os.environ.copy()
    for key in tuple(env):
        if key.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND')):env.pop(key)
    env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'player.log'),QML_DISABLE_DISK_CACHE='1',
               QT_FORCE_STDERR_LOGGING='1',QML_XHR_ALLOW_FILE_READ='1',VEYRA_VERBOSE_FRAME_LOGS='1')
    if variant.endswith('-off'):env['VEYRA_TEST_DISABLE_NGX_CORE_REUSE']='1'
    command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','270000']
    print('START',name,flush=True);before=matrix.gpu_query();start=time.monotonic()
    with (out/'console.log').open('xb') as log:
        try:rc=subprocess.run(command,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=280).returncode
        except subprocess.TimeoutExpired:rc=124
    text=(out/'player.log').read_text(encoding='utf-8',errors='replace')
    settled=[json.loads(line.split('CORE_UI_SETTLED ',1)[1]) for line in text.splitlines() if 'CORE_UI_SETTLED ' in line]
    resets=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',line)} for line in text.splitlines() if '[reset-lifecycle]' in line]
    result={'variant':variant,'group':group,'exeSha256':matrix.digest(app/'veyra_qml_ui.exe'),
            'testQmlSha256':matrix.digest(app/'qml/Veyra/NrCoreUiProbe.qml'),
            'nrSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),'command':command,'exitCode':rc,
            'wallSeconds':time.monotonic()-start,'gpuBefore':before,'gpuAfter':matrix.gpu_query(),
            'settledRequests':settled,'actualResetEvents':resets,'coreInitCalls':text.count('core-host: Init_with_ProjectID result='),
            'coreShutdownCalls':text.count('core-host: Shutdown1 result='),'cacheHits':text.count('[ngx-core-cache] event=hit'),
            'passed':rc==0 and len(settled)==20 and 'CORE_UI_PASS' in text and not any(s in text for s in ('CORE_UI_FAIL','[ERROR]','[FATAL]','leaked parameter block','ReferenceError:','TypeError:')),
            'measurement':'Real UI requests and CPU-observed reset lifecycle; bridge settling includes snapshot polling, not scanout'}
    results.append(result);(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('RESULT',name,{k:result[k] for k in ('exitCode','wallSeconds','coreInitCalls','cacheHits','passed')},flush=True)
    if not result['passed']:
        print('\n'.join(text.splitlines()[-30:]),flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
