"""Real QML saved-preset -> frozen worker -> encoded video; private state only."""
from pathlib import Path
import json,os,subprocess,hashlib,shutil,re
B=Path('E:/项目/Veyra');T='export-nr-scale-20261025';R=Path(__file__).resolve().parents[2];D=B/'build/claude-screen-recording-20261022';A=B/'tests'/T/'app';P=B/'tests'/T/'ui-profile';L=B/'logs'/T/'ui';TMP=B/'tmp'/T/'ui';O=B/'reports'/T
assert A.exists() and not P.exists()
for p in [P,L,TMP]:p.mkdir(parents=True,exist_ok=True)
def replace(p,data):
    p.parent.mkdir(parents=True,exist_ok=True)
    if p.exists():p.unlink()
    p.write_bytes(data)
replace(A/'veyra_qml_ui.exe',(D/'veyra_qml_ui.exe').read_bytes())
for folder in ['qml','i18n']:
    for p in (R/folder).rglob('*'):
        if p.is_file():replace(A/folder/p.relative_to(R/folder),p.read_bytes())
(P/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN',autoResume=False,prewarmEnhancement=False)),'utf8')
media=B/'tests'/T/'media/3840x2160-6frames.mp4';out=B/'tests'/T/'ui-preset-37.mp4';assert media.exists() and not out.exists()
main=(R/'qml/Veyra/Main.qml').read_text('utf8');pos=main.rfind('}');loader='\nLoader { source: '+json.dumps(Path(__file__).with_suffix('.qml').as_uri())+'; onLoaded: { item.media='+json.dumps(media.as_posix())+'; item.output='+json.dumps(out.as_posix())+' } }\n'
replace(A/'qml/Veyra/Main.qml',(main[:pos]+loader+main[pos:]).encode('utf8'))
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))};env.update(TEMP=str(TMP),TMP=str(TMP),APPDATA=str(P/'AppData'),LOCALAPPDATA=str(P/'LocalAppData'),CUDA_CACHE_PATH=str(TMP/'cuda'),VEYRA_LOG_FILE=str(L/'engine.log'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
before=set((A/'logs').glob('export-worker-*.log'));running=subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,errors='replace');assert not any(l.lower().startswith('"veyra') for l in running.splitlines()),'another player is running'
try:
    with (L/'console.log').open('wb') as f:p=subprocess.run([str(A/'veyra_qml_ui.exe'),'--page','pro','--size','1280x800','--data-dir',str(P),'--export-out',str(out),'--exit-after','145000'],cwd=A,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=170)
finally:replace(A/'qml/Veyra/Main.qml',(R/'qml/Veyra/Main.qml').read_bytes())
text=(L/'console.log').read_text('utf8',errors='replace');workers=set((A/'logs').glob('export-worker-*.log'))-before;assert len(workers)==1,workers
worker=next(iter(workers));wt=worker.read_text('utf8',errors='replace');actual=re.findall(r'\[nr-model\].*internal=(\d+)x(\d+)',wt)
probe=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-count_frames','-show_streams','-of','json',str(out)],text=True));v=next(x for x in probe['streams'] if x['codec_type']=='video')
passed=p.returncode==0 and 'NR_EXPORT_PRESET_PASS' in text and 'NR_EXPORT_PRESET_FAIL' not in text and actual==[('1420','798')] and v['width']==3840 and v['height']==2160 and int(v['nb_read_frames'])==6 and '[ERROR]' not in wt
result=dict(exit=p.returncode,passed=passed,exeSha256=hashlib.sha256((A/'veyra_qml_ui.exe').read_bytes()).hexdigest(),workerLog=str(worker),nrCreated=actual,output=str(out),outputSize=[v['width'],v['height']],decodedFrames=int(v['nb_read_frames']),log=str(L))
(O/'ui-preset-export.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True);assert passed,text[-3500:]
