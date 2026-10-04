"""Current QML/current product: switches, bounded pressure, key and close eviction."""
from pathlib import Path
import importlib.util
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle()
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe');shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
for source,target in [('nr-core-ui.qml','CoreUiProbe.qml'),('nr-cache-ui.qml','CacheUiProbe.qml')]:shutil.copy2(ROOT/'scripts/perf'/source,app/'qml/Veyra'/target)
main=app/'qml/Veyra/Main.qml';original=main.read_text(encoding='utf-8');at=original.rfind('}');results=[]
runs=[('nr',disabled,repeat) for repeat in range(1,4) for disabled in (True,False)]+[('pressure',False,1),('invalidate',False,1)]
for group,disabled,repeat in runs:
    matrix.assert_gpu_tests_idle();name=f'{label}-{group}-'+('off' if disabled else 'on')+f'-r{repeat}'
    out=BASE/'logs'/TASK/name;profile=BASE/'tests'/TASK/name/'profile';tmp=BASE/'tmp'/TASK/name
    for p in (out,profile,tmp):p.mkdir(parents=True,exist_ok=False)
    (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off'}),encoding='utf-8')
    (app/'cache-run.json').write_text(json.dumps({'media':str(matrix.SOURCES['M1']).replace('\\','/'),'group':group}),encoding='utf-8')
    loader='CoreUiProbe.qml' if group=='nr' else 'CacheUiProbe.qml'
    main.write_text(original[:at]+'\n Loader {source:"'+loader+'";onLoaded:{const r=new XMLHttpRequest();r.open("GET","'+(app/'cache-run.json').as_uri()+'",false);r.send();const c=JSON.parse(r.responseText);item.media=c.media;item.group=c.group}}\n'+original[at:],encoding='utf-8')
    env=os.environ.copy()
    for key in tuple(env):
        if key.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND')):env.pop(key)
    env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'player.log'),QML_DISABLE_DISK_CACHE='1',
        QT_FORCE_STDERR_LOGGING='1',QML_XHR_ALLOW_FILE_READ='1',VEYRA_VERBOSE_FRAME_LOGS='1')
    if disabled:env['VEYRA_TEST_DISABLE_RECENT_GRAPH_CACHE']='1'
    if group=='pressure':env.update(VEYRA_TEST_VRAM_LEAK_MIB='256',VEYRA_TEST_VRAM_LEAK_LIMIT_MIB='3072')
    command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','120000']
    print('START',name,flush=True);before=matrix.gpu_query();started=time.monotonic()
    with (out/'console.log').open('xb') as log:
        try:rc=subprocess.run(command,cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=130).returncode
        except subprocess.TimeoutExpired:rc=124
    text=(out/'player.log').read_text(encoding='utf-8',errors='replace')
    settled=[json.loads(line.split('CORE_UI_SETTLED ',1)[1]) for line in text.splitlines() if 'CORE_UI_SETTLED ' in line]
    resets=[{k:float(v) for k,v in re.findall(r'(\w+)=(-?\d+(?:\.\d+)?)',line)} for line in text.splitlines() if '[reset-lifecycle]' in line and 'reason=9' in line]
    cache=[line for line in text.splitlines() if '[feature-cache]' in line];hits=sum('event=hit' in line for line in cache)
    warm_on=[resets[i]['createMs'] for i,request in enumerate(settled) if request['nr'] and i<len(resets)]
    passed=rc==0 and not any(s in text for s in ('CORE_UI_FAIL','CACHE_UI_FAIL','[ERROR]','[FATAL]','leaked parameter block','ReferenceError:','TypeError:'))
    if group=='nr':passed=passed and len(settled)==20 and 'CORE_UI_PASS' in text and hits==(0 if disabled else 10)
    else:passed=passed and 'CACHE_UI_PASS' in text and hits==0
    if group=='pressure':passed=passed and any('event=evict reason=budget-pressure-or-device' in s for s in cache) and 'heldMiB=3072 limitMiB=3072 hr=0x0' in text
    if group=='invalidate':passed=passed and any('event=evict reason=key-changed' in s for s in cache) and any('event=evict reason=session-close' in s for s in cache)
    result={'group':group,'disabled':disabled,'repeat':repeat,'command':command,'exeSha256':matrix.digest(app/'veyra_qml_ui.exe'),
        'exitCode':rc,'wallSeconds':time.monotonic()-started,'gpuBefore':before,'gpuAfter':matrix.gpu_query(),
        'settledRequests':settled,'resetEvents':resets,'cacheEvents':cache,'hits':hits,
        'warmOnCreateMedianMs':statistics.median(warm_on) if warm_on else None,'passed':bool(passed),
        'measurement':'Actual product graph/presenter lifecycle CPU; public bridge observation includes polling; no scanout'}
    (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
    print('RESULT',name,result['passed'],hits,result['warmOnCreateMedianMs'],flush=True)
    if not result['passed']:print(text[-5000:],flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
