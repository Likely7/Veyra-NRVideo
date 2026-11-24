"""Exercise a loopback-only portable build using two signed test release ZIPs."""
import argparse
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time
import zipfile


def run(args):
    with zipfile.ZipFile(args.base) as archive:
        outer = args.base.stem
        trust = json.loads(archive.read(f"{outer}/portable-update-trust.json"))
        if trust.get("testBuild") is not True:
            raise ValueError("this smoke test requires a loopback-only build")
        args.output.mkdir(parents=True, exist_ok=False)
        archive.extractall(args.output)

    root = args.output / outer
    deferred = args.scenario == "deferred"
    default_data = root / "runtime_local/user-data-2.0.0"
    data = root / "runtime_local/test profile" if deferred else default_data
    data.mkdir(parents=True)
    preferences = data / "qml-preferences.v1.json"
    preferences.write_text(json.dumps({"introMusic": False, "uiScale": 125, "updateDownloadRoute": args.route}),
                           encoding="utf-8")
    luts = root / "runtime_local/luts"
    luts.mkdir()
    lut = luts / "smoke.cube"
    lut.write_text("LUT_3D_SIZE 2\n", encoding="utf-8")
    before = (root / "current-version.txt").read_text(encoding="ascii").splitlines()[0]
    fixture = json.loads(args.manifest.read_bytes())
    contents = json.loads(base64.b64decode(fixture["payload"]))
    version = contents["version"]
    flavor = (root / "portable-package.txt").read_text(encoding="ascii").splitlines()[1]
    row = next(p for p in contents["packages"] if p["flavor"] == flavor)
    source = args.manifest.parent / row["asset"]
    received = []
    transfers = []
    mirror_prefix = f"/mirror/{args.route}"
    release_path = f"/Likely7/Veyra-NRVideo/releases/download/v{version}"

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def bytes_response(self, body):
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            received.append(self.path)
            mirrored = self.path.startswith(mirror_prefix + "/")
            if mirrored and args.mirror_unavailable:
                self.send_error(503)
                return
            path = self.path.removeprefix(mirror_prefix) if mirrored else self.path
            if path.endswith("/releases/latest"):
                page = f"https://github.com/Likely7/Veyra-NRVideo/releases/tag/v{version}"
                self.bytes_response(json.dumps(dict(tag_name=f"v{version}", html_url=page,
                                                    draft=False, prerelease=False)).encode())
            elif path == release_path + "/" + args.manifest.name:
                document = fixture.copy()
                if args.scenario == "bad-signature":
                    document["signature"] = base64.b64encode(bytes(64)).decode()
                self.bytes_response(json.dumps(document).encode())
            elif path == release_path + "/" + row["asset"]:
                self.send_response(302)
                prefix = mirror_prefix if mirrored else ""
                self.send_header("Location", f"http://127.0.0.1:{args.port}{prefix}/download/{row['asset']}")
                self.send_header("Content-Length", "0")
                self.end_headers()
            elif path == "/download/" + row["asset"]:
                self.send_response(200)
                self.send_header("Content-Length", str(source.stat().st_size))
                self.end_headers()
                with source.open("rb") as payload:
                    if args.scenario == "bad-archive" or (mirrored and args.mirror_corrupt_archive):
                        self.wfile.write(bytes([payload.read(1)[0] ^ 1]))
                    shutil.copyfileobj(payload, self.wfile, 1 << 20)
                transfers.append(time.monotonic())
            else:
                self.send_error(404)

    download = "/" + row["asset"]
    launcher = root / "Veyra.exe"
    state_file = root / "current-version.txt"
    update_folder = root / ".veyra-update"
    prepared = update_folder / "prepared-version.txt"
    scratch = args.output / "scratch"
    scratch.mkdir()
    logs = scratch / "veyra-qml.log"
    env = {key: value for key, value in os.environ.items()
           if not key.upper().startswith(("VEYRA_", "QT_", "QML_", "QSG_"))}
    env.update(TEMP=str(scratch), TMP=str(scratch), APPDATA=str(scratch / "Roaming"),
               LOCALAPPDATA=str(scratch / "Local"), CUDA_CACHE_PATH=str(scratch / "CUDA"),
               VEYRA_XBOX_DATA=str(scratch / "xbox"), VEYRA_MOONLIGHT_DATA=str(scratch / "moonlight"),
               VEYRA_LOG_FILE=str(logs), VEYRA_AUTO_RESUME="0", QML_DISABLE_DISK_CACHE="1",
               VEYRA_PORTABLE_UPDATE_SMOKE="later" if args.scenario == "deferred" else "1")

    def log_text():
        # A restarting process can use a PID-suffixed log while the old
        # process's log file is still locked by Windows.
        return "\n".join(path.read_text(encoding="utf-8", errors="replace")
                         for path in sorted(scratch.glob("veyra-qml*.log")))

    def root_idle():
        try:
            with (root / ".veyra-running.lock").open("rb"):
                return True
        except PermissionError:
            return False

    def start(arguments, timeout):
        subprocess.run([str(launcher), *arguments], env=env, cwd=root, check=True, timeout=timeout)

    succeeded = args.scenario in {"success", "deferred"}
    with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            command = ["--page", "set", "--tab", "about", "--dialog", "update"]
            if deferred:
                command += ["--data-dir", str(data)]
            if args.scenario == "bad-signature":
                command += ["--exit-after", "10000"]
            start(command, 210)
            finished = time.monotonic()
            if deferred:
                assert state_file.read_text(encoding="ascii").splitlines()[0] == before
                assert prepared.is_file() and (update_folder / "pending.json").is_file()
                assert not (update_folder / "download.zip").exists()
                env["VEYRA_PORTABLE_UPDATE_SMOKE"] = "resume"
                start(["--data-dir", str(data), "--dialog", "update"], 30)
            if succeeded:
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    if "[portable-update-result] installed" in log_text() and root_idle():
                        break
                    time.sleep(0.1)
        finally:
            server.shutdown()
            thread.join()

    active = state_file.read_text(encoding="ascii").splitlines()
    assert lut.read_text(encoding="utf-8") == "LUT_3D_SIZE 2\n"
    if deferred:
        assert not (default_data / "qml-preferences.v1.json").exists(), "restart dropped the selected profile"
    saved_preferences = json.loads(preferences.read_text(encoding="utf-8"))
    assert saved_preferences["uiScale"] == 125 and saved_preferences["updateDownloadRoute"] == args.route
    history = log_text()
    if succeeded:
        result = "installed" if "[portable-update-result] installed" in history else "no-restart"
        assert result == "installed" and active[0].startswith(version + "-") and active[1] == before, (active, result)
        assert root_idle(), "automatically reopened app did not exit"
        assert not prepared.exists() and not (update_folder / "pending.json").exists()
        assert all(f"[portable-update-install-step] {step}" in history for step in (1, 2, 3)), history
        if args.rollback:
            env.pop("VEYRA_PORTABLE_UPDATE_SMOKE")
            player = root / "versions" / active[0] / "veyra_qml_ui.exe"
            player.rename(player.with_suffix(".disabled-for-smoke"))
            start(["--page", "set", "--exit-after", "1700"], 30)
            assert state_file.read_text(encoding="ascii").splitlines()[0] == before
            assert "[portable-update-result] rolled-back" in log_text()
    elif args.scenario == "bad-signature":
        result = "signature-rejected"
        assert active[0] == before and not any(p.endswith(download) for p in received), (active, received)
    elif args.scenario == "bad-archive":
        result = "archive-rejected"
        assert active[0] == before and any(p.endswith(download) for p in received), (active, received)
        assert transfers and finished - transfers[-1] >= 0, "archive was not fully served while the app was running"
        assert not (update_folder / "pending.json").exists() and not prepared.exists()
    else:
        result = "startup-check-failed"
        assert active[0] == before and "startup-check-failed" in history, history
        assert not prepared.exists()
    if args.route != "github":
        api = "/repos/Likely7/Veyra-NRVideo/releases/latest"
        assert (mirror_prefix + api in received) == (args.route == "gh-proxy"), received
        assert mirror_prefix + release_path + "/" + args.manifest.name in received, received
        if args.scenario != "bad-signature":
            assert mirror_prefix + release_path + download in received, received
        if args.mirror_unavailable:
            assert api in received and release_path + "/" + args.manifest.name in received, received
        if args.mirror_unavailable or args.mirror_corrupt_archive:
            assert release_path + download in received, received
        if succeeded and not args.mirror_unavailable:
            assert (api in received) == (args.route == "ghfast"), received
            assert release_path + "/" + args.manifest.name not in received, received
            assert (release_path + download in received) == args.mirror_corrupt_archive, received
    print(json.dumps(dict(scenario=args.scenario, flavor=flavor, result=result,
                          rollback=args.rollback, prior=before, active=active, requests=received,
                          route=args.route, mirrorUnavailable=args.mirror_unavailable,
                          mirrorCorruptArchive=args.mirror_corrupt_archive,
                          wholeArchiveServed=bool(transfers)),
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--scenario", choices=("success", "deferred", "bad-signature", "bad-archive", "probe-failure"), default="success")
    parser.add_argument("--route", choices=("github", "gh-proxy", "ghfast"), default="github")
    parser.add_argument("--mirror-unavailable", action="store_true")
    parser.add_argument("--mirror-corrupt-archive", action="store_true")
    parser.add_argument("--rollback", action="store_true")
    parameters = parser.parse_args()
    if parameters.route == "github" and (parameters.mirror_unavailable or parameters.mirror_corrupt_archive):
        parser.error("mirror failures require --route gh-proxy or --route ghfast")
    run(parameters)
