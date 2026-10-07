"""Real QML bridge and isolated portable package; no fake GPU vendor."""
from pathlib import Path
import os,sys,json,subprocess,shutil,time,hashlib
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='stability-export-priority-20261006'
label,vendor,case=sys.argv[1:4];assert label.replace('-','').isalnum() and vendor in ('AMD','NVIDIA')
out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/stability-export-priority-control.py')],check=True)
out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
source=BASE/'releases/publish-2.0.4-20261006/packages'/f'Veyra-2.0.4-{vendor}-win64-portable' if case in ('before','migration-before') else BASE/'test-packages'/TASK/f'Veyra-2.0.4-fix2-{vendor}-win64-portable'
def copy(a,b):
    p=Path(a)
    if p.suffix.lower() in ('.dll','.bin','.hsaco','.onnx','.f16','.f32'):
        try:os.link(a,b);return b
        except OSError:pass
    return shutil.copy2(a,b)
app=out/'app';shutil.copytree(source,app,copy_function=copy,ignore=shutil.ignore_patterns('*.log','logs','user-data-*','*.dmp','*.pdb'))
shutil.copy2(ROOT/'scripts/acceptance/stability-export-priority-ui.qml',app/'qml/Veyra/RepairRegression.qml')
conf=dict(case=case,catalogOnly=case in ('before','catalog'),before=case in ('before','migration-before'),restore=case=='restore',media=(BASE/'tests/perf-matrix/media/M2.mkv').as_posix(),exportMedia=(BASE/'tests/release-2.0.4-20261005/nr-fixture.mp4').as_posix())
main=app/'qml/Veyra/Main.qml';text=main.read_text(encoding='utf8');at=text.rfind('}')
main.write_text(text[:at]+'\nLoader{source:"RepairRegression.qml";onLoaded:item.config='+json.dumps(conf)+'}\n'+text[at:],encoding='utf8')
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logs/(label+'-engine.log')),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
(out/'outputs').mkdir()
profile=BASE/'tests'/TASK/'gui-fsr-export-second/profile' if case=='restore' else out/'profile'
if case=='telemetry':
    profile.mkdir();(profile/'qml-preferences.v1.json').write_text(json.dumps({'gpuPriority':'normal','overlayCompat':'off'}),encoding='utf8')
if case in ('migration-before','migration','nvidia-keep'):
    shutil.copytree(BASE/'tests'/TASK/'gui-amd-default-seed/profile',profile)
elif case=='xess-keep':
    shutil.copytree(BASE/'tests'/TASK/'gui-amd-default-migrate/profile',profile)
args=[str(app/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','pro','--size','1280x800','--exit-after','250000','--export-out',str(out/'outputs')]
console=logs/(label+'.log');begin=time.monotonic()
with console.open('xb') as stream:
    try:rc=subprocess.run(args,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=270).returncode
    except subprocess.TimeoutExpired:rc=124
body=console.read_text(encoding='utf8',errors='replace')
passed=rc==0 and ('REPAIR_UI_EXPECTED_OLD' if conf['before'] else 'REPAIR_UI_PASS') in body and not any(s in body for s in ('REPAIR_UI_FAIL','ReferenceError:','TypeError:'))
receipt=dict(passed=passed,exit=rc,seconds=time.monotonic()-begin,case=case,vendorPackage=vendor,actualGpu='RTX5070; missing-NVIDIA-runtime gate reproduction is not AMD hardware validation',exeSHA=hashlib.sha256((app/'veyra_qml_ui.exe').read_bytes()).hexdigest(),config=conf)
if case=='export':
    files=list((out/'outputs').glob('*.mp4'));receipt['outputs']=[]
    for f in files:
        probe=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-count_frames','-show_streams','-of','json',str(f)],timeout=30));v=next(s for s in probe['streams'] if s['codec_type']=='video')
        receipt['outputs'].append(dict(path=str(f),frames=int(v['nb_read_frames']),width=v['width'],height=v['height'],fps=v['r_frame_rate']))
    passed=passed and len(files)==1 and receipt['outputs'][0]['frames']==120;receipt['passed']=passed
(logs/(label+'.json')).write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(receipt,ensure_ascii=False));print('\n'.join(body.splitlines()[-12:]));raise SystemExit(0 if passed else 1)
