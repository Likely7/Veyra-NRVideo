"""Bounded local integration checks; no real user state or proprietary changes."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,time
B=Path('E:/项目/Veyra');T='capture-audio-main-20261025';R=Path(__file__).resolve().parents[2]
D=B/'build/claude-screen-recording-20261022';A=B/'tests'/T/'app';O=B/'reports'/T;L=B/'logs'/T;TMP=B/'tmp'/T
for p in [O,L,TMP]:p.mkdir(parents=True,exist_ok=True)
arg=argparse.ArgumentParser();arg.add_argument('stage',choices=['prepare','contracts','quick','gpu','launch']);arg.add_argument('--mode',default='software');a=arg.parse_args()
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def replace(dst,src):
    dst.parent.mkdir(parents=True,exist_ok=True)
    if dst.exists():dst.unlink() # detach mutable code from immutable donor hardlinks
    shutil.copyfile(src,dst)
def env_for(name):
    p=B/'tests'/T/('profile-'+name);tmp=TMP/name
    p.mkdir(parents=True,exist_ok=True);tmp.mkdir(parents=True,exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
    env.update(TEMP=str(tmp),TMP=str(tmp),APPDATA=str(p/'AppData'),LOCALAPPDATA=str(p/'LocalAppData'),CUDA_CACHE_PATH=str(TMP/'cuda'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1')
    env['PATH']=str(B/'deps/qt/6.8.3/msvc2022_64/bin')+';'+str(B/'deps/ffmpeg-filter-issues41-45-20261016/bin')+';'+env['PATH']
    return env,p
def run(name,cmd,cwd=D,env=None,timeout=280):
    f=L/(name+'.log');start=time.monotonic()
    with f.open('wb') as stream:
        c=subprocess.run(list(map(str,cmd)),cwd=cwd,env=env or env_for(name)[0],stdout=stream,stderr=subprocess.STDOUT,timeout=timeout)
    row=dict(name=name,exit=c.returncode,seconds=round(time.monotonic()-start,3),log=str(f));print(json.dumps(row),flush=True);return row
if a.stage=='prepare':
    donor=B/'releases/release-2.1.0-20261019/runtime-base/NVIDIA';dm=json.loads((donor/'package-manifest.json').read_text('utf8'))
    if not A.exists():
        A.mkdir(parents=True)
        for e in dm['files']:
            rel=e['path'];p=A/rel
            if rel.startswith('logs/') or any(x.startswith('user-data-') for x in Path(rel).parts):continue
            p.parent.mkdir(parents=True,exist_ok=True);os.link(donor/rel,p)
        shutil.copyfile(donor/'package-manifest.json',A/'package-manifest.json')
    for name in ['veyra_qml_ui','veyra_nr_runtime_switch_tests','veyra_capture_tests','veyra_nr_export_size_tests']:replace(A/(name+'.exe'),D/(name+'.exe'))
    for folder in ['qml','i18n']:
        for p in (R/folder).rglob('*'):
            if p.is_file():replace(A/folder/p.relative_to(R/folder),p)
    runtime=[e for e in dm['files'] if Path(e['path']).suffix.lower() in ['.dll','.onnx','.tflite','.hsaco','.dxil']]
    assert all(sha(A/e['path'])==e['sha256'] for e in runtime)
    (O/'prepared.json').write_text(json.dumps(dict(app=str(A),exeSha256=sha(A/'veyra_qml_ui.exe'),runtimeFiles=len(runtime)),ensure_ascii=False,indent=1),'utf8')
    print('Prepared private app; unchanged runtime files:',len(runtime))
elif a.stage in ['contracts','quick']:
    for p in (R/'tests/qml/quick').glob('tst_*.qml'):replace(D/'qml-tests'/p.name,p)
    results=[]
    targets=['capture_color','nvapi_lifetime','repair_contract','ui_contract','effect_chain','async_display_query','nr_model_settings','capture_audio','qml_quick']
    if a.stage=='quick':
        targets=['qml_quick'];results=[x for x in json.loads((O/'contracts.json').read_text('utf8')) if x['name']!='qml_quick']
        if not (O/'contracts-initial.json').exists():
            shutil.copyfile(O/'contracts.json',O/'contracts-initial.json');shutil.copyfile(L/'qml_quick.log',L/'qml_quick-initial.log')
    for name in targets:
        env,p=env_for('contracts-'+name);cmd=[D/('veyra_'+name+'_tests.exe')]
        if name=='nr_model_settings':cmd.append(B/'tests'/T/'model-policy')
        if name=='qml_quick':env.update(QT_QPA_PLATFORM='windows',QT_QUICK_BACKEND='software')
        row=run(name,cmd,env=env);results.append(row)
        (O/'contracts.json').write_text(json.dumps(results,ensure_ascii=False,indent=1),'utf8')
    assert all(x['exit']==0 for x in results)
elif a.stage=='gpu':
    assert not any(line.lower().startswith('"veyra') for line in subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,errors='replace').splitlines())
    media=B/'tests/nr-manual-scale-20261024/media/nr-1080p30.mp4'
    row=run('nr-runtime-switch', [A/'veyra_nr_runtime_switch_tests.exe',media,B/'tests'/T/'nr-switch'],cwd=A,timeout=270)
    (O/'nr-switch.json').write_text(json.dumps(row,indent=1),'utf8');assert row['exit']==0
elif a.stage=='launch':
    env,p=env_for('native-'+a.mode);assert not (p/'qml-preferences.v1.json').exists(),'use a fresh test mode name'
    (p/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN',autoResume=False,prewarmEnhancement=False)),'utf8')
    env['VEYRA_LOG_FILE']=str(L/('native-'+a.mode+'-engine.log'))
    if a.mode.startswith('software'):env['QT_QUICK_BACKEND']='software'
    else:env['VEYRA_UI_RHI']='d3d11'
    cmd=[str(A/'veyra_qml_ui.exe'),'--data-dir',str(p),'--page','pro','--size','1280x800','--exit-after','260000',str(B/'tests/nr-manual-scale-20261024/media/nr-1080p30.mp4')]
    if a.mode.startswith('software'):cmd.append('--obs-game-capture')
    with (L/('native-'+a.mode+'-console.log')).open('wb') as f:proc=subprocess.Popen(cmd,cwd=A,env=env,stdout=f,stderr=subprocess.STDOUT)
    (O/('native-'+a.mode+'.json')).write_text(json.dumps(dict(pid=proc.pid,command=cmd,exeSha256=sha(A/'veyra_qml_ui.exe')),ensure_ascii=False,indent=1),'utf8');print('Private UI pid',proc.pid)
