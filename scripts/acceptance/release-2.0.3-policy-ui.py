"""GPU package availability and real FSR SR using current product, no fake GPU."""
import json, os, shutil, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]; BASE=Path('E:/项目/Veyra'); TASK='release-2.0.3-20261004'
flavor=sys.argv[1];label=sys.argv[2];assert flavor in ('NVIDIA','AMD') and label.replace('-','').isalnum()
app=BASE/'tests'/TASK/('app-'+flavor+('-shared-fsr' if flavor=='NVIDIA' else ''))
logs=BASE/'logs'/TASK/label;out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label
for p in (logs,out,tmp):p.mkdir(parents=True,exist_ok=False)
shutil.copyfile(BASE/'build'/TASK/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe')
shutil.copytree(ROOT/'qml/Veyra',app/'qml/Veyra',dirs_exist_ok=True)
profile=out/'profile';migrated=flavor=='AMD'
if migrated:shutil.copytree(BASE/'tests'/TASK/'vfg-ui-v1/profile',profile)
media=BASE/'tests/runtime-size-20261004/ui-v1/preview-720p30.mp4';assert media.is_file()
main=app/'qml/Veyra/Main.qml';original=main.read_bytes();source=original.decode('utf8');at=source.rfind('}')
loader='\nLoader { source: '+json.dumps((ROOT/'scripts/acceptance/release-2.0.3-policy-ui.qml').as_uri())+'; onLoaded: { item.flavor='+json.dumps(flavor)+'; item.media='+json.dumps(str(media))+'; item.evidence='+json.dumps(str(out))+'; item.migrated='+str(migrated).lower()+' } }\n'
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logs/'engine.log'),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
for k in ('QT_QPA_PLATFORM','QT_QUICK_BACKEND','VEYRA_UI_RHI','VEYRA_VFG_RUNTIME','QT_QPA_PLATFORM_PLUGIN_PATH'):env.pop(k,None)
env['PATH']=os.pathsep.join((env['WINDIR']+'/System32',env['WINDIR'],env['WINDIR']+'/System32/Wbem'))
try:
 main.write_text(source[:at]+loader+source[at:],encoding='utf8')
 with (logs/'console.log').open('xb') as log:
  code=subprocess.run(['.\\veyra_qml_ui.exe','--page','pro','--size','1280x900','--reduced-motion','--data-dir',str(profile),'--exit-after','130000'],executable=str(app/'veyra_qml_ui.exe'),cwd=app,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=145).returncode
finally:main.write_bytes(original)
console=(logs/'console.log').read_text(encoding='utf8',errors='replace');engine=(logs/'engine.log').read_text(encoding='utf8',errors='replace')
assert code==0 and 'POLICY_UI_PASS' in console and 'POLICY_UI_FAIL' not in console,console[-5000:]
assert '[fsr-sr] provider=3.1.' in engine and 'failures=0' in engine,engine[-4000:]
if migrated:assert '已关闭当前硬件或包不支持的效果' in engine
assert not any(s in console for s in ('ReferenceError:','TypeError:','is not a type'))
(logs/'summary.json').write_text(json.dumps(dict(flavor=flavor,exit=code,hardware='RTX5070 / 616.56',policy=True,fsrSrPlayback=True,amdInference=False,migrated=migrated),indent=2),encoding='utf8')
print('POLICY GUI PASS',flavor,logs,flush=True)
