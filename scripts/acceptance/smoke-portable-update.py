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
    data = root / "runtime_local/user-data-2.0.0"
    data.mkdir(parents=True)
    preferences = data / "qml-preferences.v1.json"
    preferences.write_text('{"introMusic":false,"uiScale":125}', encoding="utf-8")
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
            if self.path.endswith("/releases/latest"):
                page = f"https://github.com/Likely7/Veyra-NRVideo/releases/tag/v{version}"
                self.bytes_response(json.dumps(dict(tag_name=f"v{version}", html_url=page,
                                                    draft=False, prerelease=False)).encode())
            elif self.path.endswith("/" + args.manifest.name):
                document = fixture.copy()
                if args.scenario == "bad-signature":
                    document["signature"] = base64.b64encode(bytes(64)).decode()
                self.bytes_response(json.dumps(document).encode())
            elif self.path == f"/Likely7/Veyra-NRVideo/releases/download/v{version}/{row['asset']}":
                self.send_response(302)
                self.send_header("Location", f"http://127.0.0.1:{args.port}/download/{row['asset']}")
                self.send_header("Content-Length", "0")
                self.end_headers()
            elif self.path == "/download/" + row["asset"]:
                self.send_response(200)
                self.send_header("Content-Length", str(source.stat().st_size))
                self.end_headers()
                with source.open("rb") as payload:
                    if args.scenario == "bad-archive":
                        self.wfile.write(bytes([payload.read(1)[0] ^ 1]))
                    shutil.copyfileobj(payload, self.wfile, 1 << 20)
                transfers.append(time.monotonic())
            else:
                self.send_error(404)

    download = "/" + row["asset"]
    installer = root / "Veyra.exe"
    logs = root / "logs/veyra-qml.log"
    env = os.environ.copy()
    scratch = args.output / "scratch"
    scratch.mkdir()
    env.update(TEMP=str(scratch), TMP=str(scratch), VEYRA_XBOX_DATA=str(scratch / "xbox"),
               VEYRA_MOONLIGHT_DATA=str(scratch / "moonlight"), VEYRA_PORTABLE_UPDATE_SMOKE="1")
    with ThreadingHTTPServer(("127.0.0.1", args.port), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            command = [str(installer), "--page", "set", "--tab", "about"]
            if args.scenario in {"bad-signature", "bad-archive"}:
                command += ["--exit-after", "20000"]
            subprocess.run(command, env=env, cwd=root, check=True, timeout=240)
            finished = time.monotonic()
        finally:
            server.shutdown()
            thread.join()

    active = (root / "current-version.txt").read_text(encoding="ascii").splitlines()
    assert lut.read_text(encoding="utf-8") == "LUT_3D_SIZE 2\n"
    assert json.loads(preferences.read_text(encoding="utf-8"))["uiScale"] == 125
    result_file = root / ".veyra-update/result.txt"
    result = result_file.read_text(encoding="ascii").strip() if result_file.exists() else None
    if args.scenario == "success":
        assert result == "installed" and active[0].startswith(version + "-") and active[1] == before, (active, result)
        env.pop("VEYRA_PORTABLE_UPDATE_SMOKE")
        subprocess.run([str(installer), "--page", "set", "--exit-after", "1700"], env=env, cwd=root, check=True, timeout=30)
        assert not result_file.exists() and "Veyra QML session started" in logs.read_text(encoding="utf-8")
        if args.rollback:
            player = root / "versions" / active[0] / "veyra_qml_ui.exe"
            player.rename(player.with_suffix(".disabled-for-smoke"))
            subprocess.run([str(installer), "--page", "set", "--exit-after", "1700"], env=env, cwd=root, check=True, timeout=30)
            assert (root / "current-version.txt").read_text(encoding="ascii").splitlines()[0] == before
            assert not result_file.exists(), "fallback player did not consume the result record"
    elif args.scenario == "bad-signature":
        assert active[0] == before and not any(p.endswith(download) for p in received), (active, received)
    elif args.scenario == "bad-archive":
        assert active[0] == before and any(p.endswith(download) for p in received), (active, received)
        assert transfers and finished - transfers[-1] > 1, "archive was not fully served while the app was running"
        assert not (root / ".veyra-update/pending.json").exists()
    else:
        assert active[0] == before and result == "startup-check-failed", (active, result)
    print(json.dumps(dict(scenario=args.scenario, flavor=flavor, result=result,
                          rollback=args.rollback, prior=before, active=active, requests=received,
                          wholeArchiveServed=bool(transfers)),
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--scenario", choices=("success", "bad-signature", "bad-archive", "probe-failure"), default="success")
    parser.add_argument("--rollback", action="store_true")
    run(parser.parse_args())
