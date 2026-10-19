"""Build the pinned MIT runtime from its original checkout; no injection module."""
import argparse, hashlib, json, os, re, shutil, subprocess
from pathlib import Path

PIN = '4764e4f1a24f5234b8e29952dc41f493b2d77c90' #0.44 F9 hotfix; source stays outside Git.
p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--model', choices=('small','original'), default='small')
p.add_argument('--build', type=Path, required=True)
p.add_argument('--tmp', type=Path, required=True)
p.add_argument('--log', type=Path, required=True)
a = p.parse_args()
source = a.source.resolve()
if (source / '.git').exists():
    head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
    if head != PIN or subprocess.check_output(['git','diff','--name-only','HEAD'],cwd=source):
        raise SystemExit('Expected an unchanged pinned upstream checkout')
else:
    # The corresponding-source archive deliberately omits weights, captures,
    # compiled modules and Git metadata. Validate its supplied source manifest.
    if (source / 'SOURCE_COMMIT.txt').read_text(encoding='utf8').splitlines()[0] != PIN:
        raise SystemExit('Unexpected archived source commit')
    records = json.loads((source.parent / 'source-manifest.json').read_text(encoding='utf8'))
    prefix = source.name + '/'
    selected = [r for r in records if r['path'].startswith(prefix)]
    if not selected:
        raise SystemExit('Missing archived source identities')
    for row in selected:
        file = (source.parent / row['path']).resolve()
        if not file.is_relative_to(source) or file.stat().st_size != row['size'] or hashlib.sha256(file.read_bytes()).hexdigest() != row['sha256']:
            raise SystemExit('Archived source changed: ' + row['path'])
attributes = []
for file in (source/'src').glob('*.h'):
    attributes += re.findall(r'__attribute__\(\(([^)]*)\)\)', file.read_text(encoding='utf8'))
for file in (source/'Development/HIP').glob('*.h'):
    attributes += re.findall(r'__attribute__\(\(([^)]*)\)\)', file.read_text(encoding='utf8'))
if not attributes or any(x != 'noinline' for x in attributes):
    raise SystemExit('Review MSVC compatibility shim for changed GNU attributes')
for target in (a.build,a.tmp,a.log.parent):
    target.resolve().relative_to(Path('E:/项目/Veyra').resolve())
    target.mkdir(parents=True,exist_ok=True)
# Compile isolated copies of the same MIT implementation. The model selector is
# fixed per DLL, so different NR layers never exchange a process-global mode and
# upstream game hotkeys cannot write to Veyra's configuration. Weights/kernels
# are neither copied into the build tree nor modified.
build_source = a.build/'source'
inputs = {}
for directory in ('src','include','Development/HIP'):
    for file in (source/directory).rglob('*'):
        if file.is_file() and file.suffix.lower() in ('.h','.hpp','.c','.cpp','.inc','.inl'):
            name=file.relative_to(source);target=build_source/name
            target.parent.mkdir(parents=True,exist_ok=True)
            data=file.read_bytes();inputs[name.as_posix()]=hashlib.sha256(data).hexdigest()
            target.write_text(data.decode('utf8').replace('\r\n','\n'),encoding='utf8',newline='\n')
patch = Path(__file__).resolve().parents[2]/'third_party/lmxxf/veyra-model-isolation.patch'
subprocess.run(['git','apply','--check',str(patch)],cwd=build_source,check=True)
subprocess.run(['git','apply',str(patch)],cwd=build_source,check=True)
model_source=Path(__file__).with_name('veyra-runtime-model.cpp').resolve()
output_name='LmxxfNrRuntime.dll' if a.model=='small' else 'LmxxfNrRuntime-Original.dll'
script = a.tmp/'lmxxf-build.cmd'
script.write_text('@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\ncd /d "%LMXXF_TASK_BUILD%"\ncl /nologo /std:c++17 /utf-8 /EHsc /O2 /LD /MD /DVEYRA_LMXXF_MODEL_MODE=%LMXXF_TASK_MODEL% /FI"%LMXXF_TASK_COMPAT%" /DNOMINMAX /D_WIN32_WINNT=0x0A00 /DLMXXF_NR_RUNTIME_EXPORTS /I"%LMXXF_TASK_SOURCE%\\include" /I"%LMXXF_TASK_SOURCE%\\src" /I"%LMXXF_TASK_SOURCE%\\Development\\HIP" "%LMXXF_TASK_SOURCE%\\src\\LmxxfNrRuntime.cpp" \"%LMXXF_TASK_MODEL_SOURCE%\" /Fe:%LMXXF_TASK_OUTPUT% /link d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib user32.lib\nexit /b %errorlevel%\n',encoding='ascii')
env=os.environ.copy()
env.update(LMXXF_TASK_SOURCE=str(build_source.resolve()),LMXXF_TASK_MODEL_SOURCE=str(model_source),LMXXF_TASK_MODEL="0" if a.model=="small" else "1",LMXXF_TASK_OUTPUT=output_name,LMXXF_TASK_BUILD=str(a.build.resolve()),
    LMXXF_TASK_COMPAT=str(Path(__file__).with_name('msvc-compat.h').resolve()),
    TEMP=str(a.tmp.resolve()),TMP=str(a.tmp.resolve()),VSLANG='1033')
with a.log.open('xb') as log:
    code=subprocess.run(['cmd.exe','/d','/c',str(script.resolve())],cwd=source,env=env,
        stdout=log,stderr=subprocess.STDOUT,timeout=290).returncode
receipt={'passed':code==0,'sourceCommit':PIN,'model':a.model,'command':['cmd.exe','/d','/c',str(script.resolve())],'sourceInputs':inputs,'patchSha256':hashlib.sha256(patch.read_bytes()).hexdigest(),'compatSha256':hashlib.sha256(Path(__file__).with_name('msvc-compat.h').read_bytes()).hexdigest(),'modelSourceSha256':hashlib.sha256(model_source.read_bytes()).hexdigest(),'log':str(a.log)}
if code==0:
    dll=a.build/output_name;receipt.update(path=str(dll),bytes=dll.stat().st_size,sha256=hashlib.sha256(dll.read_bytes()).hexdigest())
a.log.with_suffix('.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
print('LMXXF BUILD',code,a.model,a.log)
raise SystemExit(code)
