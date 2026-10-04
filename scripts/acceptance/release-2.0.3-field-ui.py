"""Actual Qt bridge, NR/SR, NVENC and software background regression, <300 sec."""
import json, os, shutil, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BASE=Path('E:/项目/Veyra');TASK='release-2.0.3-20261004'
APP=BASE/'tests'/TASK/'app-NVIDIA-shared-fsr';BUILD=BASE/'build'/TASK
out=BASE/'tests'/TASK/('production-'+sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
tmp=BASE/'tmp'/TASK/('production-'+sys.argv[1]);tmp.mkdir(parents=True,exist_ok=False)
logs=BASE/'logs'/TASK/('production-'+sys.argv[1]);logs.mkdir(parents=True,exist_ok=False)
media=out/'2k30.avi'
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOG_FILE=str(logs/'veyra-qml.log'),
    QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
for key in ('QT_QPA_PLATFORM','QT_QUICK_BACKEND','VEYRA_UI_RHI','VEYRA_TEST_IGNORE_RTSS'):
    env.pop(key,None)
with (logs/'fixture.log').open('xb') as log:
    subprocess.run(['ffmpeg','-v','warning','-n','-f','lavfi','-i','testsrc2=size=2560x1440:rate=30',
        '-t','0.8','-c:v','mpeg4','-q:v','3','-threads','2',str(media)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=60,check=True)
for name in ('veyra_qml_ui.exe',):shutil.copy2(BUILD/name,APP/name)
shutil.copytree(ROOT/'qml',APP/'qml',dirs_exist_ok=True)
(out/'outputs').mkdir()
(logs/'veyra-last-failure.txt').write_text('device\nnvppex.dll\n',encoding='ascii')
main=APP/'qml/Veyra/Main.qml';original=main.read_bytes();source=original.decode('utf8')
expect_rtss='--rtss' in sys.argv
loader='\nLoader { source: '+json.dumps((ROOT/'scripts/acceptance/field-upgrade-ui.qml').as_uri())+'; onLoaded: { item.media='+json.dumps(str(media))+'; item.evidence='+json.dumps(str(out))+'; item.expectRtss='+str(expect_rtss).lower()+' } }\n'
args=['.\\veyra_qml_ui.exe','--page','exp','--size','1280x720','--reduced-motion','--export-out',str(out/'outputs'),
    '--data-dir',str(out/'profile'),'--exit-after','250000']
if not expect_rtss:args.append('--obs-game-capture')
try:
    at=source.rfind('}');main.write_text(source[:at]+loader+source[at:],encoding='utf8')
    with (logs/'console.log').open('xb') as log:
        code=subprocess.run(args,executable=str(APP/'veyra_qml_ui.exe'),cwd=APP,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=270).returncode
finally:main.write_bytes(original)
console=(logs/'console.log').read_text(encoding='utf8',errors='replace')
assert code==0 and 'FIELD_UI_PASS' in console and 'FIELD_UI_FAIL' not in console,console[-5000:]
parent=(logs/'veyra-qml.log').read_text(encoding='utf8',errors='replace')
assert 'export frozen bitrateMbps=18 rateControl=1' in parent and 'export frozen bitrateMbps=24 rateControl=0' in parent
outputs=sorted((out/'outputs').glob('*.mp4'));assert len(outputs)==2,outputs
probes=[]
for file in outputs:
    data=json.loads(subprocess.check_output(['ffprobe','-v','error','-count_frames','-show_streams','-show_format','-of','json',str(file)],env=env,timeout=30))
    stream=next(x for x in data['streams'] if x['codec_type']=='video')
    assert stream['codec_name']=='hevc' and int(stream['width'])==3840 and int(stream['height'])==2160 and int(stream['nb_read_frames'])==24
    probes.append({'output':str(file),'probe':data})
(logs/'outputs.json').write_text(json.dumps(probes,ensure_ascii=False,indent=2),encoding='utf8')
from PIL import Image
im=Image.open(out/'software-background.png').convert('RGBA')
samples=[im.getpixel((12,80)),im.getpixel((640,25)),im.getpixel((1260,710))]
assert all(p[3]==255 and max(p[:3])<130 for p in samples),samples
print('FIELD PRODUCTION PASS',out,'background_pixels',samples,'outputs',len(outputs))
