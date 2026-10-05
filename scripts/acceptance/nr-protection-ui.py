"""Real Qt bridge, preview, PNG, preset/session and NVENC validation, <300s/run."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='nr-strength-protection-20261005'
APP=BASE/'test-packages'/TASK/'Veyra-2.0.3-nr-controls-NVIDIA-win64-portable'
mode=sys.argv[1];assert mode in ('first','restore','multi','visual')
label=sys.argv[2] if len(sys.argv)>2 else mode
assert label.replace('-','').isalnum()
OUT=BASE/'tests'/TASK/('production-'+label);LOG=BASE/'logs'/TASK/('production-'+label);TMP=BASE/'tmp'/TASK/('production-'+label)
for p in (OUT,LOG,TMP):p.mkdir(parents=True,exist_ok=False)
(OUT/'screenshots').mkdir();(OUT/'outputs').mkdir()
media=BASE/'tests'/TASK/'nr-fixture.avi'
if mode=='visual':
    media=Path(sys.argv[3]);assert media.is_file() and media.resolve().is_relative_to(BASE.resolve())
env=os.environ.copy();env.update(TEMP=str(TMP),TMP=str(TMP),VEYRA_LOG_FILE=str(LOG/'veyra.log'),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
for key in ('QT_QPA_PLATFORM','QT_QUICK_BACKEND','VEYRA_UI_RHI','VEYRA_TEST_IGNORE_RTSS'):env.pop(key,None)
if mode=='first' and not media.exists():
    with (LOG/'fixture.log').open('xb') as log:
        subprocess.run(['ffmpeg','-v','warning','-n','-f','lavfi','-i','testsrc2=size=1280x720:rate=30','-t','2','-c:v','mpeg4','-q:v','3','-threads','2',str(media)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60,check=True)
receipt=BASE/'logs'/TASK/'production-profile.json'
if mode=='first':
    profile=OUT/'profile'
    receipt.write_text(json.dumps({'profile':str(profile)}),encoding='utf8')
elif mode=='restore':profile=Path(json.loads(receipt.read_text(encoding='utf8'))['profile'])
else:profile=OUT/'profile'
main=APP/'qml/Veyra/Main.qml';original=main.read_bytes();source=original.decode('utf8')
loader='\nLoader { anchors.fill: parent; source: '+json.dumps((ROOT/'scripts/acceptance/nr-protection-ui.qml').as_uri())+'; onLoaded: { item.media='+json.dumps(str(media))+'; item.evidence='+json.dumps(str(OUT))+'; item.resume='+str(mode=='restore').lower()+'; item.multi='+str(mode=='multi').lower()+'; item.visual='+str(mode=='visual').lower()+' } }\n'
args=[str(APP/'veyra_qml_ui.exe'),'--page','pro','--size','1280x900','--reduced-motion','--obs-game-capture','--export-out',str(OUT/'outputs'),'--data-dir',str(profile),'--exit-after','250000']
try:
    at=source.rfind('}');main.write_text(source[:at]+loader+source[at:],encoding='utf8')
    with (LOG/'console.log').open('xb') as log:
        rc=subprocess.run(args,cwd=APP,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=270).returncode
finally:main.write_bytes(original)
assert hashlib.sha256(main.read_bytes()).digest()==hashlib.sha256(original).digest()
console=(LOG/'console.log').read_text(encoding='utf8',errors='replace')
expected={'first':'NR_CONTROLS_UI_PASS','restore':'NR_CONTROLS_RESTORE_PASS','multi':'NR_CONTROLS_MULTI_PASS','visual':'NR_CONTROLS_VISUAL_PASS'}[mode]
assert rc==0 and expected in console and 'NR_CONTROLS_UI_FAIL' not in console,console[-6000:]
if mode=='first':
    parent=(LOG/'veyra.log').read_text(encoding='utf8',errors='replace')
    assert 'mode=auto' in parent and 'mode=manual' in parent and 'gain=5' in parent
    assert len(list((OUT/'screenshots').glob('*.png')))>=4
    outputs=list((OUT/'outputs').glob('*.mp4'));assert len(outputs)==1,outputs
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_streams','-of','json',str(outputs[0])],env=env,timeout=30))
    stream=next(s for s in probe['streams'] if s['codec_type']=='video')
    assert stream['codec_name']=='hevc' and int(stream['nb_read_frames'])==60
    assert (int(stream['width']),int(stream['height']))==(3840,2160)
    (LOG/'export-probe.json').write_text(json.dumps(probe,indent=2),encoding='utf8')
elif mode=='multi':
    parent=(LOG/'veyra.log').read_text(encoding='utf8',errors='replace')
    assert 'build layer=1 gain=5 mode=auto' in parent and 'build layer=2 gain=5 mode=manual' in parent
    assert len(list((OUT/'screenshots').glob('*.png')))==1
print('NR production',mode,'PASS',OUT)
