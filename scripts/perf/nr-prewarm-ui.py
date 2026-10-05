"""Matched cold/open UI tests. Latency is first successful software Present."""
from pathlib import Path
from datetime import datetime
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];groups=sys.argv[3:] or ['nr','srnr','sr'];matrix.assert_gpu_tests_idle()
assert all(g in ('nr','srnr','sr','during','quit','disable','mismatch','alloff') for g in groups)
baseline=variant=='A';disabled=variant.endswith('-off');build_variant=variant.removesuffix('-off')
app=BASE/'tests'/TASK/(label+'-app');assert not app.exists()
shutil.copytree(BASE/'test-packages'/TASK/'Veyra-2.0.3-perf-baseline-A-NVIDIA-win64-portable',app,copy_function=matrix.copy_dependency)
if not baseline:
    shutil.copy2(BASE/'build'/TASK/build_variant/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
    shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
shutil.copy2(ROOT/'scripts/perf/nr-prewarm-ui.qml',app/'qml/Veyra/PrewarmUiProbe.qml')
main=app/'qml/Veyra/Main.qml';text=main.read_text(encoding='utf-8');at=text.rfind('}')
main.write_text(text[:at]+'\n Loader {source:"PrewarmUiProbe.qml";onLoaded:{const r=new XMLHttpRequest();r.open("GET","'+(app/'prewarm-run.json').as_uri()+'",false);r.send();const c=JSON.parse(r.responseText);item.media=c.media;item.group=c.group;item.referenceBuild=c.baseline}}\n'+text[at:],encoding='utf-8')
results=[]
for group in groups:
    for repeat in range(1,4) if group in ('nr','srnr','sr') else (1,):
        matrix.assert_gpu_tests_idle();name=f'{label}-{group}-r{repeat}'
        out=BASE/'logs'/TASK/name;profile=BASE/'tests'/TASK/name/'profile';tmp=BASE/'tmp'/TASK/name
        for p in (out,profile,tmp):p.mkdir(parents=True,exist_ok=False)
        (profile/'qml-preferences.v1.json').write_text(json.dumps({'overlayCompat':'off','prewarmEnhancement':not disabled}),encoding='utf-8')
        (app/'prewarm-run.json').write_text(json.dumps({'media':str(matrix.SOURCES['M1']).replace('\\','/'),'group':group,'baseline':baseline}),encoding='utf-8')
        env=os.environ.copy()
        for key in tuple(env):
            if key.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND')):env.pop(key)
        env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(out/'player.log'),QML_DISABLE_DISK_CACHE='1',
            QT_FORCE_STDERR_LOGGING='1',QML_XHR_ALLOW_FILE_READ='1',VEYRA_VERBOSE_FRAME_LOGS='1')
        command=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','95000']
        print('START',name,flush=True);before=matrix.gpu_query();start=time.monotonic()
        with (out/'console.log').open('xb') as f:
            try:rc=subprocess.run(command,cwd=app,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=105).returncode
            except subprocess.TimeoutExpired:rc=124
        text=(out/'player.log').read_text(encoding='utf-8',errors='replace')
        marks=[int(m) for m in re.findall(r'PREWARM_UI_OPEN (\d+)',text)]
        submits=[line for line in text.splitlines() if '[submit]' in line]
        latency=None
        if marks and submits:latency=datetime.fromisoformat(submits[0].split()[0].replace('Z','+00:00')).timestamp()*1000-marks[0]
        events=[line for line in text.splitlines() if '[prewarm]' in line]
        ready=sum('event=ready' in line for line in events);adopted=sum('event=adopt' in line for line in events)
        expect_no_open=group in ('quit','alloff')
        passed=rc==0 and not any(x in text for x in ('PREWARM_UI_FAIL','ReferenceError:','TypeError:','[ERROR]','[FATAL]','leaked parameter block'))
        if expect_no_open:passed=passed and not submits and ('PREWARM_UI_QUIT_DURING' in text if group=='quit' else 'PREWARM_UI_ALL_OFF_PASS' in text)
        else:passed=passed and 'PREWARM_UI_PASS' in text and latency is not None and latency>0
        if not baseline and not disabled and group in ('nr','srnr','sr','during'):passed=passed and ready==1 and adopted==1
        if baseline or disabled or group=='alloff':passed=passed and ready==0 and adopted==0
        if group=='disable':passed=passed and ready==1 and adopted==0 and any('event=discard' in s or 'event=disabled' in s for s in events)
        if group=='mismatch':passed=passed and ready==1 and adopted==0 and any('event=miss' in s for s in events)
        result={'variant':variant,'group':group,'repeat':repeat,'command':command,'exitCode':rc,
            'exeSha256':matrix.digest(app/'veyra_qml_ui.exe'),'runtimeSha256':matrix.digest(app/'runtime/experimental/nvngx_dlssnr.dll'),
            'sourceSha256':matrix.digest(matrix.SOURCES['M1']),'wallSeconds':time.monotonic()-start,'gpuBefore':before,'gpuAfter':matrix.gpu_query(),
            'openToFirstSuccessfulPresentMs':latency,'ready':ready,'adopted':adopted,'events':events,
            'coreInitCalls':text.count('core-host: Init_with_ProjectID result='),'coreShutdownCalls':text.count('core-host: Shutdown1 result='),
            'passed':bool(passed),'measurement':'Public open request wall time to first successful Present log; includes queue/device/decode; no scanout'}
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');results.append(result)
        print('RESULT',name,result['passed'],latency,ready,adopted,result['coreInitCalls'],flush=True)
        if not result['passed']:print('\n'.join([s for s in text.splitlines() if '[prewarm]' in s or 'PREWARM_UI_' in s or '[ERROR]' in s])[-4000:],flush=True);raise SystemExit(1)
with (BASE/'logs'/TASK/(label+'-summary.json')).open('x',encoding='utf-8') as f:json.dump(results,f,ensure_ascii=False,indent=2)
