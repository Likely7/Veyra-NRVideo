"""Inspect saved actual outputs after GPU work; full RGB PSNR and FFmpeg SSIM."""
from pathlib import Path
import csv,importlib.util,json,math,os,re,shutil,subprocess,sys
import numpy as np
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
label=sys.argv[1];matrix.assert_gpu_tests_idle();ffmpeg=shutil.which('ffmpeg');ffprobe=shutil.which('ffprobe');assert ffmpeg and ffprobe
out=BASE/'tests'/TASK/(label+'-quality');out.mkdir(parents=True,exist_ok=False);tmp=BASE/'tmp'/TASK/(label+'-quality');tmp.mkdir(parents=True,exist_ok=False)
env=os.environ.copy();env.update(TEMP=str(tmp),TMP=str(tmp));results=[]
html=['<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>Auto NR actual output comparison</title><style>body{background:#18191c;color:#eee;font-family:system-ui;margin:24px}.pair{display:flex;gap:12px}figure{margin:0;flex:1}img{width:100%}a{color:#9abaff}</style><h1>Auto NR: fixed 100% versus each actual extent</h1><p>Same source frame and reset boundaries. Lower extents change the output; PSNR/SSIM measure differences, not quality improvement. No human quality approval.</p>']
sheet=Image.new('RGB',(960,6*292),(24,25,28));draw=ImageDraw.Draw(sheet)
for frame in (0,14,15,29,30,44,45,59,60,74,75,89):
 paths=[BASE/'tests'/TASK/(label+'-auto-'+mode)/f'frame-{frame}.png' for mode in ('fixed','reference')]
 a,b=[np.asarray(Image.open(p).convert('RGBA'),dtype=np.int16) for p in paths];d=a-b;mse=float(np.mean(d[:,:,:3].astype(np.float64)**2))
 command=[ffmpeg,'-hide_banner','-nostdin','-threads','1','-i',str(paths[0]),'-threads','1','-i',str(paths[1]),'-filter_complex_threads','1','-lavfi','[0:v]format=gbrp[a];[1:v]format=gbrp[b];[a][b]ssim','-frames:v','1','-f','null','NUL']
 proc=subprocess.run(command,capture_output=True,env=env,timeout=30,check=True);log=proc.stderr.decode('utf-8',errors='replace');(out/f'ssim-{frame}.log').write_text(log,encoding='utf-8')
 ssim=re.findall(r'SSIM .*?All:([\d.]+)',log);assert len(ssim)==1,log
 level=(0,1,2,3,4,0)[frame//15];percent=(100,85,70,55,40)[level]
 row={'frame':frame,'firstLayerPercent':percent,'psnrRgb8Db':10*math.log10(255**2/mse) if mse else None,'ssimFfmpegGbrp':float(ssim[0]),
  'maxRgb8Difference':int(np.abs(d[:,:,:3]).max()),'changedRgbPixelsPercent':float(np.mean(np.any(d[:,:,:3]!=0,axis=2))*100),'alphaDifferentPixels':int(np.count_nonzero(d[:,:,3]))}
 results.append(row);assert row['alphaDifferentPixels']==0
 if percent==100:assert not np.any(d),'100% pool/fixed image differs'
 html.append(f'<h2>Frame {frame}: {percent}% first NR</h2><p>{json.dumps(row)}</p><div class="pair">')
 for p,title in zip(paths,('Fixed 100%','Current actual NR extent')):html.append(f'<figure><figcaption>{title}</figcaption><a href="{p.as_uri()}"><img src="{p.as_uri()}"></a></figure>')
 html.append('</div>')
 if frame%15==14:
  j=frame//15;draw.text((4,j*292+4),f'Frame {frame} / fixed 100 | first layer {percent}%',fill='white')
  for i,p in enumerate(paths):sheet.paste(Image.open(p).convert('RGB').resize((480,270)),(i*480,j*292+22))
sheet.save(out/'comparison.png');html.append('</html>');(out/'review.html').write_text('\n'.join(html),encoding='utf-8')
# Entire exports have already matched encoded bytes; additionally verify all
# reconstructed frames, timestamps, duration and audio from the actual files.
exports=[]
for mode in ('nr2','nr2-auto'):
 source=BASE/'tests'/TASK/(label+'-export-'+mode)/'export.mkv'
 meta=subprocess.run([ffprobe,'-v','error','-count_frames','-show_streams','-show_format','-of','json',str(source)],capture_output=True,env=env,timeout=60,check=True)
 data=json.loads(meta.stdout);(out/(mode+'-ffprobe.json')).write_bytes(meta.stdout)
 video=next(s for s in data['streams'] if s['codec_type']=='video');assert int(video['nb_read_frames'])==120
 dest=out/(mode+'-decoded.framemd5');subprocess.run([ffmpeg,'-v','error','-threads','2','-i',str(source),'-map','0:v:0','-fps_mode','passthrough','-f','framemd5',str(dest)],capture_output=True,env=env,timeout=90,check=True)
 rows=[s for s in dest.read_text().splitlines() if s and not s.startswith('#')];assert len(rows)==120
 audio=[{k:s.get(k) for k in ('codec_name','channels','sample_rate','start_time','duration')} for s in data['streams'] if s['codec_type']=='audio']
 exports.append({'mode':mode,'encodedSha256':matrix.digest(source),'decodedRows':rows,'audio':audio,'duration':data['format']['duration'],'width':video['width'],'height':video['height']})
assert {k:v for k,v in exports[0].items() if k!='mode'}=={k:v for k,v in exports[1].items() if k!='mode'},'Auto native export metadata/full frames differ'
(out/'review.json').write_text(json.dumps({'frames':results,'exports':exports,'wholeExportDecodedFramesCompared':240,'differentDecodedRows':0,'humanQualityApproval':False,
 'ssimDefinition':'FFmpeg ssim filter on lossless gbrp conversion; independent plane/block metric. Different configurations are not expected to match.'},ensure_ascii=False,indent=2),encoding='utf-8')
matrix.assert_gpu_tests_idle();print('AUTO_QUALITY_COMPLETE',json.dumps(results),out,flush=True)
