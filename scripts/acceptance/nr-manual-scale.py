"""Private real-NVIDIA NR scale checks, using released immutable runtimes.

Outputs stay on E:. User App package/logs/configuration are never modified.
The same private staging copy is reused for both runs; all replacements unlink
first to preserve the hardlinked donor. Tests do not connect capture/PS5 hosts.
"""
from pathlib import Path
import json,os,hashlib,shutil,subprocess,time,argparse
parser=argparse.ArgumentParser();parser.add_argument('--four-k',action='store_true');parser.add_argument('--pixels',action='store_true');args=parser.parse_args()
B=Path('E:/项目/Veyra');T='nr-manual-scale-20261024';ROOT=Path(__file__).resolve().parents[2]
BUILD=B/'build/claude-screen-recording-20261022';APP=B/'tests'/T/'app';PROFILE=B/'tests'/T/('player-profile-'+time.strftime('%H%M%S'));MEDIA=B/'tests'/T/'media';TMP=B/'tmp'/T/'player';REPORT=B/'reports'/T
for p in [PROFILE,MEDIA,TMP,REPORT]:p.mkdir(parents=True,exist_ok=True)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def replace(p,data):
    p.parent.mkdir(parents=True,exist_ok=True)
    if p.exists():p.unlink()
    p.write_bytes(data)
if not APP.exists():
    donor=B/'test-packages/codex-picture-rotation-20261023/Veyra-2.1.0-rot1-NVIDIA-win64-portable'
    approved=json.loads((donor/'package-manifest.json').read_text('utf8'))
    # Some older donor manifests include bootstrap state and logs. They are
    # mutable outputs, never test inputs or candidates for a shared hardlink.
    for rel in [e['path'] for e in approved['files']]+['package-manifest.json']:
        if rel.startswith('logs/') or any(part.startswith('user-data-') for part in Path(rel).parts):continue
        p=donor/rel;dst=APP/rel;dst.parent.mkdir(parents=True,exist_ok=True);os.link(p,dst)
    (REPORT/'player-donor.json').write_text(json.dumps(dict(donor=str(donor),manifestSha256=sha(donor/'package-manifest.json')),ensure_ascii=False,indent=1),'utf8')
replace(APP/'veyra_qml_ui.exe',(BUILD/'veyra_qml_ui.exe').read_bytes())
for folder in ['qml','i18n']:
    for p in (ROOT/folder).rglob('*'):
        if p.is_file():replace(APP/folder/p.relative_to(ROOT/folder),p.read_bytes())
donor_manifest=json.loads((APP/'package-manifest.json').read_text('utf8'))
for e in donor_manifest['files']:
    if e['path'].lower().endswith(('.dll','.onnx','.tflite','.dxil','.hsaco')):assert sha(APP/e['path'])==e['sha256']
video=MEDIA/('nr-4k30.mp4' if args.four_k else 'nr-1080p30.mp4')
if not video.exists():
    ff=next((Path(os.environ['LOCALAPPDATA'])/'Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe').glob('*/bin/ffmpeg.exe'))
    size='3840x2160' if args.four_k else '1920x1080'
    subprocess.run([str(ff),'-v','error','-f','lavfi','-i','testsrc2=size='+size+':rate=30','-t','90','-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p','-y',str(video)],env={**os.environ,'TEMP':str(TMP),'TMP':str(TMP)},check=True,timeout=120)
results=[]
for restore in ([False] if args.pixels else [False,True]):
    running=subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,errors='replace');assert not any(l.lower().startswith('"veyra') for l in running.splitlines()),'another player is running'
    label=('pixels' if args.pixels else 'restore' if restore else 'gpu')+('-4k' if args.four_k else '')+'-'+time.strftime('%H%M%S');log=B/'logs'/T/label;log.mkdir(parents=True)
    pixels=REPORT/('pixels-'+label);pixels.mkdir(exist_ok=True)
    (PROFILE/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN',autoResume=False,prewarmEnhancement=False,screenshotDir=pixels.as_posix())),'utf8')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))};env.update(TEMP=str(TMP),TMP=str(TMP),APPDATA=str(PROFILE/'AppData'),LOCALAPPDATA=str(PROFILE/'LocalAppData'),CUDA_CACHE_PATH=str(TMP/'cuda'),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1',VEYRA_LOG_FILE=str(log/'engine.log'))
    cfg=json.dumps(dict(restore=restore,captureOnly=args.pixels,media=str(video),screenshotDir=pixels.as_posix(),screenshot=str(REPORT/('nr-manual-card-4k.png' if args.four_k else 'nr-manual-control.png'))))
    main=(ROOT/'qml/Veyra/Main.qml').read_text('utf8');pos=main.rfind('}')
    loader='\nLoader { source: '+json.dumps(Path(__file__).with_suffix('.qml').as_uri())+'; onLoaded: item.config = '+json.dumps(cfg)+' }\n'
    replace(APP/'qml/Veyra/Main.qml',(main[:pos]+loader+main[pos:]).encode('utf8'))
    try:
        with (log/'console.log').open('wb') as f:run=subprocess.run([str(APP/'veyra_qml_ui.exe'),'--page','pro','--size','1280x800','--data-dir',str(PROFILE),'--exit-after','180000'],cwd=APP,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=210)
    finally:replace(APP/'qml/Veyra/Main.qml',(ROOT/'qml/Veyra/Main.qml').read_bytes())
    text=(log/'console.log').read_text('utf8',errors='replace');errors=[l for l in text.splitlines() if any(s in l for s in ['NR_MANUAL_FAIL','[ERROR]','ReferenceError:','TypeError:','Binding loop detected','Unable to assign','failed to load component']) and 'Retrying to obtain clipboard' not in l]
    expected='PIXELS_PASS' if args.pixels else 'RESTORE_PASS' if restore else 'GPU_AND_UI_PASS'
    assert run.returncode==0 and expected in text and not errors,(run.returncode,expected,errors,text[-2000:])
    result=dict(case=label,exit=run.returncode,passed=True,log=str(log),profile=str(PROFILE),exeSha256=sha(APP/'veyra_qml_ui.exe'))
    results.append(result);print(json.dumps(result,ensure_ascii=False),flush=True)
if args.pixels:
    from PIL import Image,ImageStat
    pngs=list(pixels.glob('*.png'));assert len(pngs)==1,pngs
    with Image.open(pngs[0]) as im:
        rgb=im.convert('RGB');stats=ImageStat.Stat(rgb)
        assert im.size==((3840,2160) if args.four_k else (1920,1080)) and min(stats.stddev)>10,(im.size,stats.stddev)
        results.append(dict(processedScreenshot=str(pngs[0]),size=im.size,mean=stats.mean,stddev=stats.stddev,sha256=sha(pngs[0]),passed=True))
(REPORT/('pixel-validation.json' if args.pixels else 'real-player-4k.json' if args.four_k else 'real-player.json')).write_text(json.dumps(results,ensure_ascii=False,indent=1),'utf8')
