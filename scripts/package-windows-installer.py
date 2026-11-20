"""Build a Windows Setup EXE from one audited Veyra GPU stage."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import tempfile

from PIL import Image, ImageOps


ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS = Path("E:/项目/Veyra")


def load_packaging_module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


portable = load_packaging_module("portable_package", "package-portable-update.py")
qml = load_packaging_module("qml_package", "package-qml-release.py")


def read_stage(stage, build, local_test):
    metadata_path = stage / "package-manifest.json"
    metadata = json.loads(metadata_path.read_bytes())
    version = portable.canonical_version(metadata["version"])
    flavor = metadata["gpuPackage"]
    if flavor not in portable.FLAVORS:
        raise ValueError("Expected a per-GPU NVIDIA or AMD stage")
    if not local_test and (metadata.get("localOnly") or not metadata.get("releaseReady")):
        raise ValueError("The stage must pass the publisher audit, or be marked --local-test")

    payload = {}
    seen = set()
    for row in metadata["files"]:
        name = row["path"]
        if not portable.safe_path(name) or name.casefold() in seen or name.casefold() == metadata_path.name:
            raise ValueError(f"Unsafe or duplicate manifest path: {name}")
        qml.validate_payload(name)
        seen.add(name.casefold())
        source = stage / name
        if (not source.is_file() or source.resolve() != stage / name
                or source.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT
                or source.stat().st_size != row["size"]
                or portable.digest(source) != row["sha256"].lower()):
            raise ValueError(f"Stage file failed the publisher audit: {name}")
        payload[name] = source
    missing = (qml.REQUIRED | {"virtualcam/veyra-camera-setup.exe", "virtualcam/veyra-virtualcam32.dll",
                               "virtualcam/veyra-virtualcam64.dll"}) - payload.keys()
    if missing:
        raise ValueError("Required files missing: " + ", ".join(sorted(missing)))
    if portable.digest(payload[portable.PLAYER]) != portable.digest(build / portable.PLAYER):
        raise ValueError("Use the stage, launcher and worker from the same product build")
    trust = json.loads((build / "portable-update-trust.json").read_bytes())
    if trust["testBuild"] or (trust["publicKey"] and not re.fullmatch(r"[0-9a-f]{64}", trust["publicKey"])):
        raise ValueError("Expected production update configuration, without a loopback test server")
    qml.check_models(stage)
    return version, flavor, payload, metadata_path


def compose_branding(directory):
    destination = directory / "brand-panel.png"
    art = ROOT / "qml/Veyra"
    panel = ImageOps.fit(Image.open(art / "backdrop-dusk-strong.png").convert("RGBA"),
                         (492, 1050), centering=(0.62, 0.52))
    sun = Image.open(art / "intro-sun.png").convert("RGBA")
    sun.thumbnail((326, 326), Image.Resampling.LANCZOS)
    sun.putalpha(sun.getchannel("A").point(lambda value: int(value * 0.57)))
    panel.alpha_composite(sun, ((panel.width - sun.width) // 2, 164))
    wordmark = Image.open(art / "wordmark-dusk.png").convert("RGBA")
    wordmark.thumbnail((392, 390), Image.Resampling.LANCZOS)
    panel.alpha_composite(wordmark, ((panel.width - wordmark.width) // 2, 374))
    panel.convert("RGB").save(destination)

    mark = Image.open(art / "logo-dusk.png").convert("RGBA")
    mark.thumbnail((156, 116), Image.Resampling.LANCZOS)
    icon = Image.new("RGBA", (174, 174), (0, 0, 0, 0))
    icon.alpha_composite(mark, ((icon.width - mark.width) // 2, (icon.height - mark.height) // 2))
    small = directory / "brand-icon.png"
    icon.save(small)
    return destination, small


def installer_file(source, folder, name=None):
    destination = rf'DestDir: "{{app}}{folder}"'
    rename = f'; DestName: "{name}"' if name else ""
    return f'Source: "{source}"; {destination}{rename}; Flags: ignoreversion'


def build_setup(args):
    version, flavor, payload, metadata = read_stage(args.stage, args.build, args.local_test)
    output_name = f"Veyra-{version}-{flavor}-win64-setup.exe"
    destination = args.output / output_name
    args.output.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        raise FileExistsError(destination)

    with tempfile.TemporaryDirectory(prefix="iscc-", dir=args.work_dir) as directory_string:
        directory = Path(directory_string)
        temp_output = directory / "output"
        temp_output.mkdir()
        panel, icon = compose_branding(directory)
        (directory / "portable-package.txt").write_bytes(f"VEYRA_PORTABLE_V1\n{flavor}\n".encode("ascii"))
        (directory / "veyra-installation.txt").write_bytes(f"Veyra.Win64.{flavor}".encode("ascii"))
        version_dir = rf"\versions\{version}"
        files = [
            installer_file(args.build / "veyra_portable_launcher.exe", "", "Veyra.exe"),
            installer_file(args.build / "portable-update-trust.json", ""),
            installer_file(directory / "portable-package.txt", ""),
            installer_file(directory / "veyra-installation.txt", ""),
            installer_file(args.build / portable.WORKER, version_dir),
            installer_file(metadata, version_dir),
            installer_file(args.iscc.parent / "license.txt", version_dir + r"\licenses", "INNO-SETUP.txt"),
        ]
        for name, source in sorted(payload.items()):
            parent = name.rpartition("/")[0]
            folder = version_dir + ("\\" + parent.replace("/", "\\") if parent else "")
            files.append(installer_file(source, folder))
        file_list = directory / "files.iss"
        file_list.write_text("\n".join(files) + "\n", encoding="utf-8-sig")

        defines = dict(Flavor=flavor, AppVersion=version, OutputDir=temp_output,
                       PayloadList=file_list, AppIcon=ROOT / "assets/veyra.ico",
                       BrandBackground=ROOT / "qml/Veyra/backdrop-dusk.png",
                       BrandPanel=panel, BrandIcon=icon)
        command = [str(args.iscc), "/Qp"]
        if args.sign_tool:
            defines["PublisherSigning"] = "1"
            command.append("/Spublisher=" + args.sign_tool)
        command.extend(f"/D{key}={str(value).replace('/', os.sep)}" for key, value in defines.items())
        command.append(str(ROOT / "scripts/installer/Veyra.iss"))
        environment = {**os.environ, "TEMP": str(directory), "TMP": str(directory)}
        with args.log.open("xb") as log:
            subprocess.run(command, cwd=directory, env=environment, stdout=log, stderr=subprocess.STDOUT,
                           timeout=850, check=True)
        intermediate = temp_output / output_name
        with intermediate.open("rb") as archive:
            if archive.read(2) != b"MZ" or intermediate.stat().st_size < 1024 * 1024:
                raise ValueError("Compiler did not produce an offline Windows installer")
        if destination.exists():
            raise FileExistsError(destination)
        intermediate.rename(destination)

    print(json.dumps(dict(path=str(destination), flavor=flavor, version=version,
                          size=destination.stat().st_size, sha256=portable.digest(destination),
                          localTest=args.local_test, payloadFiles=len(files)), ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for item in ("stage", "build", "iscc", "output", "work-dir", "log"):
        parser.add_argument("--" + item, type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument("--local-test", action="store_true")
    parser.add_argument("--sign-tool", help="Inno Setup SignTool command for Setup and its uninstaller")
    args = parser.parse_args()
    if not all(path.is_relative_to(ARTIFACTS.resolve()) for path in (args.output, args.work_dir, args.log)):
        parser.error("Installer build files must stay in the Veyra artifact tree")
    args.work_dir.mkdir(parents=True, exist_ok=True)
    args.log.parent.mkdir(parents=True, exist_ok=True)
    build_setup(args)


if __name__ == "__main__":
    main()
