"""Bounded integration checks; real DXGI pixels, not physical HDR/HIP claims."""
from pathlib import Path
import os,json,sys,subprocess,shutil,hashlib,time
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='pr19-pr20-20261006'
label,case=sys.argv[1:3];out=BASE/'tests'/TASK/label;tmp=BASE/'tmp'/TASK/label;logs=BASE/'logs'/TASK
subprocess.run([sys.executable,'-B',str(ROOT/'scripts/acceptance/pr19-pr20-control.py')],check=True)
out.mkdir(parents=True,exist_ok=False);tmp.mkdir(parents=True,exist_ok=False)
app=BASE/'test-packages/obs-export-amdnr-20261006/Veyra-2.0.4-fix1-NVIDIA-win64-portable'
build=BASE/'build'/TASK/'standard'
for p in app.glob('*.dll'):shutil.copyfile(p,out/p.name)
shutil.copytree(build/'shaders',out/'shaders')
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_'))}
env.update(TEMP=str(tmp),TMP=str(tmp),VEYRA_LOCAL_DATA_DIR=str(out/'profile'),VEYRA_LOG_FILE=str(logs/(label+'-engine.log')))
results=[];args=[]
if case=='units':targets=['veyra_hdr_tuning_tests','veyra_effect_chain_tests','veyra_preset_library_tests','veyra_ui_i18n_tests']
elif case=='hdr':targets=['veyra_hdr_output_tuning_gpu_tests','veyra_hdr_color_tests']
elif case=='graph':
    targets=['veyra_amd_nr_graph_tests'];d=out/'runtime/amd-nr';d.mkdir(parents=True);(d/'assets').mkdir()
    shutil.copyfile(build/'lmxxf-test-runtime/LmxxfNrRuntime.dll',d/'LmxxfNrRuntime.dll');(out/'runtime/nvidia').mkdir()
elif case=='abi':targets=['veyra_lmxxf_nr_tests'];shutil.copytree(build/'lmxxf-test-runtime',out/'lmxxf-test-runtime')
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
    if target=='veyra_hdr_output_tuning_gpu_tests':
        section=body.split('METADATA_RESIZE_BEGIN')[-1].split('METADATA_RESIZE_END')[0]
        accepted='SetHDRMetaData hr=0x00000000' in section
        cleared='clear swapchain metadata hr=0x00000000' in section
        # A platform that accepts an active hint must also clear it after resize.
        passed=passed and 'METADATA_RESIZE_END' in body and (not accepted or cleared)
        print('METADATA_RESIZE accepted=',accepted,'cleared=',cleared)
    results.append(dict(target=target,exit=rc,passed=passed,seconds=time.monotonic()-begin,exeSHA=hashlib.sha256(exe.read_bytes()).hexdigest(),log=str(log),gpuErrors=error_lines))
    print(target,rc,'PASS' if passed else 'FAIL');print('\n'.join(body.splitlines()[-11:]))
if (out/'result.mp4').exists():
    probe=json.loads(subprocess.check_output([shutil.which('ffprobe'),'-v','error','-count_frames','-show_streams','-of','json',str(out/'result.mp4')],timeout=30))
    (out/'ffprobe.json').write_text(json.dumps(probe,indent=2),encoding='utf8')
    video=next(s for s in probe['streams'] if s['codec_type']=='video')
    assert int(video['nb_read_frames'])==({'lifecycle':20,'lifecycle-boundary':2}.get(case,60))
(logs/(label+'.json')).write_text(json.dumps(results,indent=2),encoding='utf8')
raise SystemExit(0 if all(x['passed'] for x in results) else 1)
