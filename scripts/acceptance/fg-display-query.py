"""Private real-VFG playback/async-display regression; no capture or real profile."""
from pathlib import Path
import os,json,hashlib,shutil,subprocess,time
B=Path('E:/项目/Veyra');T='fg-scheduling-repair-20261024';R=Path(__file__).resolve().parents[2]
D=B/'build/claude-screen-recording-20261022';A=B/'tests'/T/'app';P=B/'tests'/T/'profile';L=B/'logs'/T/'native';TMP=B/'tmp'/T/'native';OUT=B/'reports'/T
for p in [P,L,TMP,OUT]:p.mkdir(parents=True,exist_ok=True)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def replace(p,data):
    p.parent.mkdir(parents=True,exist_ok=True)
    if p.exists():p.unlink()
    p.write_bytes(data)
donor=B/'test-packages/codex-picture-rotation-20261023/Veyra-2.1.0-rot1-NVIDIA-win64-portable'
manifest=json.loads((donor/'package-manifest.json').read_text('utf8'))
if not A.exists():
    for e in manifest['files']:
        rel=e['path']
        if rel.startswith('logs/') or any(x.startswith('user-data-') for x in Path(rel).parts):continue
        p=A/rel;p.parent.mkdir(parents=True,exist_ok=True);os.link(donor/rel,p)
    shutil.copyfile(donor/'package-manifest.json',A/'package-manifest.json')
replace(A/'veyra_qml_ui.exe',(D/'veyra_qml_ui.exe').read_bytes())
for folder in ['qml','i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file():replace(A/folder/p.relative_to(R/folder),p.read_bytes())
for e in manifest['files']:
    if Path(e['path']).suffix.lower() in ['.dll','.onnx','.tflite','.hsaco','.dxil']:assert sha(A/e['path'])==e['sha256']
media=B/'tests/nr-manual-scale-20261024/media/nr-4k30.mp4';assert media.is_file()
(P/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN',autoResume=False,prewarmEnhancement=False)),'utf8')
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
env.update(TEMP=str(TMP),TMP=str(TMP),APPDATA=str(P/'AppData'),LOCALAPPDATA=str(P/'LocalAppData'),CUDA_CACHE_PATH=str(TMP/'cuda'),VEYRA_LOG_FILE=str(L/'engine.log'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
main=(R/'qml/Veyra/Main.qml').read_text('utf8');pos=main.rfind('}')
loader='\nLoader { source: '+json.dumps(Path(__file__).with_suffix('.qml').as_uri())+'; onLoaded: item.media='+json.dumps(media.as_posix())+' }\n'
replace(A/'qml/Veyra/Main.qml',(main[:pos]+loader+main[pos:]).encode('utf8'))
running=subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,errors='replace')
assert not any(l.lower().startswith('"veyra') for l in running.splitlines()),'another player is running; do not compete'
try:
    with (L/'console.log').open('wb') as f:p=subprocess.run([str(A/'veyra_qml_ui.exe'),'--page','pro','--size','1280x800','--data-dir',str(P),'--exit-after','80000'],cwd=A,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=100)
finally:replace(A/'qml/Veyra/Main.qml',(R/'qml/Veyra/Main.qml').read_bytes())
text=(L/'console.log').read_text('utf8',errors='replace');log=(L/'engine.log').read_text('utf8',errors='replace')
errors=[x for x in text.splitlines() if any(s in x for s in ['FG_DISPLAY_FAIL','[ERROR]','ReferenceError:','TypeError:','Binding loop detected'])]
queries=[x for x in log.splitlines() if '[display-query]' in x]
passed=p.returncode==0 and 'FG_DISPLAY_PASS' in text and not errors and len(queries)>=3
result=dict(exit=p.returncode,passed=passed,log=str(L),exeSha256=sha(A/'veyra_qml_ui.exe'),displayQueries=queries,errors=errors,scope='actual 4K30 file VFG2X/4X, FollowDisplay/VRR, pause/resume, close/reopen; no capture device or photon measurement')
(OUT/'native.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
assert passed,text[-5000:]
