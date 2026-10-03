"""Build the pinned MIT runtime from its original checkout; no injection module."""
import argparse, os, re, subprocess
from pathlib import Path

PIN = '78f548749e74824327b8458c57be31a1df78376a'
p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True)
p.add_argument('--build', type=Path, required=True)
p.add_argument('--tmp', type=Path, required=True)
p.add_argument('--log', type=Path, required=True)
a = p.parse_args()
source = a.source.resolve()
head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()
if head != PIN or subprocess.check_output(['git','diff','--name-only','HEAD'],cwd=source):
    raise SystemExit('Expected an unchanged pinned upstream checkout')
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
script = a.tmp/'lmxxf-build.cmd'
script.write_text('@echo off\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul 2>&1\nif errorlevel 1 exit /b 4\ncd /d "%LMXXF_TASK_BUILD%"\ncl /nologo /std:c++17 /EHsc /O2 /LD /MD /FI"%LMXXF_TASK_COMPAT%" /DNOMINMAX /D_WIN32_WINNT=0x0A00 /DLMXXF_NR_RUNTIME_EXPORTS /I"%LMXXF_TASK_SOURCE%\\include" /I"%LMXXF_TASK_SOURCE%\\src" /I"%LMXXF_TASK_SOURCE%\\Development\\HIP" "%LMXXF_TASK_SOURCE%\\src\\LmxxfNrRuntime.cpp" /Fe:LmxxfNrRuntime.dll /link d3d12.lib dxgi.lib d3dcompiler.lib dxguid.lib user32.lib\nexit /b %errorlevel%\n',encoding='ascii')
env=os.environ.copy()
env.update(LMXXF_TASK_SOURCE=str(source),LMXXF_TASK_BUILD=str(a.build.resolve()),
    LMXXF_TASK_COMPAT=str(Path(__file__).with_name('msvc-compat.h').resolve()),
    TEMP=str(a.tmp.resolve()),TMP=str(a.tmp.resolve()),VSLANG='1033')
with a.log.open('xb') as log:
    code=subprocess.run(['cmd.exe','/d','/c',str(script.resolve())],cwd=source,env=env,
        stdout=log,stderr=subprocess.STDOUT,timeout=290).returncode
print('LMXXF BUILD',code,a.log)
raise SystemExit(code)
