"""Short real-worker NR export matrix. Immutable runtime donor, all outputs on E:."""
from pathlib import Path
import argparse,json,os,subprocess,hashlib,shutil,re,statistics,time
parser=argparse.ArgumentParser();parser.add_argument('--case',default='all');a=parser.parse_args()
B=Path('E:/项目/Veyra');T='export-nr-scale-20261025';R=Path(__file__).resolve().parents[2];D=B/'build/claude-screen-recording-20261022';A=B/'tests'/T/'app';M=B/'tests'/T/'media';L=B/'logs'/T/'exports';O=B/'reports'/T;TMP=B/'tmp'/T/'exports'
for p in [M,L,O,TMP]:p.mkdir(parents=True,exist_ok=True)
ff=shutil.which('ffmpeg');fp=shutil.which('ffprobe');assert ff and fp
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def replace(p,data):
    if p.exists():p.unlink()
    p.write_bytes(data)
donor=B/'releases/release-2.1.0-20261019/runtime-base/NVIDIA';dm=json.loads((donor/'package-manifest.json').read_text('utf8'))
if not A.exists():
    for e in dm['files']:
        rel=e['path']
        if rel.startswith('logs/') or any(x.startswith('user-data-') for x in Path(rel).parts):continue
        p=A/rel;p.parent.mkdir(parents=True,exist_ok=True);os.link(donor/rel,p)
    (A/'runtime_local').mkdir(exist_ok=True)
    shutil.copyfile(donor/'package-manifest.json',A/'package-manifest.json')
replace(A/'veyra_nr_export_size_tests.exe',(D/'veyra_nr_export_size_tests.exe').read_bytes())
for e in dm['files']:
    if Path(e['path']).suffix.lower() in ['.dll','.onnx','.tflite','.hsaco','.dxil']:assert sha(A/e['path'])==e['sha256']
cases=[('4k-1080p',3840,2160,'0','h264',[(1920,1080)]),('4k-50pct',3840,2160,'150','hevc',[(1920,1080)]),('4k-37pct',3840,2160,'137','hevc',[(1420,798)]),('4k-100pct',3840,2160,'200','hevc',[(3840,2160)]),('4k-mixed',3840,2160,'3,150','hevc',[(1280,720),(1920,1080)]),('4k-auto',3840,2160,'6','hevc',[(2560,1440)]),('8k-1080p',7680,4320,'0','hevc',[(1920,1080)]),('8k-25pct',7680,4320,'125','hevc',[(1920,1080)])]
cases.append(('4k-flow',3840,2160,'137','hevc',[(1420,798)]))
if a.case!='all':cases=[c for c in cases if c[0]==a.case];assert cases
results=[]
for name,w,h,policies,codec,expected in cases:
    running=subprocess.check_output(['tasklist','/fo','csv','/nh'],text=True,errors='replace');assert not any(l.lower().startswith('"veyra') for l in running.splitlines()),'another Veyra process is running; do not compete'
    caseTmp=TMP/name;caseTmp.mkdir(exist_ok=True);env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))};env.update(TEMP=str(caseTmp),TMP=str(caseTmp),APPDATA=str(caseTmp/'AppData'),LOCALAPPDATA=str(caseTmp/'LocalAppData'),CUDA_CACHE_PATH=str(TMP/'cuda'))
    media=M/(str(w)+'x'+str(h)+'-6frames.mp4')
    if not media.exists():subprocess.run([ff,'-v','error','-f','lavfi','-i',f'testsrc2=size={w}x{h}:rate=30','-frames:v','6','-c:v','libx264','-threads','2','-preset','ultrafast','-crf','18','-pix_fmt','yuv420p',str(media)],env=env,check=True,timeout=180)
    out=B/'tests'/T/(name+'.mp4');assert not out.exists(),out
    log=L/(name+'.log');start=time.monotonic()
    with log.open('wb') as f:p=subprocess.run([str(A/'veyra_nr_export_size_tests.exe'),str(media),str(out),policies,codec,'6']+(['flow'] if name=='4k-flow' else []),cwd=A,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=270)
    text=log.read_text('utf8',errors='replace');worker=next((x[11:] for x in text.splitlines() if x.startswith('WORKER_LOG ')),'');wt=Path(worker).read_text('utf8',errors='replace') if worker else ''
    actual=[tuple(map(int,x)) for x in re.findall(r'\[export-nr-layer\].*?internal=(\d+)x(\d+)',wt)]
    created=[tuple(map(int,x)) for x in re.findall(r'\[nr-model\].*?internal=(\d+)x(\d+)',wt)]
    result=dict(case=name,exit=p.returncode,seconds=round(time.monotonic()-start,3),expected=expected,actual=actual,created=created,log=str(log),workerLog=worker,output=str(out),passed=False)
    if p.returncode==0 and actual==expected and created==expected:
        probe=json.loads(subprocess.check_output([fp,'-v','error','-count_frames','-show_streams','-of','json',str(out)],env=env,text=True));video=next(x for x in probe['streams'] if x['codec_type']=='video')
        raw=subprocess.check_output([ff,'-v','error','-i',str(out),'-vf','select=eq(n\\,3),scale=320:180','-frames:v','1','-pix_fmt','rgb24','-f','rawvideo','-'],env=env,timeout=120)
        assert len(raw)==320*180*3 and statistics.pstdev(raw)>10
        result.update(width=video['width'],height=video['height'],decodedFrames=int(video['nb_read_frames']),codec=video['codec_name'],pixelStddev=statistics.pstdev(raw),outputSha256=sha(out))
        result['passed']=video['width']==w and video['height']==h and int(video['nb_read_frames'])==6 and 'NR_EXPORT_SIZE ok=1' in text and '[ERROR]' not in wt
    results.append(result);(O/(name+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=1),'utf8');print(json.dumps(result,ensure_ascii=False),flush=True)
    if not result['passed']:print(wt[-3500:],text[-1800:],flush=True);break
(O/('gpu-exports-'+a.case+'.json')).write_text(json.dumps(results,ensure_ascii=False,indent=1),'utf8')
assert all(x['passed'] for x in results),results[-1]
