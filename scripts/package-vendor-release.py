"""Split a freshly assembled QML product by usable GPU runtime, never by brand name.

Publisher identities are checked while packaging. The application still permits
user-supplied replacement runtimes. SDKs, user data and driver DLLs are excluded.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = Path('E:/项目/Veyra')
spec = importlib.util.spec_from_file_location('qml_package', ROOT / 'scripts/package-qml-release.py')
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)
VFG_KEEP = {'cudart64_12.dll', 'NVCVImage.dll', 'nvngxruntime.dll', 'NVVideoEffects.dll', 'nvVFXVideoFrameGeneration.dll'}
AMD_RUNTIME_SHA = '2693cdb760db564ec6f34b39b8a7e554d35cbab5df2366bff030e1611756e3d8'


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def record(path, name):
    return dict(path=name, size=path.stat().st_size, sha256=pkg.digest(path))


def checked_copy(source, stage, name, identity=None):
    pkg.validate_payload(name)
    if identity and (source.stat().st_size != identity['size'] or pkg.digest(source) != identity['sha256'].lower()):
        raise ValueError('Publisher identity mismatch: ' + str(source))
    target = stage / name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)


def keep_common(name, flavor):
    if flavor == 'NVIDIA':
        return not name.startswith('runtime/amd-nr/')
    return not name.startswith(('runtime/experimental/nvngx', 'runtime/experimental/nr-',
                                'runtime/experimental/fallback-dlssg', 'runtime/experimental/dlssg-kernels/',
                                'runtime/nvidia-vfg/', 'runtime/config/', 'licenses/nvidia-vfg/',
                                'licenses/vfg-samples/'))


def check_vendor(stage, flavor):
    names = {f.relative_to(stage).as_posix() for f in stage.rglob('*') if f.is_file()}
    if flavor == 'NVIDIA':
        assert not any(n.startswith('runtime/amd-nr/') for n in names)
        assert {Path(n).name for n in names if n.startswith('runtime_local/amd/fidelityfx/')} == {'amd_fidelityfx_loader_dx12.dll', 'amd_fidelityfx_upscaler_dx12.dll', 'amd_fidelityfx_framegeneration_dx12.dll'}
        assert {Path(n).name for n in names if n.startswith('runtime/nvidia-vfg/')} == VFG_KEEP
    else:
        assert not any(n.startswith(('runtime/nvidia-vfg/', 'runtime/experimental/nvngx', 'runtime/experimental/nr-',
                                     'runtime/experimental/fallback-dlssg', 'runtime/experimental/dlssg-kernels/')) for n in names)
        assert 'runtime/amd-nr/LmxxfNrRuntime.dll' in names
        assert 'runtime_local/amd/fidelityfx/amd_fidelityfx_framegeneration_dx12.dll' in names
    assert not any(Path(n).name.lower() in {'nvcuda.dll', 'nvopticalflow64.dll', 'amdhip64.dll', 'amdhip64_6.dll', 'amdhip64_7.dll'} for n in names)
    manifest = json.loads((stage / 'release-runtime-manifest.json').read_text(encoding='utf8'))
    for row in manifest['files']:
        f = stage / row['path']
        assert f.stat().st_size == row['size'] and pkg.digest(f) == row['sha256'].lower(), row['path']
    assert json.loads((stage / 'runtime/experimental/release-runtime-manifest.json').read_text(encoding='utf8')) == manifest
    # Every distributed runtime (including HIP code objects and NR weights) has
    # a source/identity row; nothing is silently introduced by a copytree.
    runtime_names = {n for n in names if n.startswith(('runtime/', 'runtime_local/')) and
                     Path(n).suffix.lower() not in {'.json', '.txt', '.md'}}
    assert runtime_names <= {r['path'] for r in manifest['files']}, runtime_names - {r['path'] for r in manifest['files']}
    assert pkg.REQUIRED <= names, pkg.REQUIRED - names
    return names


def signature_audit(paths):
    env = {k: v for k, v in os.environ.items() if k.upper() != 'PSMODULEPATH'}
    env['VEYRA_VENDOR_AUDIT'] = json.dumps([str(p) for p in paths])
    script = r'''$ErrorActionPreference='Stop'; $rows=foreach($p in ($env:VEYRA_VENDOR_AUDIT | ConvertFrom-Json)) { $s=Get-AuthenticodeSignature -LiteralPath $p; $v=(Get-Item -LiteralPath $p).VersionInfo; [pscustomobject]@{name=[IO.Path]::GetFileName($p);version="$($v.FileMajorPart).$($v.FileMinorPart).$($v.FileBuildPart).$($v.FilePrivatePart)";signature=[string]$s.Status;signer=$s.SignerCertificate.Subject} }; $rows | ConvertTo-Json -Depth 4'''
    rows = json.loads(subprocess.check_output(['powershell.exe', '-NoProfile', '-NonInteractive', '-Command', script], env=env, timeout=120).decode('utf-8-sig'))
    for row in rows:
        assert row['signature'] == 'Valid', row
    return {r['name']: r for r in rows}


def stage_packages(args):
    common = json.loads((args.common_stage / 'package-manifest.json').read_text(encoding='utf8'))
    runtimes = json.loads((args.common_stage / 'release-runtime-manifest.json').read_text(encoding='utf8'))
    vfg = json.loads((args.runtime_source / 'vfg-runtime-manifest.json').read_text(encoding='utf8'))
    assert {r['name'] for r in vfg['files']} == VFG_KEEP
    vfg_ids = signature_audit([args.runtime_source / r['path'] for r in vfg['files']])
    assert pkg.digest(args.amd_source / 'LmxxfNrRuntime.dll') == AMD_RUNTIME_SHA
    amd = json.loads((args.amd_source / 'amd-nr-local-manifest.json').read_text(encoding='utf8'))
    for flavor in ('NVIDIA', 'AMD'):
        stage = args.output / f'Veyra-{args.version}-{flavor}-win64-portable'
        stage.mkdir(parents=True, exist_ok=False)
        for row in common['files']:
            if row['path'] in {'release-runtime-manifest.json', 'runtime/experimental/release-runtime-manifest.json'}:
                continue
            if keep_common(row['path'], flavor):
                checked_copy(args.common_stage / row['path'], stage, row['path'], row)
        rows = [r for r in runtimes['files'] if keep_common(r['path'], flavor)]
        if flavor == 'NVIDIA':
            vfg.update(localOnly=False, distributionAuthorized='2026-10-04 user-authorized 2.0.3 Release',
                       identityAndLicenseIncluded=True, publicDistributionAudited=False)
            for row in vfg['files']:
                checked_copy(args.runtime_source / row['path'], stage, row['path'], row)
                identity = vfg_ids[row['name']]
                rows.append(dict(row, source='official nvidia-vfx 0.2.0.0 wheel / Video Effects SDK 1.3.0',
                                 fileVersion=identity['version'], authenticode=identity['signature'], signer=identity['signer']))
            write_json(stage / 'vfg-runtime-manifest.json', vfg)
            for folder in ('nvidia-vfg', 'vfg-samples'):
                for f in (args.runtime_source / 'licenses' / folder).rglob('*'):
                    if f.is_file(): checked_copy(f, stage, 'licenses/' + folder + '/' + f.relative_to(args.runtime_source / 'licenses' / folder).as_posix())
        else:
            amd_rows = []
            for row in amd['files']:
                source = args.amd_source / row['path']
                if row['path'] == 'LOCAL-EXPERIMENT.txt': continue
                name = 'runtime/amd-nr/' + row['path']
                checked_copy(source, stage, name, row)
                item = dict(row, path=name, name=Path(name).name,
                            source='lmxxf/dlss5-on-amd-9070xt-porting 78f548749e74824327b8458c57be31a1df78376a; user-supplied Magpie-DLSS5-AMD-0.39 assets',
                            experimental=True, removable=True, modified=False)
                if name.endswith('LmxxfNrRuntime.dll'):
                    item.update(source='Veyra-built MIT lmxxf host ABI; actual source and patches in corresponding-source bundle',
                                authenticode='NotSigned', fileVersion='0.0.0.0', modified=True)
                rows.append(item); amd_rows.append(item)
            write_json(stage / 'runtime/amd-nr/runtime-manifest.json', dict(schema=2, upstream=amd['upstream'],
                       inferenceVerified=False, hardware=['RX 9000 / gfx1200 / gfx1201'], hipRequirement='AMD driver HIP 7 in System32; not included',
                       license='LICENSE-lmxxf-MIT.txt', files=amd_rows))
        runtime_manifest = dict(schema=3, package=args.version, gpuPackage=flavor, enforcedAtRuntime=False,
                                excludedOtherVendorOnlyRuntimes=True, files=rows)
        write_json(stage / 'release-runtime-manifest.json', runtime_manifest)
        write_json(stage / 'runtime/experimental/release-runtime-manifest.json', runtime_manifest)
        text = ('NVIDIA: NR Lecram / SF-v2 / original, DLSS/RTX SR, DLSS FG, VFG 2-8X, FSR 3.1 and XeSS. No AMD NR. Shared FidelityFX 2.3.0 components remain; FSR4 ML is disabled on NVIDIA.\n'
                if flavor == 'NVIDIA' else 'AMD: lmxxf NR 0.39 (RX9000, requires HIP 7), FSR4 with FSR3.1 compatibility and XeSS. No NVIDIA DLSS/NGX/VFG/CUDA runtime. AMD inference still awaits physical RX9000 verification.\n')
        (stage / 'GPU-PACKAGE.txt').write_text('Veyra ' + args.version + ' / ' + flavor + '\n' + text +
            'Unsupported functions stay visible and disabled. Use the package matching the GPU used by Veyra. Extract with 7-Zip, then run veyra_qml_ui.exe. Model/DLL bytes are unmodified; default effects are off.\n', encoding='utf8')
        check_vendor(stage, flavor)
        manifest = dict(common, gpuPackage=flavor, candidate=args.version, localOnly=False,
                        worktreeDirty=True, releaseReady=False, sourceSnapshot=None)
        manifest['files'] = [record(f, f.relative_to(stage).as_posix()) for f in sorted(stage.rglob('*')) if f.is_file()]
        write_json(stage / 'package-manifest.json', manifest)
        print('STAGE PASS', flavor, len(manifest['files']), sum(r['size'] for r in manifest['files']), stage, flush=True)


def finalize(args):
    assert not subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain']).strip(), 'Commit before finalizing'
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD']).decode().strip()
    for flavor in ('NVIDIA', 'AMD'):
        stage = args.output / f'Veyra-{args.version}-{flavor}-win64-portable'
        manifest = json.loads((stage / 'package-manifest.json').read_text(encoding='utf8'))
        for row in manifest['files']:
            f = stage / row['path']
            assert f.stat().st_size == row['size'] and pkg.digest(f) == row['sha256'], row['path']
        assert pkg.digest(stage / 'veyra_qml_ui.exe') == pkg.digest(args.build / 'veyra_qml_ui.exe')
        for name in ('README.md', 'README_EN.md', 'README_CN.md', 'THIRD_PARTY_NOTICES.md'):
            checked_copy(ROOT / name, stage, name)
        for name in ('BUILD_' + args.version + '.md', 'RUNTIME_COMPONENTS_' + args.version + '.md',
                     'RELEASE_NOTES_' + args.version + '.md', 'RELEASE_2.0.3_ACCEPTANCE_2026-10-04.md'):
            checked_copy(ROOT / 'docs' / name, stage, 'docs/' + name)
        checked_copy(ROOT / 'docs' / ('RELEASE_NOTES_' + args.version + '.md'), stage, 'RELEASE_NOTES.md')
        for f in (ROOT / 'qml/Veyra').rglob('*'):
            if f.is_file(): assert pkg.digest(f) == pkg.digest(stage / 'qml/Veyra' / f.relative_to(ROOT / 'qml/Veyra')), f
        check_vendor(stage, flavor)
        manifest.update(baseCommit=commit, sourceArchiveCommit=commit, worktreeDirty=False, releaseReady=True,
                        correspondingSource=dict(application=f'https://github.com/Likely7/Veyra-NRVideo/archive/refs/tags/v{args.version}.zip',
                                                 dependencies=f'https://github.com/Likely7/Veyra-NRVideo/releases/download/v{args.version}/Veyra-{args.version}-dependency-source.zip',
                                                 instructions=f'docs/BUILD_{args.version}.md'))
        manifest.pop('sourceSnapshot', None)
        manifest['files'] = [record(f, f.relative_to(stage).as_posix()) for f in sorted(stage.rglob('*')) if f.is_file() and f.name != 'package-manifest.json']
        write_json(stage / 'package-manifest.json', manifest)
        print('FINALIZE PASS', flavor, commit, len(manifest['files']), flush=True)


def archive(args):
    for flavor in ('NVIDIA', 'AMD'):
        stage = args.output / f'Veyra-{args.version}-{flavor}-win64-portable'
        check_vendor(stage, flavor)
        manifest = json.loads((stage / 'package-manifest.json').read_text(encoding='utf8'))
        assert manifest['releaseReady'] and not manifest['worktreeDirty']
        destination = Path(str(stage) + '.7z')
        assert not destination.exists()
        tmp = BASE / 'tmp' / 'release-2.0.3-20261004' / ('compress-' + flavor)
        tmp.mkdir(parents=True, exist_ok=False)
        env = os.environ.copy(); env.update(TEMP=str(tmp), TMP=str(tmp))
        with (args.output / (flavor + '-archive.log')).open('xb') as log:
            subprocess.run([str(args.seven_zip), 'a', '-t7z', '-m0=lzma2', '-mx=5', '-md=256m', '-mmt=4', '-ms=on',
                            str(destination), stage.name], cwd=args.output, env=env, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=890)
        destination.with_suffix('.7z.sha256').write_text(pkg.digest(destination) + '  ' + destination.name + '\n', encoding='ascii')
        print('ARCHIVE PASS', flavor, destination.stat().st_size, pkg.digest(destination), flush=True)


def refresh(args):
    # An explicit rebuild replaces only the production executable and QML.
    # Other identities are checked against the previous staging manifest first.
    for flavor in ('NVIDIA', 'AMD'):
        stage = args.output / f'Veyra-{args.version}-{flavor}-win64-portable'
        manifest = json.loads((stage / 'package-manifest.json').read_text(encoding='utf8'))
        for row in manifest['files']:
            f = stage / row['path']
            assert f.stat().st_size == row['size'] and pkg.digest(f) == row['sha256'], row['path']
        checked_copy(args.build / 'veyra_qml_ui.exe', stage, 'veyra_qml_ui.exe')
        for f in (args.build / 'qml/Veyra').rglob('*'):
            if f.is_file(): checked_copy(f, stage, 'qml/Veyra/' + f.relative_to(args.build / 'qml/Veyra').as_posix())
        manifest.update(worktreeDirty=True, releaseReady=False)
        manifest['files'] = [record(f, f.relative_to(stage).as_posix()) for f in sorted(stage.rglob('*')) if f.is_file() and f.name != 'package-manifest.json']
        write_json(stage / 'package-manifest.json', manifest)
        print('REFRESH PASS', flavor, pkg.digest(stage / 'veyra_qml_ui.exe'), flush=True)


def shared_fsr(args):
    """Reconcile the owned pre-steering stage with the user's FSR exception."""
    stage = args.output / f'Veyra-{args.version}-NVIDIA-win64-portable'
    manifest = json.loads((stage / 'package-manifest.json').read_text(encoding='utf8'))
    for row in manifest['files']:
        f = stage / row['path']
        assert f.stat().st_size == row['size'] and pkg.digest(f) == row['sha256'], row['path']
    legacy = stage / 'runtime_local/amd/fidelityfx/amd_fidelityfx_dx12.dll'
    assert legacy.resolve().is_relative_to(stage.resolve())
    assert pkg.digest(legacy) == '12a5081257ec95b0b53ad51b4a87fb3c03f97fe0bbb59f9496968f8d50ef93a6'
    legacy.unlink()  # Only our exact, newly staged file; no runtime DLL is edited.
    shared = json.loads((args.common_stage / 'release-runtime-manifest.json').read_text(encoding='utf8'))
    runtime = json.loads((stage / 'release-runtime-manifest.json').read_text(encoding='utf8'))
    runtime['files'] = [r for r in runtime['files'] if not r['path'].startswith('runtime_local/amd/fidelityfx/')]
    for row in shared['files']:
        if row['path'].startswith('runtime_local/amd/fidelityfx/'):
            checked_copy(args.common_stage / row['path'], stage, row['path'], row)
            runtime['files'].append(row)
    runtime['sharedFidelityFxException'] = '2026-10-04 user requested retaining the existing FSR3.1/4 components together; FSR4 stays disabled on NVIDIA'
    write_json(stage / 'release-runtime-manifest.json', runtime)
    write_json(stage / 'runtime/experimental/release-runtime-manifest.json', runtime)
    (stage / 'GPU-PACKAGE.txt').write_text('Veyra 2.0.3 / NVIDIA\nThree NVIDIA NR versions, DLSS/RTX SR, HDR, DLSS FG, VFG 2-8X, FSR3.1 and XeSS. AMD NR is excluded. Shared FidelityFX SDK 2.3.0 components are retained by user choice; FSR4 ML remains visible but disabled on NVIDIA. Extract with 7-Zip, then run veyra_qml_ui.exe. Unsupported functions stay gray. Driver DLLs and proprietary SDKs are not included.\n', encoding='utf8')
    check_vendor(stage, 'NVIDIA')
    manifest['files'] = [record(f, f.relative_to(stage).as_posix()) for f in sorted(stage.rglob('*')) if f.is_file() and f.name != 'package-manifest.json']
    write_json(stage / 'package-manifest.json', manifest)
    print('SHARED FSR PASS', len(runtime['files']), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=('stage', 'shared-fsr', 'refresh', 'finalize', 'archive', 'verify'))
    for name in ('common-stage', 'runtime-source', 'amd-source', 'build', 'seven-zip'):
        parser.add_argument('--' + name, type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', default='2.0.3')
    args = parser.parse_args()
    assert args.output.resolve().is_relative_to(BASE.resolve())
    if args.mode == 'stage': stage_packages(args)
    elif args.mode == 'shared-fsr': shared_fsr(args)
    elif args.mode == 'refresh': refresh(args)
    elif args.mode == 'finalize': finalize(args)
    elif args.mode == 'archive': archive(args)
    else:
        for flavor in ('NVIDIA', 'AMD'):
            stage = args.output / f'Veyra-{args.version}-{flavor}-win64-portable'
            names = check_vendor(stage, flavor)
            print('VENDOR CONTENT PASS', flavor, len(names))
