"""Build the source-only Remote Play dependency asset, separate from the portable ZIP."""
import argparse, hashlib, io, json, subprocess, tempfile, zipfile
from contextlib import contextmanager
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--root',type=Path,default=Path('.'))
p.add_argument('--output',type=Path,required=True)
p.add_argument('--mapper-source',type=Path,required=True,help='Pinned Stickmouse checkout used by the build')
p.add_argument('--chiaki-source',type=Path,default=Path('C:/veyra-deps/chiaki-source'))
p.add_argument('--vcpkg',type=Path,default=Path('C:/veyra-deps/vcpkg'))
p.add_argument('--prefix',type=Path,default=Path('C:/veyra-deps/remoteplay-installed/x64-windows-static'))
p.add_argument('--fidelityfx-source',type=Path)
p.add_argument('--version',required=True)
a=p.parse_args(); root=a.root.resolve()
clean=a.chiaki_source.resolve(); vcpkg=a.vcpkg.resolve()
prefix=a.prefix.resolve()
def git(path,*args): return subprocess.check_output(['git','-C',str(path),*args]).decode().strip()
pin='0e16950165f06e5c3291537c2eeba6e852be7120'
if git(clean,'rev-parse','HEAD')!=pin or git(clean,'status','--porcelain','--untracked-files=no'): raise RuntimeError('Chiaki pin/clean mismatch')
mapper_pin='826547879659100e5e3b3e4c975f205d15cf66fb'
if git(a.mapper_source,'rev-parse','HEAD')!=mapper_pin or git(a.mapper_source,'status','--porcelain','--untracked-files=no'):
 raise RuntimeError('Stickmouse Mapper pin/clean mismatch')
if a.output.exists(): raise RuntimeError('Output exists')
a.output.parent.mkdir(parents=True,exist_ok=True)
records=[]; excluded=[]
bad={'.dll','.exe','.lib','.pdb','.obj','.o','.a','.so','.dylib','.onnx','.pth','.pt','.zip','.7z','.mp4','.log','.addon64'}
def put_bytes(z,name,data):
 z.writestr(name,data)
 records.append(dict(path=name,size=len(data),sha256=hashlib.sha256(data).hexdigest()))

def put(z,path,name):
 if path.suffix.lower() in bad or '.git' in path.parts:
  excluded.append(name);return
 if path.is_symlink(): raise RuntimeError('Source symlink needs explicit review: '+str(path))
 put_bytes(z,name,path.read_bytes())
def tree(z,folder,name):
 for path in sorted(folder.rglob('*')):
  if path.is_file(): put(z,path,name+'/'+path.relative_to(folder).as_posix())
@contextmanager
def new_archive(path):
 with tempfile.TemporaryDirectory(dir=path.parent) as folder:
  pending=Path(folder)/path.name
  with zipfile.ZipFile(pending,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
   yield archive
  with zipfile.ZipFile(pending) as archive:
   if corrupt:=archive.testzip(): raise ValueError('Invalid archive entry: '+corrupt)
  if path.exists(): raise FileExistsError(path)
  pending.rename(path)

with new_archive(a.output) as z:
 modules=git(clean,'submodule','status','--recursive')
 z.writestr('chiaki-submodules.txt',modules+'\n')
 repos=[clean]
 for line in modules.splitlines():
  fields=line.split(); repos.append(clean/fields[1])
 for repo in repos:
  rel=repo.relative_to(clean).as_posix()
  for name in git(repo,'ls-files').splitlines():
   path=repo/name
   if path.is_file(): put(z,path,'chiaki-clean/'+(('' if rel=='.' else rel+'/'))+name)
 # Use the pinned blobs so Windows checkout line endings do not change the offline source manifest.
 snapshot=subprocess.check_output(['git','-C',str(a.mapper_source),'-c','core.autocrlf=false','archive','--format=zip',mapper_pin])
 with zipfile.ZipFile(io.BytesIO(snapshot)) as mapper:
  for entry in mapper.infolist():
   if not entry.is_dir(): put_bytes(z,'stickmouse-mapper/'+entry.filename,mapper.read(entry))
 z.writestr('stickmouse-mapper-revision.txt',mapper_pin+'\n')
 tree(z,root/'scripts/remoteplay','veyra/scripts/remoteplay')
 build_guide=f'docs/BUILD_{a.version}.md'
 for name in ['cmake/VeyraStickmouse.cmake','cmake/VeyraRemotePlay.cmake','scripts/build.ps1','CMakePresets.json',build_guide,'docs/DEVELOPMENT.md','docs/PS5_KEYBOARD_MOUSE.md','THIRD_PARTY_NOTICES.md']:
  put(z,root/name,'veyra/'+name)
 tree(z,root/'licenses/remoteplay','licenses/remoteplay')
 for dep in ['json-c','libevent','miniupnpc','openssl','opus','sdl3']:
  spdx=json.loads((prefix/'share'/dep/'vcpkg.spdx.json').read_text())
  for item in spdx['files']:
   if item['SPDXID'].startswith('SPDXRef-port-file-'):
    path=vcpkg/'ports'/dep/item['fileName']
    wanted=next(c['checksumValue'] for c in item['checksums'] if c['algorithm']=='SHA256')
    if hashlib.sha256(path.read_bytes()).hexdigest()!=wanted.lower(): raise RuntimeError('Port mismatch: '+str(path))
  sources=list((vcpkg/'buildtrees'/dep/'src').glob('*.clean'))
  if len(sources)!=1:raise RuntimeError('Ambiguous source: '+dep)
  tree(z,sources[0],'dependencies/'+dep+'/source')
  tree(z,vcpkg/'ports'/dep,'dependencies/'+dep+'/port')
  tree(z,prefix/'share'/dep,'dependencies/'+dep+'/installed-share')
 for dep in ['vcpkg-cmake','vcpkg-cmake-config','vcpkg-cmake-get-vars']:
  tree(z,vcpkg/'ports'/dep,'vcpkg-port-scripts/ports/'+dep)
 tree(z,vcpkg/'scripts/cmake','vcpkg-port-scripts/scripts/cmake')
 for name in ['scripts/buildsystems/vcpkg.cmake','triplets/x64-windows-static.cmake','LICENSE.txt']:
  put(z,vcpkg/name,'vcpkg-port-scripts/'+name)
 put(z,prefix.parent/'vcpkg/status','installed-status.txt')
 ff=a.fidelityfx_source or root/'third_party_local/amd/FidelityFX-SDK'
 # Open-source FidelityFX source only; never NVIDIA/Intel SDK directories.
 if Path(git(ff,'rev-parse','--show-toplevel')).resolve()!=ff.resolve():
  raise RuntimeError('FidelityFX source must be its own checkout: '+str(ff))
 for name in git(ff,'ls-files').splitlines():
  path=ff/name
  if path.is_file():put(z,path,'fidelityfx/'+name)
 revision=git(root,'rev-parse','HEAD')
 z.writestr('README.txt',f'Veyra {a.version} corresponding dependency source. Application source commit {revision}. See veyra/{build_guide} and veyra/docs/PS5_KEYBOARD_MOUSE.md. FFmpeg source is a separate asset.\n')
 z.writestr('source-manifest.json',json.dumps(records,indent=2))
 z.writestr('excluded-development-binaries.json',json.dumps(excluded,indent=2))
digest=hashlib.sha256(a.output.read_bytes()).hexdigest().upper()
a.output.with_suffix('.zip.sha256').write_text(digest+'  '+a.output.name+'\n',encoding='ascii')
print(json.dumps(dict(archive=str(a.output),files=len(records),excluded=len(excluded),sha256=digest)))
