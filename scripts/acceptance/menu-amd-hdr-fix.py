"""Bounded local regressions. Synthetic AMD provider, real D3D12 graph; no capture."""
from pathlib import Path
import argparse,json,os,re,shutil,subprocess,sys,time

R=Path(__file__).resolve().parents[2];B=Path('E:/项目/Veyra');T='menu-amd-hdr-fix-20261026'
D=B/'build/claude-screen-recording-20261022';O=B/'reports'/T;L=B/'logs'/T;A=B/'tests'/T/'amd-graph'
parser=argparse.ArgumentParser();parser.add_argument('mode',choices=['gpu','menu','contracts']);args=parser.parse_args()
for p in [O,L]:p.mkdir(parents=True,exist_ok=True)
if args.mode!='gpu':
    # The executable's POST_BUILD staging is skipped when C++ is unchanged.
    shutil.copytree(R/'tests/qml/quick',D/'qml-tests',dirs_exist_ok=True)

def environment(tag):
    tmp=B/'tmp'/T/tag;tmp.mkdir(parents=True,exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('VEYRA_','QT_','QSG_','QML_','DLSS5_'))}
    env.update(TEMP=str(tmp),TMP=str(tmp),APPDATA=str(tmp/'AppData'),LOCALAPPDATA=str(tmp/'LocalAppData'),CUDA_CACHE_PATH=str(tmp/'cuda'),VEYRA_LOCAL_DATA_DIR=str(tmp/'profile'),QT_FORCE_STDERR_LOGGING='1',QML_DISABLE_DISK_CACHE='1')
    env['PATH']=str(B/'deps/qt/6.8.3/msvc2022_64/bin')+';'+str(B/'deps/ffmpeg-filter-issues41-45-20261016/bin')+';'+env['PATH']
    env['VEYRA_LOG_FILE']=str(L/(tag+'-engine.log'));return env

def run(tag,command,cwd,env,qt=False):
    log=L/(tag+'.txt');start=time.monotonic()
    if qt:command+=[ '-o',str(log)+',txt']
    with (log.with_suffix('.console') if qt else log).open('wb') as f:
        rc=subprocess.run(command,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=280).returncode
    text=log.read_text('utf-8',errors='replace') if log.exists() else ''
    row=dict(tag=tag,exit=rc,seconds=round(time.monotonic()-start,3),log=str(log),command=command)
    print(json.dumps(row,ensure_ascii=False),flush=True)
    if rc:print(text[-3500:],flush=True)
    return row,text

if args.mode=='gpu':
    assert not A.exists(),'Use a new run directory to preserve evidence'
    A.mkdir(parents=True);donor=B/'releases/release-2.1.0-20261019/runtime-base/AMD'
    for p in donor.glob('*.dll'):shutil.copyfile(p,A/p.name)
    shutil.copytree(D/'shaders',A/'shaders')
    shutil.copytree(donor/'runtime_local/amd/fidelityfx',A/'runtime_local/amd/fidelityfx')
    for rel in ['runtime/amd-nr','lmxxf-test-runtime']:
        dest=A/rel;dest.mkdir(parents=True);(dest/'assets').mkdir()
        for p in (D/'lmxxf-test-runtime').glob('*.dll'):shutil.copyfile(p,dest/p.name)
    (A/'runtime/nvidia').mkdir()
    results=[]
    for name in ['amd_nr_graph','lmxxf_nr','hdr_color']:
        exe='veyra_'+name+'_tests.exe';shutil.copyfile(D/exe,A/exe)
        row,text=run(name,[str(A/exe)],A,environment(name));results.append(row)
        if name=='amd_nr_graph':
            markers=re.findall(r'^AMD_HDR_GRAPH .*',text,re.M)
            row.update(hdrCases=len(markers),fakeProvider=True,hipInference=False)
            assert len(markers)==108 and all('pass=1' in s for s in markers) and 'D3D12_DEBUG errors=0' in text,text[-3500:]
        (O/'gpu.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),'utf-8')
        assert row['exit']==0,text[-3500:]
elif args.mode=='menu':
    results=[]
    for mode in ['software','d3d12']:
        for scale in ['1','1.5','2']:
            tag='menu-'+mode+'-'+scale;env=environment(tag);env.update(QT_QPA_PLATFORM='windows',QT_SCALE_FACTOR=scale)
            if mode=='software':env['QT_QUICK_BACKEND']='software'
            else:env['QSG_RHI_BACKEND']='d3d12'
            row,text=run(tag,[str(D/'veyra_qml_quick_tests.exe'),'MenuAndRowLayout::test_open_motion_preserves_text_layout'],D,env,True)
            results.append(row);(O/'menu.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),'utf-8')
            assert row['exit']==0 and '0 failed' in text and '4 passed' in text,text[-3500:]
            assert 'MENU_LAYOUT_RENDERER software='+str(mode=='software').lower() in text,text[-3500:]
else:
    results=[]
    for name in ['effect_chain','ui_contract','repair_contract','ui_i18n','qml_quick','translation']:
        env=environment('contract-'+name);env.update(QT_QPA_PLATFORM='windows',QT_QUICK_BACKEND='software')
        command=[sys.executable,'-B',str(R/'scripts/i18n/extract.py'),'--check'] if name=='translation' else [str(D/('veyra_'+name+'_tests.exe'))]
        row,text=run('contract-'+name,command,R,env,name=='qml_quick');results.append(row)
        (O/'contracts.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),'utf-8')
        assert row['exit']==0,text[-3500:]
        if name=='qml_quick':assert '117 passed' in text and '0 failed' in text,text[-3500:]
