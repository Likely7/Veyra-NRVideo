"""Wrap audited per-GPU stages and sign the final ZIP identities offline."""
import argparse
import base64
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path, PureWindowsPath
import re
import stat
import subprocess
import tempfile
import zipfile

FLAVORS = ("NVIDIA", "AMD")
PLAYER = "veyra_qml_ui.exe"
WORKER = "veyra_update_worker.exe"
INDEX = "Veyra-portable-update-v1.json"


def canonical_version(value):
    parts = value.split(".")
    if len(parts) != 3 or any(not re.fullmatch(r"0|[1-9][0-9]{0,4}", p) or int(p) > 65535 for p in parts):
        raise argparse.ArgumentTypeError("expected a three-part Windows version")
    return value


def stem(version, flavor):
    return f"Veyra-{version}-{flavor}-win64-portable"


def numeric_version(value):
    return canonical_version(value.split("-", 1)[0])


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def safe_path(name):
    return (
        0 < len(name) < 500
        and not re.search(r'[\x00-\x1f\\:"*?<>|]', name)
        and all(part not in {"", ".", ".."} and not part.endswith((" ", "."))
                and not PureWindowsPath(part).is_reserved() for part in name.split("/"))
    )


def get_trust(data, test_build):
    trust = json.loads(data)
    key = trust.get("publicKey", "")
    if not re.fullmatch(r"[0-9a-f]{64}", key) or trust.get("testBuild") is not test_build:
        raise ValueError("build needs the correct embedded Ed25519 key and release/test configuration")
    return key


@contextmanager
def new_artifact(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        raise FileExistsError(path)
    with tempfile.TemporaryDirectory(prefix="portable-", dir=path.parent) as scratch:
        temporary = Path(scratch) / path.name
        yield temporary
        if path.exists():
            raise FileExistsError(path)
        temporary.rename(path)


def wrap(args):
    metadata = args.stage / "package-manifest.json"
    manifest = json.loads(metadata.read_text(encoding="utf-8-sig"))
    flavor = manifest.get("gpuPackage")
    if flavor not in FLAVORS or numeric_version(manifest["version"]) != args.version:
        raise ValueError("stage version or GPU package does not match")
    if not args.test and (manifest.get("localOnly") or not manifest.get("releaseReady")):
        raise ValueError("the stage must pass the normal publisher audit")
    trust = (args.build / "portable-update-trust.json").read_bytes()
    get_trust(trust, args.test)
    if digest(args.stage / PLAYER) != digest(args.build / PLAYER):
        raise ValueError("use the stage, player, launcher and worker from the same build")
    prefix = stem(args.version, flavor)
    destination = args.output / f"{prefix}.zip"
    rows = manifest["files"]
    names = {row["path"].casefold() for row in rows}
    needed = {PLAYER, "qt6core.dll", "platforms/qwindows.dll", "qml/veyra/main.qml"}
    if len(names) != len(rows) or not needed <= names or WORKER in names:
        raise ValueError("duplicate, missing or reserved paths in audited stage")
    with new_artifact(destination) as temporary, \
            zipfile.ZipFile(temporary, "x", zipfile.ZIP_DEFLATED, compresslevel=3, allowZip64=False) as bundle:
        bundle.writestr(f"{prefix}/portable-package.txt", f"VEYRA_PORTABLE_V1\n{flavor}\n")
        bundle.writestr(f"{prefix}/current-version.txt", f"{args.version}\n\n")
        bundle.writestr(f"{prefix}/portable-update-trust.json", trust)
        bundle.write(args.build / "veyra_portable_launcher.exe", f"{prefix}/Veyra.exe")
        version_path = f"{prefix}/versions/{args.version}"
        bundle.write(args.build / WORKER, f"{version_path}/{WORKER}")
        bundle.write(metadata, f"{version_path}/package-manifest.json")
        for row in rows:
            name = row["path"]
            if (not safe_path(name) or name.casefold() == "package-manifest.json"
                    or name.casefold().startswith(("logs/", "runtime_local/user-data-2.0.0/", "runtime_local/luts/"))):
                raise ValueError(f"invalid or user-specific stage path: {name}")
            source = args.stage / name
            if (not source.is_file() or source.resolve() != args.stage.resolve() / name
                    or source.stat().st_size != row["size"] or digest(source) != row["sha256"].lower()):
                raise ValueError(f"stage file does not match publisher audit: {name}")
            bundle.write(source, f"{version_path}/{name}")
        bundle.writestr(f"{prefix}/START-HERE.txt",
                        "运行 Veyra.exe。2.1.9 用户先退出旧版，再按需复制旧目录中的\n"
                        "runtime_local/user-data-2.0.0 与 runtime_local/luts 到新版根目录的 runtime_local。\n"
                        "新版本：设置 → 关于 → 检查更新。下载完成后，正常退出会应用更新。\n\n"
                        "Start Veyra.exe. To migrate from 2.1.9, close it, then copy\n"
                        "runtime_local/user-data-2.0.0 and runtime_local/luts to this folder's runtime_local.\n"
                        "Settings → About → Check for updates. Updates are applied after a normal exit.\n")
    print(destination)


def inspect_bundle(path, version, flavor, test_build, public_key):
    prefix = stem(version, flavor)
    if path.name != prefix + ".zip":
        raise ValueError(f"wrong portable ZIP name: {path}")
    version_path = f"{prefix}/versions/{version}/"
    with zipfile.ZipFile(path) as bundle:
        entries = bundle.infolist()
        names = [entry.filename for entry in entries]
        if (not 0 < len(entries) <= 30000 or len({name.casefold() for name in names}) != len(names)
                or any(not safe_path(entry.filename) or not entry.filename.startswith(prefix + "/")
                       or stat.S_IFMT(entry.external_attr >> 16) not in {0, stat.S_IFREG}
                       or entry.flag_bits & 1 for entry in entries)):
            raise ValueError("ZIP must contain unique, regular relative files in the portable directory")
        metadata_path = version_path + "package-manifest.json"
        if bundle.getinfo(metadata_path).file_size > 8 * 1024 * 1024:
            raise ValueError("publisher audit is too large")
        audit = json.loads(bundle.read(metadata_path))
        if audit["gpuPackage"] != flavor or numeric_version(audit["version"]) != version:
            raise ValueError("publisher audit differs from the requested package")
        files = {version_path + row["path"]: row for row in audit["files"]}
        base = {prefix + "/" + n for n in ("Veyra.exe", "START-HERE.txt", "portable-package.txt",
                                          "current-version.txt", "portable-update-trust.json")}
        if version_path + PLAYER not in files or set(names) != set(files) | base | {metadata_path, version_path + WORKER}:
            raise ValueError("portable ZIP is missing audited files or contains extra files")
        if not test_build and (audit.get("localOnly") or not audit.get("releaseReady")):
            raise ValueError("refusing to sign a stage without publisher approval")
        if (bundle.read(prefix + "/portable-package.txt") != f"VEYRA_PORTABLE_V1\n{flavor}\n".encode()
                or bundle.read(prefix + "/current-version.txt") != f"{version}\n\n".encode()
                or get_trust(bundle.read(prefix + "/portable-update-trust.json"), test_build) != public_key):
            raise ValueError("package identity or embedded trust key does not match")
        expanded = sum(entry.file_size for entry in entries)
        size = path.stat().st_size
        if not 0 < size <= 3 << 30 or not 0 < expanded <= 8 << 30:
            raise ValueError("portable archive exceeds the downloader limits")
        for name, row in files.items():
            with bundle.open(name) as stream:
                if bundle.getinfo(name).file_size != row["size"] or hashlib.file_digest(stream, "sha256").hexdigest() != row["sha256"].lower():
                    raise ValueError(f"archived bytes differ from publisher audit: {name}")
    return dict(flavor=flavor, asset=path.name, sha256=digest(path), size=size, expandedSize=expanded)


def sign(args):
    openssl = str(args.openssl.resolve(strict=True))
    key = str(args.key.resolve(strict=True))
    public_der = subprocess.check_output([openssl, "pkey", "-in", key, "-pubout", "-outform", "DER"], timeout=120)
    if len(public_der) != 44 or public_der[:12].hex() != "302a300506032b6570032100":
        raise ValueError("expected an Ed25519 private key")
    rows = [inspect_bundle(path, args.version, flavor, args.test, public_der[12:].hex())
            for path, flavor in zip((args.nvidia, args.amd), FLAVORS, strict=True)]
    notes = args.notes.read_text(encoding="utf-8-sig").strip() if args.notes else ""
    if len(notes) > 480:
        raise ValueError("in-app release summary must be at most 480 characters")
    payload = json.dumps(dict(schema=1, product="Veyra-portable", arch="windows-x64",
                              channel="stable", version=args.version, notes=notes, packages=rows),
                         separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sign-", dir=args.output) as scratch:
        message = Path(scratch) / "payload.json"
        message.write_bytes(payload)
        signature = subprocess.check_output([openssl, "pkeyutl", "-sign", "-rawin", "-inkey", key,
                                             "-in", str(message)], timeout=120)
    if len(signature) != 64:
        raise ValueError("invalid Ed25519 signature length")
    envelope = dict(payload=base64.b64encode(payload).decode("ascii"),
                    signature=base64.b64encode(signature).decode("ascii"))
    output = args.output / INDEX
    with new_artifact(output) as temporary, temporary.open("x", encoding="utf-8", newline="\n") as file:
        json.dump(envelope, file, separators=(",", ":"))
        file.write("\n")
    print(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    for name, action, paths in (
        ("wrap", wrap, ("stage", "build")),
        ("sign", sign, ("nvidia", "amd", "key", "openssl")),
    ):
        command = modes.add_parser(name)
        command.set_defaults(action=action)
        command.add_argument("--version", type=canonical_version, required=True)
        command.add_argument("--output", type=Path, required=True)
        command.add_argument("--test", action="store_true", help="only allow isolated loopback test builds")
        for option in paths:
            command.add_argument("--" + option, type=Path, required=True)
    modes.choices["sign"].add_argument("--notes", type=Path)
    args = parser.parse_args()
    args.action(args)


if __name__ == "__main__":
    main()
