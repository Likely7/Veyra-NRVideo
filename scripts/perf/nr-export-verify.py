"""Verify all completed exports after timed processes exit; whole decoded frames + PTS."""
from pathlib import Path
import importlib.util,json,os,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
labels=sys.argv[1:];assert labels;matrix.assert_gpu_tests_idle();ffmpeg=shutil.which('ffmpeg');ffprobe=shutil.which('ffprobe');assert ffmpeg and ffprobe
results=[];references={}
for label in labels:
 folder=BASE/'logs'/TASK/(label+'-summary');summary=json.loads((folder/'summary.json').read_text(encoding='utf-8'))
 for run in summary['runs']:
  matrix.assert_gpu_tests_idle();out=BASE/'logs'/TASK/run['name'];source=Path(run['command'][2]);assert matrix.digest(source)==run['outputSha256']
  print('VERIFY',run['name'],flush=True);env=os.environ.copy();env.update(TEMP=str(BASE/'tmp'/TASK/run['name']),TMP=str(BASE/'tmp'/TASK/run['name']))
  metadata=subprocess.run([ffprobe,'-v','error','-count_frames','-show_streams','-show_format','-of','json',str(source)],capture_output=True,env=env,timeout=180,check=True)
  (out/'ffprobe.json').write_bytes(metadata.stdout);data=json.loads(metadata.stdout)
  video=next(s for s in data['streams'] if s['codec_type']=='video');assert int(video['nb_read_frames'])==int(run['metrics']['encoded']),run['name']
  hashfile=out/'decoded.framemd5';assert not hashfile.exists()
  with (out/'decode-console.log').open('xb') as stream:
   subprocess.run([ffmpeg,'-v','error','-threads','2','-i',str(source),'-map','0:v:0','-fps_mode','passthrough','-f','framemd5',str(hashfile)],stdout=stream,stderr=subprocess.STDOUT,env=env,timeout=180,check=True)
  hashes=[s for s in hashfile.read_text().splitlines() if s and not s.startswith('#')];assert len(hashes)==int(run['metrics']['encoded'])
  signature={'codec':video['codec_name'],'width':video['width'],'height':video['height'],'rate':video['avg_frame_rate'],'timeBase':video['time_base'],
   'audio':[{k:s.get(k) for k in ('codec_name','channels','sample_rate','start_time','duration')} for s in data['streams'] if s['codec_type']=='audio'],
   'formatDuration':data['format'].get('duration'),'hashes':hashes}
  group=run['group'];reference=references.setdefault(group,{'name':run['name'],'signature':signature});expected=reference['signature']
  diffs=sum(a!=b for a,b in zip(signature['hashes'],expected['hashes']))
  sameMeta={k:v for k,v in signature.items() if k!='hashes'}=={k:v for k,v in expected.items() if k!='hashes'}
  result={'name':run['name'],'reference':reference['name'],'group':group,'differentDecodedRows':diffs,'sameMetadata':sameMeta,'frames':len(hashes),'passed':diffs==0 and sameMeta,
   'width':video['width'],'height':video['height'],'duration':signature['formatDuration'],'outputSha256':run['outputSha256'],'framemd5Sha256':matrix.digest(hashfile)}
  results.append(result);(folder/'verification-progress.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8');print('RESULT',json.dumps(result),flush=True)
  # Preserve mismatches and finish the matrix; a mismatch never becomes an accepted reference.
report={'labels':labels,'runs':results,'passed':all(r['passed'] for r in results),'note':'Entire reconstructed video frames/PTS plus mux metadata. No GPU timing during software decoding.'}
dest=BASE/'logs'/TASK/(labels[-1]+'-decoded-review.json');assert not dest.exists();dest.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print('EXPORT_VERIFY_COMPLETE',report['passed'],len(results),dest,flush=True);raise SystemExit(0 if report['passed'] else 1)
