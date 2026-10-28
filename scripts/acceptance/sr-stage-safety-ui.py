"""One bounded file-only regression using a copy of the reported settings."""
import hashlib,json,os,shutil,subprocess
from pathlib import Path
B=Path('E:/项目/Veyra');R=Path(__file__).resolve().parents[2];T='sr-stage-freeze-20261028'
A=B/'tests/claude-sr-stage-20261028/app';P=B/'tests'/T/'profile';L=B/'logs'/T;TMP=B/'tmp'/T;OUT=B/'reports'/T
for p in (P,L,TMP,OUT):p.mkdir(parents=True,exist_ok=True)
user=A/'runtime_local/user-data-2.0.0'
before={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in user.glob('chain-session*')}
for name in before:shutil.copyfile(user/name,P/name)
(P/'qml-preferences.v1.json').write_text(json.dumps({'language':'zh-CN','autoResume':False,'prewarmEnhancement':False}),encoding='utf-8')
media=B/'tests/nr-manual-scale-20261024/media/nr-4k30.mp4'
src=(R/'qml/Veyra/Main.qml').read_bytes();text=src.decode('utf-8');idx=text.rfind('}')
loader='\nLoader {source: '+json.dumps((R/'scripts/acceptance/sr-stage-safety-ui.qml').as_uri())+'; onLoaded: item.media='+json.dumps(media.as_posix())+'}\n'
def replace(path,data):
    staged=path.with_name(path.name+'.safety-new');staged.write_bytes(data);os.replace(staged,path)
replace(A/'qml/Veyra/Main.qml',(text[:idx]+loader+text[idx:]).encode('utf-8'))
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
env.update(TEMP=str(TMP),TMP=str(TMP),APPDATA=str(P/'AppData'),LOCALAPPDATA=str(P/'LocalAppData'),CUDA_CACHE_PATH=str(TMP/'cuda'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_LOG_FILE=str(L/'engine.log'))
try:
    with (L/'console.log').open('wb') as f:rc=subprocess.run([str(A/'veyra_qml_ui.exe'),'--page','pro','--data-dir',str(P),'--exit-after','60000'],cwd=A,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=75).returncode
finally:replace(A/'qml/Veyra/Main.qml',src)
console=(L/'console.log').read_text(encoding='utf-8',errors='replace');engine=(L/'engine.log').read_text(encoding='utf-8',errors='replace')
errors=[l for l in console.splitlines() if any(x in l for x in ('ReferenceError:','TypeError:','Binding loop detected','is not a type','SyntaxError:'))]
unchanged=all(hashlib.sha256((user/n).read_bytes()).hexdigest()==h for n,h in before.items())
result={'exit':rc,'passed':rc==0 and 'SR_SAFETY_UI_PASS' in console and 'SR_SAFETY_UI_FAIL' not in console and not errors and unchanged,'qmlErrors':errors,'userSettingsUnchanged':unchanged,'unsafeWorkExtentObserved':'base=7680x4320' in engine,'steps':[l for l in engine.splitlines() if '[sr-safety-test]' in l or '[sr-stage-safety]' in l],'log':str(L/'engine.log')}
result['passed']=result['passed'] and not result['unsafeWorkExtentObserved']
(OUT/'safe-ui.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(result,ensure_ascii=False,indent=2))
raise SystemExit(0 if result['passed'] else 1)
