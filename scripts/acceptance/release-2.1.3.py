"""Promote the explicitly approved Claude 2.1.3 ZIPs without changing product bytes.

Run from a clean publication commit. Does not push, upload, publish or alter donors.
All new archives and receipts stay under the E: project artifact root.
"""
from pathlib import Path
import hashlib, importlib.util, json, re, shutil, stat, subprocess, zipfile

R = Path(__file__).resolve().parents[2]
B = Path('E:/项目/Veyra')
T = 'codex-release-2.1.3-20261106'
O = B/'releases'/T
REPORT = B/'reports'/T
D = B/'test-packages/codex-rebuild-2.1.3-20261106'
PRODUCT = '7a2d15d9222a98fac69e5343c01b443e1d7060be'
EXE = '76c4a44ee582dcd4150152c4e32e5e45f61fee1a35a285818dc7ee1464940f7a'
DONORS = {'NVIDIA': 'a9e251f006d42bafa9e0d62bc5936a9d781798e2187f9b72f7c1b082d9cdb0cd',
          'AMD': '9d0491c66e6035ba9a76d9153b5974a5f4edd83e0e72ce566ec3f4392498b71f'}
NETWORK = '0b4657f1340ed4775e702a9eda27cbc558a49ee5d4a2ed25bbecdd2447d3eb48'
WORKER = 'runtime/amd-nr-rdna3/veyra_amd_nr_worker.exe'
STUB = 'runtime/amd-nr-rdna3/zluda/nvcuda.dll'
STUB_SHA = 'b5f118a8f08b3789319cc61e151e1c84ff0f85dc2620a09bd51e7d877fc1ebbe'
BASE = 'https://github.com/Likely7/Veyra-NRVideo/releases/download/'
spec = importlib.util.spec_from_file_location('package_qml', R/'scripts/package-qml-release.py')
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)

def sha(p):
    with p.open('rb') as f: return hashlib.file_digest(f, 'sha256').hexdigest()

def encoded(v): return (json.dumps(v, ensure_ascii=False, indent=2)+'\n').encode('utf-8')

def git(*args):
    return subprocess.check_output(['git', '-C', str(R), *args]).decode('utf-8').strip()

def validate(n):
    if n == WORKER: pkg.relative(n)
    else: pkg.validate_payload(n)

def verify(p, expected=None):
    if expected: assert sha(p) == expected, p
    with zipfile.ZipFile(p) as z:
        ns = [i.filename for i in z.infolist() if not i.is_dir()]
        assert len(ns) == len(set(n.lower() for n in ns)), 'Duplicate path'
        for i in z.infolist():
            pkg.relative(i.filename.rstrip('/'))
            assert not stat.S_ISLNK(i.external_attr >> 16)
        ks = [n for n in ns if n.count('/') == 1 and n.endswith('/package-manifest.json')]
        assert len(ks) == 1
        key = ks[0]; prefix = key.split('/')[0]+'/'
        m = json.loads(z.read(key)); paths = {r['path'] for r in m['files']}
        assert len(paths) == len(m['files'])
        assert set(ns) == {prefix+n for n in paths}|{key}
        assert pkg.REQUIRED <= paths
        assert m['productSourceCommit'] == PRODUCT and m['exeSha256'] == EXE
        for row in m['files']:
            n = row['path']; validate(n)
            info = z.getinfo(prefix+n)
            with z.open(info) as f: actual = hashlib.file_digest(f, 'sha256').hexdigest()
            assert info.file_size == row['size'] and actual == row['sha256'].lower(), n
            if Path(n).name.lower() in {'nvcuda.dll','nvopticalflow64.dll','amdhip64.dll','amdhip64_6.dll','amdhip64_7.dll'}:
                assert m['gpuPackage'] == 'AMD' and n == STUB and actual == STUB_SHA
        runtime = json.loads(z.read(prefix+'release-runtime-manifest.json'))
        assert runtime == json.loads(z.read(prefix+'runtime/experimental/release-runtime-manifest.json'))
        assert runtime['enforcedAtRuntime'] is False
        rows = {r['path']: r for r in m['files']}
        assert rows['veyra_qml_ui.exe']['sha256'] == EXE
        for n, expected_model in pkg.MODELS.items(): assert rows[n]['sha256'] == expected_model
        for row in runtime['files']:
            assert rows[row['path']]['sha256'].lower() == row['sha256'].lower()
            assert rows[row['path']]['size'] == row['size']
        if m['gpuPackage'] == 'AMD':
            rd = json.loads(z.read(prefix+'runtime/amd-nr-rdna3/veyra-rdna3-runtime-manifest.json'))
            for row in rd['files']:
                assert rows['runtime/amd-nr-rdna3/'+row['path']]['sha256'] == row['sha256']
        return m

def release_flags(value):
    if isinstance(value, dict):
        return {k: (False if k == 'localOnly' else True if k == 'releaseAuthorized' else release_flags(v))
                for k, v in value.items() if k != 'published'}
    if isinstance(value, list): return [release_flags(v) for v in value]
    return value

def main():
    assert not git('status','--porcelain'), 'Commit publication inputs first'
    assert not git('diff', PRODUCT, '--', 'src','include','apps','qml','i18n','shaders','CMakeLists.txt')
    O.mkdir(parents=True, exist_ok=True); REPORT.mkdir(parents=True, exist_ok=True)
    head = git('rev-parse','HEAD')
    source = O/'Veyra-2.1.3-source.zip'; assert not source.exists()
    subprocess.run(['git','-C',str(R),'archive','--format=zip','--prefix=Veyra-2.1.3-source/',
                    '--output='+str(source),head], check=True, timeout=120)
    with zipfile.ZipFile(source) as z:
        assert z.testzip() is None
        for n in z.namelist():
            assert not re.search(r'\.(dll|exe|lib|pdb|obj|onnx|pth|ptx|addon64)$|(^|/)(runtime_local|third_party_local|\.git)/',n,re.I), n
    network = O/'Veyra-2.1.3-rdna3-network-source.zip'
    assert sha(D/network.name) == NETWORK
    shutil.copyfile(D/network.name,network)
    results = []
    for flavor, expected in DONORS.items():
        name = f'Veyra-2.1.3-{flavor}-win64-portable'
        donor = D/(name+'.zip'); old = verify(donor,expected)
        print('DONOR VERIFIED',flavor,flush=True)
        replacements = {}
        for n in ('README.md','README_CN.md','README_EN.md','LICENSE','THIRD_PARTY_NOTICES.md',
                  'docs/BUILD_2.1.3.md','docs/RELEASE_NOTES_2.1.3.md','docs/RELEASE_SUPPORT.md',
                  'docs/images/community/group-8-20261102.png','docs/images/2.1.3/sr-comparison.png'):
            replacements[n] = (R/n).read_bytes()
        replacements['RELEASE_NOTES.md'] = (R/'docs/RELEASE_NOTES_2.1.3.md').read_bytes()
        replacements['README-2.1.3.zh-CN.md'] = replacements['RELEASE_NOTES.md']
        replacements['GPU-PACKAGE.txt'] = (
            f'Veyra 2.1.3 / {flavor} / PUBLIC RELEASE\n'
            'Extract into a new writable folder and run veyra_qml_ui.exe.\n'
            'Settings migration: see RELEASE_NOTES.md. No user profile is included.\n'
            'Community/experimental runtimes retain their original identities and licences.\n'
            'AMD RX7000 RedDuke / RX9000 lmxxf require installed HIP7 driver components.\n'
        ).encode()
        with zipfile.ZipFile(donor) as zin:
            prefix = name+'/'
            rdname = 'runtime/amd-nr-rdna3/veyra-rdna3-runtime-manifest.json'
            if flavor == 'AMD': replacements[rdname] = encoded(release_flags(json.loads(zin.read(prefix+rdname))))
            runtime = release_flags(json.loads(zin.read(prefix+'release-runtime-manifest.json')))
            runtime.update(intendedRelease='v2.1.3', authorization='User requested GitHub publication on 2026-11-06')
            for row in runtime['files']:
                if row['path'] in replacements:
                    data = replacements[row['path']]
                    row.update(size=len(data),sha256=hashlib.sha256(data).hexdigest())
            for n in ('release-runtime-manifest.json','runtime/experimental/release-runtime-manifest.json'):
                replacements[n] = encoded(runtime)
            m = release_flags(old)
            m.pop('published',None)
            m.update(releaseReady=True,releaseAuthorized=True,intendedRelease='v2.1.3',
                     publicationStatus='See GitHub Release page',publicationSourceCommit=head,
                     mainCommit=head,donorArchiveSha256=expected)
            m['correspondingSource'] = dict(application=BASE+'v2.1.3-sources/'+source.name,
                applicationSha256=sha(source),amdNetwork=BASE+'v2.1.3-sources/'+network.name if flavor=='AMD' else None,
                amdNetworkSha256=NETWORK if flavor=='AMD' else None,unchangedDependencies='docs/BUILD_2.1.3.md')
            rows = {r['path']: dict(r) for r in old['files']}
            for n, data in replacements.items():
                validate(n); rows[n] = dict(path=n,size=len(data),sha256=hashlib.sha256(data).hexdigest())
            m['files'] = [rows[n] for n in sorted(rows)]
            out = O/(name+'.zip'); assert not out.exists()
            with zipfile.ZipFile(out,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as zout:
                for row in m['files']:
                    n = row['path']
                    if n in replacements: zout.writestr(prefix+n,replacements[n])
                    else:
                        with zin.open(prefix+n) as src, zout.open(prefix+n,'w',force_zip64=True) as dst:
                            shutil.copyfileobj(src,dst,1024*1024)
                zout.writestr(prefix+'package-manifest.json',encoded(m))
        new = verify(out)
        changed = [r['path'] for r in old['files'] if rows[r['path']]['sha256'] != r['sha256']]
        assert all(n in replacements for n in changed)
        assert not any(n.startswith(('qml/','i18n/','shaders/')) or Path(n).suffix.lower() in {'.exe','.dll','.dxil','.onnx','.tflite','.bin','.cubin','.hsaco','.ptx'} for n in changed)
        results.append(dict(flavor=flavor,path=str(out),bytes=out.stat().st_size,sha256=sha(out),
                            files=len(new['files']),changed=changed,productBytesPreserved=True,donorSha256=expected))
        print('RELEASE ZIP VERIFIED',flavor,len(new['files']),flush=True)
    assets = [source,network]+[Path(x['path']) for x in results]
    for p in assets: Path(str(p)+'.sha256').write_text(sha(p)+'  '+p.name+'\n',encoding='ascii')
    (O/'SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in assets),encoding='ascii')
    result = dict(status='LOCAL_RELEASE_ARCHIVES_VERIFIED_NOT_YET_PUBLISHED',version='2.1.3',
        publicationSourceCommit=head,productSourceCommit=PRODUCT,exeSha256=EXE,
        source=dict(path=str(source),sha256=sha(source)),networkSource=dict(path=str(network),sha256=sha(network)),
        packages=results,sourceScan='no proprietary binaries, SDK or user profiles',
        originalPackagesUntouched=True,productDiff='empty',published=False)
    (REPORT/'PACKAGING.json').write_bytes(encoded(result))
    print(json.dumps(result,ensure_ascii=False,indent=2),flush=True)

if __name__ == '__main__': main()
