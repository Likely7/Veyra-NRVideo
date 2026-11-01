"""One fixed captured frame, offline GPU diagnostics, never opens a capture device."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,time,sys
import numpy as np
from PIL import Image
B=Path('E:/项目/Veyra');T='codex-capture-review-handoff-20261026';R=Path(__file__).resolve().parents[2]
A=B/'tests'/T/'app';O=B/'reports'/T;L=B/'logs'/T;TMP=B/'tmp'/T;D=B/'build/claude-screen-recording-20261022'
arg=argparse.ArgumentParser();arg.add_argument('stage',choices=['prepare','aa','analyze','contracts','ui']);arg.add_argument('--case');arg.add_argument('--run',default='final');arg.add_argument('--extra-python-packages');args=arg.parse_args()
if args.extra_python_packages:sys.path.append(args.extra_python_packages)
for p in [O,L,TMP]:p.mkdir(parents=True,exist_ok=True)
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def replace(dst,src):
    dst.parent.mkdir(parents=True,exist_ok=True)
    if dst.exists():dst.unlink()
    shutil.copyfile(src,dst)
def env_for(name):
    tmp=TMP/name;tmp.mkdir(parents=True,exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
    env.update(TEMP=str(tmp),TMP=str(tmp),APPDATA=str(tmp/'AppData'),LOCALAPPDATA=str(tmp/'LocalAppData'),CUDA_CACHE_PATH=str(TMP/'cuda'),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
    env['PATH']=str(B/'deps/qt/6.8.3/msvc2022_64/bin')+';'+str(B/'deps/ffmpeg-filter-issues41-45-20261016/bin')+';'+env['PATH']
    return env
if args.stage=='prepare':
    donor=B/'releases/release-2.1.0-20261019/runtime-base/NVIDIA';m=json.loads((donor/'package-manifest.json').read_text('utf8'))
    if not A.exists():
        for e in m['files']:
            rel=e['path']
            if rel.startswith('logs/') or any(x.startswith('user-data-') for x in Path(rel).parts):continue
            p=A/rel;p.parent.mkdir(parents=True,exist_ok=True);os.link(donor/rel,p)
    for name in ['veyra_qml_ui','veyra_anti_alias_gpu_tests']:replace(A/(name+'.exe'),D/(name+'.exe'))
    for folder in ['qml','i18n']:
        for p in (R/folder).rglob('*'):
            if p.is_file():replace(A/folder/p.relative_to(R/folder),p)
    runtime=[e for e in m['files'] if Path(e['path']).suffix.lower() in ['.dll','.onnx','.tflite','.hsaco','.dxil']]
    assert all(sha(A/e['path'])==e['sha256'] for e in runtime)
    # The old "AA-before" live shot had an empty graph: it is not ordering
    # evidence, but is useful here as a fixed, unenhanced captured picture.
    source=B/'tmp/claude-capture-review-20261026/shots-raw/veyra-20261026-052849-047.f32'
    rgba=np.fromfile(source,np.float32).reshape(1440,2560,4);rgb=rgba[...,:3]
    assert np.isfinite(rgb).all()
    linear=np.maximum(rgb,0)*(80/203);linear=linear/(1+linear)
    coded=np.where(linear<=.0031308,12.92*linear,1.055*np.power(linear,1/2.4)-.055)
    ppm=B/'tests'/T/'fixed-captured-frame.ppm';Image.fromarray(np.rint(np.clip(coded,0,1)*255).astype(np.uint8)).save(ppm)
    receipt=dict(runtimeFilesVerified=len(runtime),frame=str(ppm),source=str(source),sourceSha256=sha(source),frameSha256=sha(ppm),width=2560,height=1440,conversion='scRGB 80nit units -> reference 203nit -> Reinhard -> sRGB8; SDR diagnostic, not original HDR processing')
    (O/'prepared.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=1),'utf8');print(json.dumps(receipt,ensure_ascii=False))
elif args.stage=='aa':
    assert not any(x.lower().startswith('"veyra') for x in subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,errors='replace').splitlines()),'user player is running; do not compete'
    result=json.loads((O/'aa-gpu.json').read_text('utf8')) if args.case and (O/'aa-gpu.json').exists() else []
    if args.case and not (O/'aa-gpu-initial.json').exists():shutil.copyfile(O/'aa-gpu.json',O/'aa-gpu-initial.json')
    result=[x for x in result if x['case']!='hdr-smaa-high' and x['case']!=args.case]
    for case in ([args.case] if args.case else ['real-frame','sdr-smaa-high','hdr-smaa']):
        env=env_for(case);env.update(VEYRA_AA_FRAME=str(B/'tests'/T/'fixed-captured-frame.ppm'),VEYRA_AA_FRAME_OUT=str(B/'tests'/T/'aa-fixed-frame'))
        log=L/(case+'.log');start=time.monotonic()
        with log.open('wb') as f:rc=subprocess.run([str(A/'veyra_anti_alias_gpu_tests.exe'),'--case',case],cwd=A,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=280).returncode
        row=dict(case=case,exit=rc,seconds=round(time.monotonic()-start,3),log=str(log));result.append(row);print(json.dumps(row),flush=True)
        (O/'aa-gpu.json').write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8')
        if rc:raise SystemExit(rc)
elif args.stage=='contracts':
    for p in (R/'tests/qml/quick').glob('tst_*.qml'):replace(D/'qml-tests'/p.name,p)
    names=args.case.split(',') if args.case else ['effect_chain','async_display_query','ui_contract','ui_i18n','repair_contract','qml_quick']
    results=[x for x in json.loads((O/'contracts.json').read_text('utf8')) if x['name'] not in names] if args.case else []
    if args.case and not (O/'contracts-pre-snapshot.json').exists():shutil.copyfile(O/'contracts.json',O/'contracts-pre-snapshot.json')
    for name in names:
        env=env_for('contracts-'+name)
        if name=='qml_quick':env.update(QT_QPA_PLATFORM='windows',QT_QUICK_BACKEND='software')
        log=L/(name+'.log');start=time.monotonic()
        with log.open('wb') as f:rc=subprocess.run([str(D/('veyra_'+name+'_tests.exe'))],cwd=D,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=280).returncode
        row=dict(name=name,exit=rc,seconds=round(time.monotonic()-start,3),log=str(log));results.append(row);print(json.dumps(row),flush=True)
        (O/'contracts.json').write_text(json.dumps(results,ensure_ascii=False,indent=1),'utf8')
    assert all(x['exit']==0 for x in results)
elif args.stage=='ui':
    assert not any(x.lower().startswith('"veyra') for x in subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,errors='replace').splitlines()),'user player is running'
    # Small tagged PQ clip, generated locally. No camera/capture/device access.
    hdr=B/'tests'/T/'hdr-pq-tagged.mp4';env=env_for('media')
    if not hdr.exists():
        with (L/'media.log').open('wb') as f:
            subprocess.run([shutil.which('ffmpeg'),'-v','error','-f','lavfi','-i','testsrc2=size=640x360:rate=30','-t','10','-vf','format=yuv420p10le','-c:v','libx265','-preset','ultrafast','-x265-params','pools=2:frame-threads=2:log-level=error:colorprim=9:transfer=16:colormatrix=9','-color_primaries','bt2020','-color_trc','smpte2084','-colorspace','bt2020nc',str(hdr)],env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=120)
    stream=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-select_streams','v:0','-show_streams','-of','json',str(hdr)],text=True))['streams'][0]
    assert stream.get('color_transfer')=='smpte2084' and stream.get('color_primaries')=='bt2020','the fixture must really carry PQ VUI metadata'
    if (O/'ui.json').exists() and not (O/'ui-initial.json').exists():shutil.copyfile(O/'ui.json',O/'ui-initial.json')
    if (O/'ui.json').exists():shutil.copyfile(O/'ui.json',O/('ui-before-'+args.run+'.json'))
    main=A/'qml/Veyra/Main.qml';original=main.read_bytes();results=[x for x in json.loads((O/'ui.json').read_text('utf8')) if x['case']!=args.case] if args.case else []
    source=original.decode('utf8');pos=source.rfind('}')
    loader='\nLoader { source: '+json.dumps(Path(__file__).with_name('capture-review-handoff-ui.qml').as_uri())+'; onLoaded: item.shell=root }\n'
    main.write_text(source[:pos]+loader+source[pos:],'utf8')
    try:
        for case,media in [('sdr',B/'tests/nr-manual-scale-20261024/media/nr-1080p30.mp4'),('hdr',hdr)]:
            if args.case and args.case!=case:continue
            profile=B/'tests'/T/('ui-'+case+'-'+args.run);assert not profile.exists();profile.mkdir()
            shots=profile/'screenshots'
            (profile/'qml-preferences.v1.json').write_text(json.dumps(dict(language='zh-CN',autoResume=False,prewarmEnhancement=False,screenshotDir=str(shots))),'utf8')
            env=env_for('ui-'+case+'-'+args.run);env.update(QT_QUICK_BACKEND='software',VEYRA_LOG_FILE=str(L/('ui-'+case+'-'+args.run+'-engine.log')))
            log=L/('ui-'+case+'-'+args.run+'.log');start=time.monotonic()
            with log.open('wb') as f:rc=subprocess.run([str(A/'veyra_qml_ui.exe'),'--data-dir',str(profile),'--page','min','--size','1280x800','--obs-game-capture','--exit-after','38000',str(media)],cwd=A,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=50).returncode
            text=log.read_text('utf8',errors='replace');m=__import__('re').search(r'REVIEW_UI_SCREENSHOT (\{.*\})',text)
            saved=json.loads(m[1]) if m else {};p=Path(saved['file']) if saved else None
            passed=rc==0 and 'REVIEW_UI_PASS' in text and 'REVIEW_UI_FAIL' not in text and 'Unable to set geometry' not in text and p and p.is_file() and p.stat().st_size>0
            if case=='sdr':passed=passed and p.suffix=='.png'
            if case=='hdr':passed=passed and saved and 'HDR' in saved.get('color','') and 'SDR' not in saved.get('color','') and p.suffix=='.jxr'
            row=dict(case=case,exit=rc,passed=bool(passed),seconds=round(time.monotonic()-start,3),log=str(log),screenshot=saved);results.append(row);print(json.dumps(row,ensure_ascii=False),flush=True)
            (O/'ui.json').write_text(json.dumps(results,ensure_ascii=False,indent=1),'utf8')
            if not passed:raise AssertionError(text[-3000:])
    finally:main.write_bytes(original)
elif args.stage=='analyze':
    folder=B/'tests'/T/'aa-fixed-frame';names=['plain','aa','nr','nr-again','aa-before-nr','aa-after-nr']
    frames={name:np.asarray(Image.open(folder/(name+'.ppm')).convert('RGB'),dtype=np.float32)/255 for name in names}
    luma=lambda v:v@np.array([.2126,.7152,.0722],np.float32)
    base=luma(frames['nr']);grad=np.hypot(np.diff(base,axis=1,prepend=base[:,:1]),np.diff(base,axis=0,prepend=base[:1]))
    edges=grad>.06;assert int(edges.sum())>1000
    metrics={}
    for name,reference in [('aa','plain'),('nr-again','nr'),('aa-before-nr','nr'),('aa-after-nr','nr')]:
        d=np.abs(frames[name]-frames[reference]);y=np.mean(d,axis=2)
        metrics[name]=dict(changedPixels=int(np.count_nonzero(np.max(d,axis=2)>1/255+.00001)),meanAbs=float(d.mean()),edgeMeanAbs=float(y[edges].mean()),flatMeanAbs=float(y[~edges].mean()))
    assert metrics['aa']['changedPixels']>500 and metrics['aa-after-nr']['changedPixels']>500
    assert metrics['aa-after-nr']['edgeMeanAbs']>metrics['nr-again']['edgeMeanAbs']*2
    (O/'fixed-frame-analysis.json').write_text(json.dumps(dict(size=[2560,1440],metrics=metrics,note='One identical SDR-tonemapped captured input. Image changes prove the effect executes; this alone is not a ground-truth quality score or live HDR comparison.'),indent=1),'utf8')
    # Scientific comparison figure: a fixed ROI and nearest-neighbour zoom.
    import matplotlib;matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    x,y,w,h=1152,800,256,192
    fig,axes=plt.subplots(1,4,figsize=(16,4.2),layout='constrained')
    for ax,name in zip(axes,['plain','nr','aa-before-nr','aa-after-nr']):
        ax.imshow(frames[name][y:y+h,x:x+w],interpolation='nearest');ax.set_title(name);ax.axis('off')
    fig.suptitle('Same captured input, SDR diagnostic; fixed ROI (1152, 800, 256, 192)')
    fig.savefig(O/'fixed-frame-comparison.png',dpi=150);plt.close(fig)
    print(json.dumps(metrics))
