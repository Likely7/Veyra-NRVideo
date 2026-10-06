"""Preserve the approved dependency sources and add actual subtitle source inputs."""
from pathlib import Path
import hashlib,json,subprocess,zipfile,tarfile
ROOT=Path(__file__).resolve().parents[1];BASE=Path('E:/项目/Veyra');TASK='publish-2.0.4-20261006'
OUT=BASE/'releases'/TASK;OLD=BASE/'releases/release-2.0.3-20261004/Veyra-2.0.3-dependency-source.zip'
DOWNLOAD=BASE/'downloads/vcpkg-libass-20261005';VCPKG=Path('C:/veyra-deps/vcpkg');INSTALLED=Path('E:/veyra-ascii-20261005/installed/x64-windows-static')
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def git(*a):return subprocess.check_output(['git','-C',str(VCPKG),*a],stderr=subprocess.PIPE).decode('utf8').strip()
assert sha(OLD)=='1a04192bd021a610f08cee7ee0f17093a621bd263fde22c39a6a4a1ac703420c'
assert git('rev-parse','HEAD')=='30ef65cad98f08e7197c9a1656fbd871bcb72f2d' and not git('status','--porcelain')
sources=['libass-libass-0.17.5.tar.gz','freetype-freetype-VER-2-14-3.tar.gz','fribidi-fribidi-v1.0.16.tar.gz','harfbuzz-harfbuzz-14.4.0.tar.gz',
         'pnggroup-libpng-v1.6.58.tar.gz','madler-zlib-v1.3.2.tar.gz','google-brotli-v1.2.0.tar.gz','bzip2-1.0.8.tar.gz']
ports=['libass','freetype','fribidi','harfbuzz','libpng','zlib','brotli','bzip2','vcpkg-cmake','vcpkg-cmake-config','vcpkg-cmake-get-vars','vcpkg-tool-meson']
target=OUT/'Veyra-2.0.4-dependency-source.zip';assert not target.exists();records=[]
def put(z,name,data):
    z.writestr(name,data);records.append(dict(path=name,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
with zipfile.ZipFile(target,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    z.write(OLD,'unchanged/'+OLD.name,compress_type=zipfile.ZIP_STORED)
    records.append(dict(path='unchanged/'+OLD.name,size=OLD.stat().st_size,sha256=sha(OLD)))
    for name in sources:
        p=DOWNLOAD/name
        with tarfile.open(p,'r:gz') as t:
            members=t.getmembers();assert len(members)>10
            assert all(not Path(m.name).is_absolute() and '..' not in Path(m.name).parts for m in members)
        z.write(p,'subtitle-sources/upstream/'+name,compress_type=zipfile.ZIP_STORED)
        records.append(dict(path='subtitle-sources/upstream/'+name,size=p.stat().st_size,sha256=sha(p)))
    paths=git('ls-files','-z',*(f'ports/{p}' for p in ports),'triplets/x64-windows-static.cmake','scripts').split('\0')
    for rel in sorted(filter(None,paths)):
        p=VCPKG/rel
        if p.suffix.lower() in ('.dll','.exe','.lib','.pdb','.onnx','.bin','.obj','.o','.a','.so','.zip','.7z','.gz','.nupkg','.png','.jpg','.pdf') or '/test' in rel:continue
        put(z,'subtitle-sources/vcpkg/'+rel,p.read_bytes())
    for port in ports[:8]:
        for name in ('copyright','vcpkg.spdx.json','vcpkg_abi_info.txt'):
            p=INSTALLED/'share'/port/name
            if p.is_file():put(z,'subtitle-sources/installed-metadata/'+port+'/'+name,p.read_bytes())
    put(z,'subtitle-sources/installed-metadata/status.txt',(INSTALLED.parent/'vcpkg/status').read_bytes())
    put(z,'subtitle-sources/VCPKG_COMMIT.txt',(git('rev-parse','HEAD')+'\n').encode())
    libs={p.name:sha(p) for p in (INSTALLED/'lib').glob('*.lib')}
    put(z,'subtitle-sources/linked-library-provenance.json',json.dumps(libs,indent=2).encode())
    put(z,'BUILD_2.0.4.md',(ROOT/'docs/BUILD_2.0.4.md').read_bytes())
    put(z,'README.txt',b'Veyra2.0.4 corresponding open-source dependencies. Extract unchanged/Veyra-2.0.3-dependency-source.zip, then its nested 2.0.0 dependency source for Qt6.8.3, patched FFmpeg/dav1d, Chiaki, Moonlight, Xbox/WebRTC, OpenSSL, FidelityFX2.3.0 and existing MIT source. subtitle-sources contains the actual upstream libass/font/compression source archives, pinned vcpkg port recipes/patches/scripts and installed metadata. Rebuild/relink the tagged Veyra application with modified static libraries if desired. Preserve licenses and the PS5 slice patch. Proprietary NVIDIA SDKs, runtimes and model weights are excluded.\n')
    z.writestr('source-manifest.json',json.dumps(records,ensure_ascii=False,indent=2))
with zipfile.ZipFile(target) as z:
    assert z.testzip() is None
    for row in records:
        with z.open(row['path']) as f:assert hashlib.file_digest(f,'sha256').hexdigest()==row['sha256'],row['path']
result=dict(path=str(target),bytes=target.stat().st_size,sha256=sha(target),files=len(records),upstreamSources=len(sources),passed=True)
(BASE/'logs'/TASK/'dependency-source.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
print(json.dumps(result),flush=True)
