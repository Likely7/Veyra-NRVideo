"""Fresh local 2.0.8 app/test build; pinned external SDK paths from the accepted cache.
No downloads, package publication, or mutations of other build trees.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,time
parser=argparse.ArgumentParser()
parser.add_argument('--task',default='claude-release-2.0.8-20261015')
parser.add_argument('--label',default='2.0.8-test')
parser.add_argument('--extra-target',action='append',default=[])
args=parser.parse_args()
assert re.fullmatch(r'[a-z0-9][a-z0-9.-]{0,79}',args.task)
assert re.fullmatch(r'2\.0\.8-[A-Za-z0-9.-]+',args.label)
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK=args.task;LABEL=args.label
BUILD=BASE/'build'/TASK;TMP=BASE/'tmp'/TASK/'build';LOG=BASE/'logs'/TASK/('build-'+time.strftime('%Y%m%d-%H%M%S'))
for p in (BUILD,TMP,LOG):p.mkdir(parents=True,exist_ok=True)
def sha(p):
    with Path(p).open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args],text=True,encoding='utf8').strip()
def save(p,data):p.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n','utf8')
assert not git('status','--porcelain'),'Commit build inputs first'
cache=BASE/'build/vfg-next-20261007/CMakeCache.txt';text=cache.read_text('utf8')
options={k:v for k,t,v in re.findall(r'^(VEYRA_[^:]+|CMAKE_MAKE_PROGRAM|CMAKE_PREFIX_PATH):([^=]+)=(.*)$',text,re.M) if t not in ('INTERNAL','STATIC')}
for k in ('PROTOC','PKG_CONFIG_EXECUTABLE','SDL3_DIR','LibDataChannel_DIR','Opus_DIR','nlohmann_json_DIR'):
    options[k]=re.search(r'^'+k+r':[^=]+=(.*)$',text,re.M).group(1)
options.update(VEYRA_DECKLINK_SDK_DIR=str(BASE/'deps/decklink-sdk-obs31-20261009'),
    VEYRA_AMD_NR_TEST_GRAPH_SOURCE=str(ROOT/'src/pipeline/EnhanceGraph.cpp'),
    VEYRA_FFMPEG_ROOT=str(BASE/'deps/ffmpeg-filter-issues41-45-20261016'),
    VEYRA_DISPLAY_VERSION=LABEL,Python3_EXECUTABLE=os.sys.executable)
assert all(options['VEYRA_ENABLE_'+f]=='ON' for f in ('REMOTEPLAY','MOONLIGHT','XBOX'))
env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_'))}
env.update(TEMP=str(TMP),TMP=str(TMP),VSLANG='1033',PYTHONDONTWRITEBYTECODE='1')
setup=subprocess.run(['cmd.exe','/u','/d','/c',r'call C:\PROGRA~2\MICROS~3\2022\BUILDT~1\VC\Auxiliary\Build\vcvars64.bat >nul && set'],stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,encoding='utf-16-le',errors='replace',env=env,check=True)
for line in setup.stdout.splitlines():
    if '=' in line:
        k,v=line.split('=',1)
        if k and not k.startswith('='):env[k]=v
env.update(TEMP=str(TMP),TMP=str(TMP),PYTHONDONTWRITEBYTECODE='1')
# The audited subtitle FFmpeg staging tree contains DLLs/DEFs and headers, but
# no MSVC import libraries. Generate those from the matching DEF export lists
# in this build tree; never modify the DLLs or external dependency tree.
importdir=BUILD/'ffmpeg-imports';importdir.mkdir(exist_ok=True)
lib=r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/lib.exe'
ffimports=[]
for short,dll in [('avformat','avformat-63'),('avcodec','avcodec-63'),('swresample','swresample-7'),('swscale','swscale-10'),('avutil','avutil-61')]:
    definition=Path(options['VEYRA_FFMPEG_ROOT'])/'lib'/(dll+'.def');target=importdir/(short+'.lib')
    with (LOG/('import-'+short+'.log')).open('wb') as stream:
        subprocess.run([lib,'/nologo','/machine:x64','/def:'+str(definition),'/name:'+dll+'.dll','/out:'+str(target)],cwd=TMP,env=env,stdout=stream,stderr=subprocess.STDOUT,check=True,timeout=30)
    options['FFMPEG_lib'+short+'_LIBRARY']=str(target)
    options['FFMPEG_lib'+short+'_LIBRARY_RELEASE']=str(target)
    options['FFMPEG_lib'+short+'_LIBRARY_RELEASE_DIR']=str(importdir)
    ffimports.append(dict(definition=str(definition),definitionSha256=sha(definition),path=str(target),sha256=sha(target)))
cmake=r'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
targets=['veyra_qml_ui','veyra_xess_motion_contract_tests','veyra_xess_motion_quality_tests','veyra_fg_xess_boundary_tests',
    'veyra_fg_motion_boundary_tests','veyra_capture_cadence_fg_tests','veyra_scene_tests','veyra_live_timing_tests',
    'veyra_effect_chain_tests','veyra_preset_library_tests','veyra_repair_contract_tests','veyra_ui_contract_tests',
    'veyra_hdr_color_tests','veyra_live_presentation_tests','veyra_settings_transaction_tests',
    'veyra_present_sink_lifecycle_tests','veyra_decklink_tests','veyra_capture_format_selection_tests',
    'veyra_capture_color_tests','veyra_vfg_gpu_tests','veyra_fsr_dispatch_tests','veyra_qml_quick_tests',
    'veyra_playlist_tests','veyra_qml_data_tests','veyra_bitmap_subtitle_tests','veyra_subtitle_format_tests','veyra_capture_tests',
    # Added or changed by the branches merged for 2.0.8.
    'veyra_update_checker_tests','veyra_portable_update_rules_tests','veyra_portable_update_security_tests','veyra_realtime_preview_tests','veyra_screen_capture_tests','veyra_preview_geometry_tests',
    'veyra_experimental_backend_tests','veyra_remoteplay_boundary_tests','veyra_ui_i18n_tests','veyra_fg_presentation_tests']
assert all(re.fullmatch(r'veyra_[a-z0-9_]+',target) for target in args.extra_target)
targets=list(dict.fromkeys(targets+args.extra_target))
inputs={p.relative_to(ROOT).as_posix():sha(p) for folder in ('include','src','apps','shaders','qml','tests','cmake','i18n') for p in (ROOT/folder).rglob('*') if p.is_file()}
inputs['CMakeLists.txt']=sha(ROOT/'CMakeLists.txt');commands=[];start=time.monotonic()
configure=[cmake,'-S',str(ROOT),'-B',str(BUILD),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release']+['-D'+k+'='+v for k,v in options.items()]
build=[cmake,'--build',str(BUILD),'--parallel','8','--target',*targets]
results=[]
for name,command in (('configure',configure),('compile',build)):
    commands.append(command)
    with (LOG/(name+'.log')).open('wb') as stream:
        try:rc=subprocess.run(command,cwd=TMP,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=max(1,890-(time.monotonic()-start))).returncode
        except subprocess.TimeoutExpired:rc=124
    results.append(dict(step=name,exit=rc));print(name,rc,LOG/(name+'.log'),flush=True)
    if rc:
        print((LOG/(name+'.log')).read_text('utf8',errors='replace')[-9000:],flush=True)
        save(LOG/'failure.json',dict(results=results,commands=commands,seconds=time.monotonic()-start));raise SystemExit(rc)
for name,h in inputs.items():assert sha(ROOT/name)==h,name
exes={n+'.exe':sha(BUILD/(n+'.exe')) for n in targets}
receipt=dict(passed=True,head=git('rev-parse','HEAD'),displayVersion=LABEL,freshCMakeBuild=True,
    sources=inputs,commands=commands,dependencies=options,ffmpegImportLibraries=ffimports,targets=targets,executables=exes,seconds=time.monotonic()-start)
save(LOG/'receipt.json',receipt)
report=BASE/'reports'/TASK;report.mkdir(parents=True,exist_ok=True)
save(report/'build.json',dict(receipt=str(LOG/'receipt.json'),sha256=sha(LOG/'receipt.json')))
print('PASS fresh source build',len(targets),'targets',round(receipt['seconds'],1),'seconds',flush=True)
