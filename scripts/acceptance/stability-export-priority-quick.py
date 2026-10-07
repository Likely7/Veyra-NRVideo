"""Bounded software-rendered tests of the actual staged telemetry component."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='stability-export-priority-20261006'
label=sys.argv[1];assert label.replace('-','').isalnum()
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/stability-export-priority-control.py')],check=True)
app=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK
app.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
shutil.copy2(BASE/'build'/TASK/'standard/veyra_qml_quick_tests.exe',app/'veyra_qml_quick_tests.exe')
shutil.copytree(ROOT/'qml',app/'qml');shutil.copytree(ROOT/'tests/qml/quick',app/'qml-tests')
qt=BASE/'deps/qt/6.8.3/msvc2022_64'
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),PATH=str(qt/'bin')+';'+env['PATH'],
           QML_IMPORT_PATH=str(qt/'qml'),QT_PLUGIN_PATH=str(qt/'plugins'),
           QT_QUICK_BACKEND='software',QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
args=[str(app/'veyra_qml_quick_tests.exe'),'-platform','offscreen','-input',str(app/'qml-tests/tst_telemetry.qml')]
begin=time.monotonic();log=logs/(label+'.log')
with log.open('xb') as stream:
    try:rc=subprocess.run(args,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=120).returncode
    except subprocess.TimeoutExpired:rc=124
body=log.read_text(encoding='utf8',errors='replace')
passed=rc==0 and '0 failed' in body and 'Telemetry::test_frequent_samples' in body
receipt=dict(passed=passed,exit=rc,seconds=time.monotonic()-begin,args=args,softwareOnly=True,
             componentSha256=hashlib.sha256((app/'qml/Veyra/VTelemetryValue.qml').read_bytes()).hexdigest(),log=str(log))
(logs/(label+'.json')).write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(receipt,ensure_ascii=False));print('\n'.join(body.splitlines()[-22:]));raise SystemExit(0 if passed else 1)
