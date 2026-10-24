"""Authorized physical G50 1440p60 NV12 A/B, private profiles, one GPU owner."""
from pathlib import Path
import os,json,hashlib,subprocess,time,argparse
parser=argparse.ArgumentParser();parser.add_argument('--case',required=True);parser.add_argument('--baseline',action='store_true');parser.add_argument('--half',action='store_true');parser.add_argument('--seconds',type=int,default=40);a=parser.parse_args()
B=Path('E:/项目/Veyra');T='fg-scheduling-repair-20261024';R=Path(__file__).resolve().parents[2]
A=B/'tests'/T/'app';P=B/'tests'/T/('capture-'+a.case);L=B/'logs'/T/('capture-'+a.case);TMP=B/'tmp'/T/('capture-'+a.case);OUT=B/'reports'/T
assert A.exists() and not P.exists(),P
for p in [P,L,TMP,OUT]:p.mkdir(parents=True,exist_ok=True)
def replace(p,data):
    if p.exists():p.unlink()
    p.write_bytes(data)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
exe=(B/'test-packages/nr-manual-scale-20261024/Veyra-2.1.0-nr1-NVIDIA-update/veyra_qml_ui.exe') if a.baseline else B/'build/claude-screen-recording-20261022/veyra_qml_ui.exe'
replace(A/'veyra_qml_ui.exe',exe.read_bytes())
(P/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN',autoResume=False,prewarmEnhancement=False,screenshotDir=P.as_posix())),'utf8')
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))};env.update(TEMP=str(TMP),TMP=str(TMP),APPDATA=str(P/'AppData'),LOCALAPPDATA=str(P/'LocalAppData'),CUDA_CACHE_PATH=str(TMP/'cuda'),VEYRA_LOG_FILE=str(L/'engine.log'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
main=(R/'qml/Veyra/Main.qml').read_text('utf8');pos=main.rfind('}');cfg=dict(name=a.case,half=a.half,seconds=a.seconds)
loader='\nLoader { source: '+json.dumps(Path(__file__).with_suffix('.qml').as_uri())+'; onLoaded: item.config='+json.dumps(json.dumps(cfg))+' }\n'
replace(A/'qml/Veyra/Main.qml',(main[:pos]+loader+main[pos:]).encode('utf8'))
running=subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,errors='replace');assert not any(l.lower().startswith('"veyra') for l in running.splitlines()),'another player is running; no device competition'
try:
    with (L/'console.log').open('wb') as f:p=subprocess.run([str(A/'veyra_qml_ui.exe'),'--page','pro','--size','1280x800','--data-dir',str(P),'--exit-after','130000'],cwd=A,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=150)
finally:replace(A/'qml/Veyra/Main.qml',(R/'qml/Veyra/Main.qml').read_bytes())
text=(L/'console.log').read_text('utf8',errors='replace');errors=[l for l in text.splitlines() if any(s in l for s in ['CAPTURE_FG_FAIL','[ERROR]','ReferenceError:','TypeError:','Binding loop detected'])]
passed=p.returncode==0 and 'CAPTURE_FG_PASS' in text and not errors
result=dict(case=a.case,baseline=a.baseline,half=a.half,seconds=a.seconds,exit=p.returncode,passed=passed,log=str(L),profile=str(P),exeSha256=sha(A/'veyra_qml_ui.exe'),errors=errors)
(OUT/('capture-'+a.case+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
assert passed,text[-6000:]
