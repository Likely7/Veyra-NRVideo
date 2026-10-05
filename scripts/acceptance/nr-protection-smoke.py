"""Verify inherited libass remains active in the actual local executable."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
BASE=Path('E:/项目/Veyra');TASK='nr-strength-protection-20261005'
label=sys.argv[1];assert label.replace('-','').isalnum()
APP=Path(sys.argv[2]) if len(sys.argv)>2 else BASE/'test-packages'/TASK/'Veyra-2.0.3-nr-controls-NVIDIA-win64-portable'
assert APP.resolve().is_relative_to(BASE.resolve())
OUT=BASE/'tests'/TASK/('smoke-'+label);LOG=BASE/'logs'/TASK/('smoke-'+label);TMP=BASE/'tmp'/TASK/('smoke-'+label)
for p in (OUT,LOG,TMP):p.mkdir(parents=True,exist_ok=False)
media=BASE/'tests/field-fixes-20261005/media/fx-embedded.mkv';assert media.is_file()
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_QUICK_BACKEND','QSG_RHI_BACKEND'))}
env.update(TEMP=str(TMP),TMP=str(TMP),VEYRA_LOG_FILE=str(LOG/'player.log'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
args=[str(APP/'veyra_qml_ui.exe'),'--data-dir',str(OUT/'profile'),'--page','pro','--size','1280x800','--obs-game-capture','--exit-after','8000',str(media)]
with (LOG/'console.log').open('xb') as log:
    rc=subprocess.run(args,cwd=APP,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60).returncode
text=(LOG/'player.log').read_text(encoding='utf8',errors='replace')
bad=[l for l in text.splitlines() if any(x in l for x in ('[ERROR]','[FATAL]','ReferenceError:','TypeError:','no root object'))]
ass=[l for l in text.splitlines() if '[libass]' in l]
assert rc==0 and not bad and any('track ready events=5' in l for l in ass) and any('fonts configured container=1' in l for l in ass),text[-4000:]
result={'exit':rc,'passed':True,'exeSha256':hashlib.sha256((APP/'veyra_qml_ui.exe').read_bytes()).hexdigest(),'libass':ass[:8]}
(LOG/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('INHERITED LIBASS PASS',LOG)
