"""Stage and exercise shared QML controls through real Qt mouse events."""
from pathlib import Path
import json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='nr-strength-protection-20261005'
label=sys.argv[1];assert label.replace('-','').isalnum()
OUT=BASE/'tests'/TASK/('qml-'+label);TMP=BASE/'tmp'/TASK/('qml-'+label)
LOG=BASE/'logs'/TASK/('qml-'+label)
for p in (OUT,TMP,LOG):p.mkdir(parents=True,exist_ok=False)
QT=Path('C:/veyra-deps/qt-veyra/6.8.3/msvc2022_64')
APP=BASE/'test-packages'/TASK/'Veyra-2.0.3-nr-controls-NVIDIA-win64-portable'
shutil.copy2(BASE/'build'/TASK/'veyra_qml_quick_tests.exe',OUT/'veyra_qml_quick_tests.exe')
for p in APP.glob('Qt6*.dll'):shutil.copy2(p,OUT/p.name)
for name in ('Qt6Test.dll','Qt6QuickTest.dll'):shutil.copy2(QT/'bin'/name,OUT/name)
shutil.copytree(APP/'qml',OUT/'qml')
shutil.copytree(ROOT/'qml',OUT/'qml',dirs_exist_ok=True)
shutil.copytree(QT/'qml/QtTest',OUT/'qml/QtTest')
shutil.copytree(ROOT/'tests/qml/quick',OUT/'qml-tests')
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP),QML_DISABLE_DISK_CACHE='1',
    QT_FORCE_STDERR_LOGGING='1',QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',
    QT_QUICK_CONTROLS_STYLE='Basic',QT_QPA_PLATFORM_PLUGIN_PATH=str(QT/'plugins/platforms'),
    VEYRA_EXPORT_TEST_ARTIFACTS=str(OUT/'artifacts'),PATH=str(QT/'bin')+os.pathsep+env.get('PATH',''))
(OUT/'artifacts').mkdir()
with (LOG/'quick.log').open('xb') as log:
    try:rc=subprocess.run([str(OUT/'veyra_qml_quick_tests.exe'),'-input',str(OUT/'qml-tests')],cwd=OUT,env=env,
        stdout=log,stderr=subprocess.STDOUT,timeout=290).returncode
    except subprocess.TimeoutExpired:rc=124
(LOG/'result.json').write_text(json.dumps({'exit':rc,'staged':str(OUT),'log':str(LOG/'quick.log')},indent=2),encoding='utf8')
print((LOG/'quick.log').read_text(encoding='utf8',errors='replace')[-3500:])
raise SystemExit(rc)
