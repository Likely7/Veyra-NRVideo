"""Local release source material: approved legacy sources plus actual new dependencies.
Never includes proprietary SDKs, runtime DLLs or user profiles.
"""
from pathlib import Path
import hashlib, json, subprocess, zipfile

ROOT = Path(__file__).resolve().parents[1]
BASE = Path('E:/项目/Veyra')
OUT = BASE / 'releases/2.0.0-final-20261002'
OUT.mkdir(parents=True, exist_ok=True)
target = OUT / 'Veyra-2.0.0-dependency-source.zip'
records = []
bad = {'.dll','.exe','.lib','.pdb','.obj','.o','.a','.so','.dylib','.onnx','.pth','.pt','.log','.dmp','.mp4','.mkv','.addon64'}
def digest(data): return hashlib.sha256(data).hexdigest()
def git(p,*args): return subprocess.check_output(['git','-C',str(p),*args]).decode().strip()
def put(z,name,data):
    z.writestr(name,data)
    records.append(dict(path=name,size=len(data),sha256=digest(data)))
def tree(z,p,prefix):
    for f in sorted(p.rglob('*')):
        if f.is_file() and '.git' not in f.parts and f.suffix.lower() not in bad:
            put(z,prefix+'/'+f.relative_to(p).as_posix(),f.read_bytes())
def repo(z,p,prefix):
    put(z,prefix+'/SOURCE_COMMIT.txt',(git(p,'rev-parse','HEAD')+'\n'+git(p,'status','--porcelain','--untracked-files=no')).encode())
    names=git(p,'ls-files','--recurse-submodules','-z').split('\0')
    for n in names:
        f=p/n
        if f.is_file() and f.suffix.lower() not in bad:
            put(z,prefix+'/'+n,f.read_bytes())

with zipfile.ZipFile(target,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    old=BASE/'releases/1.4.4/final/Veyra-1.4.4-source.zip'
    assert digest(old.read_bytes())=='7d3a8efed76422eab536d714221f579f1478921b927149fbb5adffa8635c53ba'
    with zipfile.ZipFile(old) as src:
        expected={'Veyra-1.4.1-FFmpeg-source.zip':'15a77217bedceb4280fc680d5c0761544170356ca85e871f1558b61c86ee5f02',
                  'Veyra-1.4.1-RemotePlay-source.zip':'272e41bc128c9bd86d7a9a93e4f0150dd92db6b9d00909643336b9e01561f166'}
        for name,sha in expected.items():
            data=src.read('Veyra-1.4.4-source/dependencies/'+name);assert digest(data)==sha
            put(z,'legacy-unchanged/'+name,data)
    repo(z,BASE/'deps/moonlight/moonlight-common-c','moonlight-common-c')
    vcpkg=Path('C:/veyra-deps/vcpkg')
    for dep in ['libdatachannel','libjuice','usrsctp','libsrtp','plog','nlohmann-json','openssl']:
        sources=list((vcpkg/'buildtrees'/dep/'src').glob('*.clean'))
        assert sources,dep
        # Include both audited libjuice source variants, with the build recipe
        # selecting the patched source via its exact patch; no ambiguity hidden.
        for source in sources: tree(z,source,'xbox/'+dep+'/'+source.name)
        tree(z,vcpkg/'ports'/dep,'xbox/'+dep+'/port')
        share=Path('C:/veyra-deps/xbox-installed/x64-windows-static/share')/dep
        if share.exists():tree(z,share,'xbox/'+dep+'/installed-share')
    tree(z,ROOT/'scripts/xbox','veyra/scripts/xbox')
    tree(z,ROOT/'scripts/moonlight','veyra/scripts/moonlight')
    tree(z,ROOT/'scripts/remoteplay','veyra/scripts/remoteplay')
    put(z,'xbox/installed-status.txt',Path('C:/veyra-deps/xbox-installed/vcpkg/status').read_bytes())
    tree(z,vcpkg/'scripts','vcpkg/scripts')
    tree(z,vcpkg/'triplets','vcpkg/triplets')
    put(z,'vcpkg/COMMIT.txt',git(vcpkg,'rev-parse','HEAD').encode())
    repo(z,Path('C:/Users/123/Desktop/Veyra DLSS Video Player/third_party_local/amd/FidelityFX-SDK-2.3.0'),'fidelityfx-2.3.0')
    qt=BASE/'downloads/qt-source-6.8.3'
    for item in json.loads((qt/'manifest.json').read_text()):
        data=(qt/item['name']).read_bytes();assert digest(data)==item['sha256']
        put(z,'qt/'+item['name'],data)
    put(z,'qt/download-manifest.json',(qt/'manifest.json').read_bytes())
    tree(z,BASE/'deps/qt/6.8.3/msvc2022_64/sbom','qt/binary-sbom')
    for name in ['BUILD_2.0.0.md','RUNTIME_COMPONENTS_2.0.0.md']:
        put(z,'veyra/docs/'+name,(ROOT/'docs'/name).read_bytes())
    put(z,'README.txt',b'Veyra 2.0.0 corresponding open-source dependencies. Legacy archive names identify unchanged source provenance, not the application version. Current Moonlight, Xbox (including libjuice patch and both source variants), Qt 6.8.3 and FidelityFX 2.3.0 are added. Build instructions and patches are included; proprietary SDKs must be obtained separately under their licences. See the application source archive/tag for Veyra and its build integration. No claim that source-only packaging constitutes an independent legal audit.\n')
    z.writestr('source-manifest.json',json.dumps(records,indent=2))
with zipfile.ZipFile(target) as z:
    for r in records:
        with z.open(r['path']) as f: assert hashlib.file_digest(f,'sha256').hexdigest()==r['sha256']
sha=hashlib.file_digest(target.open('rb'),'sha256').hexdigest()
target.with_suffix('.zip.sha256').write_text(sha+'  '+target.name+'\n')
print(json.dumps(dict(path=str(target),files=len(records),sha256=sha,bytes=target.stat().st_size)))
