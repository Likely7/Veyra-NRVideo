"""Rotation tests in a private native player, with unchanged released runtimes.

Run --prepare once, --contracts for GPU pixel checks, or --case wide|metadata|portrait.
All subprocesses use private profiles on E:, and each run is bounded to 240 s.
"""
from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, time

B=Path('E:/项目/Veyra'); T='codex-picture-rotation-20261023'
ROOT=Path(__file__).resolve().parents[2]; BUILD=B/'build/claude-screen-recording-20261022'
DONOR=B/'releases/release-2.1.0-20261019/runtime-base/NVIDIA'
APP=B/'tests'/T/'app'; MEDIA=B/'tests'/T/'media'
FF=next((Path(os.environ['LOCALAPPDATA'])/'Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe').glob('*/bin'))

def replace(dst,src):
    if dst.exists(): dst.unlink()
    dst.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(src,dst)

def prepare():
    if not APP.exists():
        for src in DONOR.rglob('*'):
            dst=APP/src.relative_to(DONOR)
            if src.is_dir(): dst.mkdir(parents=True,exist_ok=True)
            else: os.link(src,dst)
    for name in ['veyra_qml_ui','veyra_preview_geometry_tests']:
        replace(APP/(name+'.exe'),BUILD/(name+'.exe'))
    for src in (ROOT/'qml').rglob('*'):
        if src.is_file(): replace(APP/'qml'/src.relative_to(ROOT/'qml'),src)

def env_for(profile,tmp,log):
    for d in [profile,tmp,log]:d.mkdir(parents=True,exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
    env.update(TEMP=str(tmp),TMP=str(tmp),APPDATA=str(profile/'AppData'),LOCALAPPDATA=str(profile/'LocalAppData'),
               CUDA_CACHE_PATH=str(tmp/'cuda'),QML_DISABLE_DISK_CACHE='1',QT_FORCE_STDERR_LOGGING='1',VEYRA_LOG_FILE=str(log/'engine.log'))
    return env

def media(case):
    MEDIA.mkdir(parents=True,exist_ok=True)
    wide=MEDIA/'wide.mp4'; portrait=MEDIA/'portrait.mp4'; rotated=MEDIA/'metadata90-matrix.mp4'
    base=portrait if case=='portrait' else wide
    if not base.exists():
        size='360x640' if case=='portrait' else '640x360'
        subprocess.run([str(FF/'ffmpeg.exe'),'-v','error','-f','lavfi','-i','testsrc2=size='+size+':rate=30','-t','100','-c:v','libx264','-preset','ultrafast','-pix_fmt','yuv420p','-y',str(base)],check=True,timeout=120)
    if case=='metadata':
        if not rotated.exists():subprocess.run([str(FF/'ffmpeg.exe'),'-v','error','-display_rotation:v','90','-i',str(wide),'-c','copy','-y',str(rotated)],check=True,timeout=60)
        # FFmpeg's display matrix sign and the app's clockwise convention are
        # different. Derive the fixture metadata from the actual probe result.
        probe=json.loads(subprocess.check_output([str(FF/'ffprobe.exe'),'-v','error','-show_streams','-of','json',str(rotated)]))
        angle=next(s['rotation'] for s in probe['streams'][0]['side_data_list'] if 'rotation' in s)
        return rotated,9/16,(-int(angle))%360
    return base,(9/16 if case=='portrait' else 16/9),0

def main():
    p=argparse.ArgumentParser();p.add_argument('--prepare',action='store_true');p.add_argument('--contracts',action='store_true');p.add_argument('--case',choices=['wide','metadata','portrait','record']);p.add_argument('--software',action='store_true');p.add_argument('--software-decode',action='store_true');p.add_argument('--amd',action='store_true');p.add_argument('--scale',type=float,default=1);p.add_argument('--language',default='zh-CN');a=p.parse_args()
    if a.prepare:prepare()
    if not a.contracts and not a.case:return
    case='gpu-contracts' if a.contracts else a.case+('-software' if a.software else '')+('-amd' if a.amd else '')+('-swdecode' if a.software_decode else '')+'-'+a.language
    stamp=time.strftime('%H%M%S');log=B/'logs'/T/(case+'-'+stamp)
    profile=B/'tests'/T/'profiles'/(case+'-'+stamp);tmp=B/'tmp'/T/(case+'-'+stamp)
    env=env_for(profile,tmp,log)
    env['QT_SCALE_FACTOR']=str(a.scale)
    if a.contracts:cmd=[str(APP/'veyra_preview_geometry_tests.exe'),str(log/'pixels')]
    else:
        m,aspect,metadata=media(a.case)
        prefs=dict(language=a.language,autoResume=False,prewarmEnhancement=False)
        if a.amd:prefs.update(monitorGpu='1002:13c0:88771043',computeOnMonitorGpu=True)
        if a.software_decode:prefs['decode']='software'
        (profile/'qml-preferences.v1.json').write_text(json.dumps(prefs),'utf8')
        qml=(ROOT/'qml/Veyra/Main.qml').read_text('utf8');pos=qml.rfind('}')
        cfg=dict(media=str(m),aspect=aspect,metadata=metadata)
        if a.case=='record':
            rec=B/'tests'/T/'recordings'/stamp;rec.mkdir(parents=True);cfg['recordDir']=str(rec)
        cfg=json.dumps(cfg)
        loader='\nLoader { source: '+json.dumps((Path(__file__).with_suffix('.qml')).as_uri())+'; onLoaded: item.config = '+json.dumps(cfg)+' }\n'
        dst=APP/'qml/Veyra/Main.qml'
        if dst.exists():dst.unlink()
        dst.write_text(qml[:pos]+loader+qml[pos:],'utf8')
        cmd=[str(APP/'veyra_qml_ui.exe'),'--page','pro','--size','1280x800','--data-dir',str(profile),'--exit-after','200000']
        if a.software:cmd.append('--obs-game-capture')
        cmd.append(str(m))
    # Never run concurrent GPU apps or stop processes belonging to other tasks.
    tasks=subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,errors='replace')
    assert not any(line.lower().startswith('"veyra') for line in tasks.splitlines()),'another Veyra process is running'
    with (log/'console.log').open('wb') as f:
        result=subprocess.run(cmd,cwd=APP,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=240)
    console=(log/'console.log').read_text('utf8',errors='replace')
    engine=(log/'engine.log').read_text('utf8',errors='replace') if (log/'engine.log').exists() else ''
    errors=[l for l in engine.splitlines() if '[ERROR]' in l and 'Retrying to obtain clipboard' not in l]
    errors += [l for l in console.splitlines() if any(s in l for s in ['ReferenceError:', 'TypeError:', 'Binding loop detected', 'Unable to assign'])]
    with (APP/'veyra_qml_ui.exe').open('rb') as binary: exe_sha=hashlib.file_digest(binary,'sha256').hexdigest()
    report=dict(case=case,exit=result.returncode,exe_sha256=exe_sha,
                lines=[l for l in console.splitlines() if 'ROTATION' in l or 'PREVIEW_GEOMETRY' in l],errors=errors)
    report['passed']=result.returncode==0 and not errors and ('ROTATION PASS' in console if a.case else 'pass=0' not in console)
    if a.software:report['passed']=report['passed'] and 'software UI:' in engine
    if a.amd:report['passed']=report['passed'] and 'AMD Radeon(TM) Graphics' in engine
    if a.case=='record':
        files=[]
        for path in rec.glob('*.mp4'):
            data=json.loads(subprocess.check_output([str(FF/'ffprobe.exe'),'-v','error','-count_frames','-show_streams','-of','json',str(path)],timeout=300))
            video=next(s for s in data['streams'] if s['codec_type']=='video');files.append(dict(path=str(path),codec=video['codec_name'],width=video['width'],height=video['height'],frames=int(video.get('nb_read_frames',0))))
        report['files']=files
        report['passed']=report['passed'] and bool(files) and all(f['frames']>500 and f['width']==640 and f['height']==360 for f in files)
    (log/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=1),'utf8')
    print(json.dumps(report,ensure_ascii=False,indent=1)[-3500:]);print('LOG',log,flush=True)
    assert report['passed'],log

if __name__=='__main__':main()
