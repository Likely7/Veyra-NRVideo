"""Serial, bounded GPU checks with immutable approved runtime components."""
from pathlib import Path
import os,json,sys,subprocess,shutil,hashlib,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='release-2.0.5-20261007'
label,case=sys.argv[1:3];assert label.replace('-','').isalnum()
out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/release-2.0.5-control.py')],check=True)
out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
app=BASE/'test-packages/obs-export-amdnr-20261006/Veyra-2.0.4-fix1-NVIDIA-win64-portable'
build=BASE/'build'/TASK/'standard'
for p in app.glob('*.dll'):shutil.copyfile(p,out/p.name)
shutil.copytree(build/'shaders',out/'shaders')
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOCAL_DATA_DIR=str(out/'profile'),VEYRA_LOG_FILE=str(logs/(label+'-engine.log')),VEYRA_TEST_EXPORT_DEBUG='1')
results=[];args=[]
if case=='units':targets=['veyra_effect_chain_tests','veyra_repair_contract_tests','veyra_preset_library_tests','veyra_ui_i18n_tests','veyra_effect_availability_tests','veyra_live_timing_tests','veyra_hdr_tuning_tests']
elif case=='graph':
    targets=['veyra_amd_nr_graph_tests'];d=out/'runtime/amd-nr';d.mkdir(parents=True);(d/'assets').mkdir()
    shutil.copyfile(build/'lmxxf-test-runtime/LmxxfNrRuntime.dll',d/'LmxxfNrRuntime.dll');(out/'runtime/nvidia').mkdir()
elif case in ('presentation','display-sync'):
    targets=['veyra_fg_presentation_tests'];shutil.copytree(app/'runtime',out/'runtime');shutil.copytree(app/'runtime_local',out/'runtime_local')
    args=[str(out/'pixels'),str(out/'runtime/experimental')]
    if case=='display-sync':args.append(case)
else:
    targets=['veyra_export_workflow_tests'];args=[case,str(BASE/'tests/release-2.0.4-20261005/nr-fixture.mp4'),str(out/'result.mp4')]
    shutil.copytree(app/'runtime',out/'runtime');shutil.copytree(app/'runtime_local',out/'runtime_local')
for target in targets:
    exe=out/(target+'.exe');shutil.copyfile(build/exe.name,exe)
    log=logs/(label+'-'+target+'.log');begin=time.monotonic()
    with log.open('xb') as console:
        try:rc=subprocess.run([str(exe),*args],cwd=out,env=env,stdout=console,stderr=subprocess.STDOUT,timeout=290).returncode
        except subprocess.TimeoutExpired:rc=124
    body=log.read_text(encoding='utf8',errors='replace')
    error_lines=[line for line in body.splitlines() if 'D3D12 ERROR' in line or 'D3D12 CORRUPTION' in line]
    passed=rc==0 and not error_lines
    if case=='display-sync':
        import re
        sections=re.findall(r'DISPLAY_SYNC_BEGIN full=(\d) lowQueue=(\d) mode=(\d)(.*?)DISPLAY_SYNC_END',body,re.S)
        passed=passed and len(sections)==12
        for full,low,mode,section in sections:
            calls=re.findall(r'\[present-contract\] backend=DXGI sync=(\d) flags=0x([0-9A-F]+) hr=0x([0-9A-F]+)',section)
            passed=passed and bool(calls) and all(int(sync)==(0 if mode=='0' else 1) and int(flags,16)==(0x200 if mode=='0' else 0) and int(hr,16)==0 for sync,flags,hr in calls)
    results.append(dict(target=target,exit=rc,passed=passed,seconds=time.monotonic()-begin,exeSHA=hashlib.sha256(exe.read_bytes()).hexdigest(),log=str(log),gpuErrors=error_lines))
    print(target,rc,'PASS' if passed else 'FAIL',flush=True);print('\n'.join(body.splitlines()[-11:]),flush=True)
if (out/'result.mp4').exists():
    probe=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-count_frames','-show_streams','-of','json',str(out/'result.mp4')],timeout=30))
    (out/'ffprobe.json').write_text(json.dumps(probe,indent=2),encoding='utf8')
    video=next(s for s in probe['streams'] if s['codec_type']=='video')
    assert int(video['nb_read_frames'])==(120 if case.startswith('fsr-') else {'lifecycle':20,'lifecycle-boundary':2}.get(case,60)),video
    decoded=subprocess.run([shutil.which('ffmpeg'),'-v','error','-xerror','-i',str(out/'result.mp4'),'-map','0:v:0','-f','null','-'],capture_output=True,timeout=60)
    assert decoded.returncode==0,decoded.stderr.decode('utf8',errors='replace')
    results.append(dict(decodedEveryEncodedFrame=True,dimensions=[video['width'],video['height']],frames=int(video['nb_read_frames']),passed=True))
(logs/(label+'.json')).write_text(json.dumps(results,indent=2),encoding='utf8')
raise SystemExit(0 if all(x['passed'] for x in results) else 1)
