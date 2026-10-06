"""Bounded real graph / actual worker regressions, never AMD inference claims."""
from pathlib import Path
import os,json,sys,subprocess,shutil,hashlib,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='obs-export-amdnr-20261006'
label,case=sys.argv[1:3];out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK
out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
app=BASE/'releases/publish-2.0.4-20261006/packages/Veyra-2.0.4-NVIDIA-win64-portable';build=BASE/'build'/TASK
for p in app.glob('*.dll'):shutil.copyfile(p,out/p.name)
shutil.copytree(build/'shaders',out/'shaders')
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOCAL_DATA_DIR=str(out/'profile'),VEYRA_LOG_FILE=str(logs/(label+'-engine.log')))
results=[]
if case.startswith('graph'):
    targets=['veyra_amd_nr_graph_tests'];args=[]
    d=out/'runtime/amd-nr';d.mkdir(parents=True);(d/'assets').mkdir()
    shutil.copyfile(build/'lmxxf-test-runtime/LmxxfNrRuntime.dll',d/'LmxxfNrRuntime.dll')
    (out/'runtime/nvidia').mkdir()
elif case=='units':targets=['veyra_effect_chain_tests','veyra_preset_library_tests','veyra_ui_i18n_tests'];args=[]
elif case=='abi':
    targets=['veyra_lmxxf_nr_tests'];args=[];shutil.copytree(build/'lmxxf-test-runtime',out/'lmxxf-test-runtime')
elif case=='mapping-reject':targets=['veyra_qml_ui'];args=['--export-worker','1']
else:
    targets=['veyra_export_workflow_tests'];args=[case,str(BASE/'tests/release-2.0.4-20261005/nr-fixture.mp4'),str(out/'result.mp4')]
    shutil.copytree(app/'runtime',out/'runtime');shutil.copytree(app/'runtime_local',out/'runtime_local')
for t in targets:
    exe=out/(t+'.exe');shutil.copyfile(build/exe.name,exe)
    log=logs/(label+'-'+t+'.log');begin=time.monotonic()
    with log.open('xb') as console:
        try:rc=subprocess.run([str(exe),*args],cwd=out,env=env,stdout=console,stderr=subprocess.STDOUT,timeout=290).returncode
        except subprocess.TimeoutExpired:rc=124
    text=log.read_text(encoding='utf8',errors='replace')
    good=rc==0
    if case=='graph-before':good=rc==1 and 'layers=0 temporal=0' in text and text.count('pass=0')==6
    if case=='mapping-reject':
        workers=list((out/'logs').glob('export-worker-*.log'))
        good=rc==1 and len(workers)==1 and 'mapping failed win32=' in workers[0].read_text(encoding='utf8',errors='replace')
    results.append(dict(target=t,exit=rc,expectedPass=good,seconds=time.monotonic()-begin,exeSHA=hashlib.sha256(exe.read_bytes()).hexdigest(),log=str(log)))
    print(t,rc,'expected',good);print('\n'.join(text.splitlines()[-9:]))
if (out/'result.mp4').exists():
    probe=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-count_frames','-show_streams','-of','json',str(out/'result.mp4')],timeout=30))
    (out/'ffprobe.json').write_text(json.dumps(probe,indent=2),encoding='utf8')
    video=next(s for s in probe['streams'] if s['codec_type']=='video')
    assert int(video['nb_read_frames'])==({'lifecycle':20,'lifecycle-boundary':2}.get(case,60))
(logs/(label+'.json')).write_text(json.dumps(results,indent=2),encoding='utf8')
raise SystemExit(0 if all(x['expectedPass'] for x in results) else 1)
