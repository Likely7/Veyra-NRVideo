"""Bounded local FG acceptance; all generated files stay under the E: artifact root.

Guard compares the complete checkout with the immutable pre-task hashes. The
allowlist permits changes, it does not replace those hashes or claim acceptance.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

BASE = Path("E:/项目/Veyra")
TAG = "fg-fsr-xess-20260930"
ROOT = BASE / "worktrees/p0-p1-r53-20260927"
BASELINE = BASE / "archives/fg-fsr-xess-20260930-start/scope-baseline-d.json"
BASELINE_SHA = "dd0791c996344ea9f0cf8f516e84b5086dd8810eedbf36c7b400a1707dc90bfc"


def guard():
    raw = BASELINE.read_bytes()
    if hashlib.sha256(raw).hexdigest() != BASELINE_SHA:
        raise RuntimeError("immutable scope baseline hash changed")
    baseline = json.loads(raw)
    if Path(baseline["root"]) != ROOT:
        raise RuntimeError("scope baseline checkout mismatch")
    names = subprocess.check_output(
        ["git", "ls-files", "-c", "-o", "--exclude-standard", "-z"], cwd=ROOT
    ).decode("utf-8").split("\0")
    allow, hashes = set(baseline["allow"]), baseline["hashes"]
    outside = []
    for name in filter(None, names):
        if name not in allow:
            path = ROOT / name
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != hashes.get(name):
                outside.append(name)
    outside += [name for name in hashes if name not in allow and not (ROOT / name).is_file()]
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    branch = subprocess.check_output(["git", "branch", "--show-current"], cwd=ROOT, text=True).strip()
    if head != baseline["head"] or branch != "codex/fg-fsr-xess-20260930":
        outside.append("Git baseline/branch changed")
    print("SCOPE", "FAIL" if outside else "PASS", "files", len(names) - 1, outside, flush=True)
    return not outside


def run_cases(group, app, out):
    env = os.environ.copy()
    env["TEMP"] = env["TMP"] = str(BASE / "tmp" / TAG)
    env["PATH"] = str(app) + os.pathsep + env.get("PATH", "")
    env["QML2_IMPORT_PATH"] = str(app / "qml")
    if group == "unit":
        cases = [("repair-contract", "veyra_repair_contract_tests", []),
                 ("repair-presets", "veyra_repair_preset_tests", [str(out / "repair-presets.v1")]),
                 ("effect-chain", "veyra_effect_chain_tests", []),
                 ("preset-library", "veyra_preset_library_tests", [])]
    elif group == "qml":
        cases = [("qml-data", "veyra_qml_data_tests", [str(out / "qml-data-scratch")]),
                 ("qml-easing", "veyra_qml_easing_tests", ["-platform", "offscreen"]),
                 ("qml-quick", "veyra_qml_quick_tests", ["-platform", "offscreen", "-input", str(app / "qml-tests")])]
    else:
        cases = [("fsr-independent-nvidia", "veyra_fsr_dispatch_tests", []),
                 ("fsr-independent-amd", "veyra_fsr_dispatch_tests", ["amd"]),
                 ("fsr-switch-nvidia", "veyra_fsr_switch_tests", [str(out / "switch-nvidia")]),
                 ("fsr-switch-amd", "veyra_fsr_switch_tests", [str(out / "switch-amd"), "amd"]),
                 ("dlss-presentation", "veyra_fg_presentation_tests", [str(out / "dlss"), str(app / "runtime/experimental")])]
    results = []
    for name, binary, args in cases:
        log = out / (name + ".log")
        command = [str(app / (binary + ".exe")), *args]
        started = time.monotonic()
        with log.open("w", encoding="utf-8") as stream:
            try:
                code = subprocess.run(command, cwd=app, env=env, stdout=stream,
                                      stderr=subprocess.STDOUT, timeout=300).returncode
            except subprocess.TimeoutExpired:
                code = 124
        lines = log.read_text(encoding="utf-8", errors="replace").splitlines()
        last = lines[-1] if lines else ""
        result = dict(test=name, command=command, exit=code,
                      seconds=round(time.monotonic() - started, 2), log=str(log), last=last)
        results.append(result)
        print(name, "exit", code, "seconds", result["seconds"], last[:180], flush=True)
        (out / "summary.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    return all(item["exit"] == 0 for item in results)


def run_ui(app, out):
    profile=BASE / "tests" / TAG / ("ui-data-"+out.name)
    if profile.exists():
        raise RuntimeError("UI profile already exists; use a fresh run id")
    main=app / "qml/Veyra/Main.qml"
    original=main.read_bytes()
    source=original.decode("utf-8-sig")
    snippet=(ROOT / "scripts/acceptance/fg-fsr-xess.qml").read_text(encoding="utf-8")
    closing=source.rfind("}")
    if closing<0:
        raise RuntimeError("staged Main.qml has no root closing brace")
    env=os.environ.copy()
    env["TEMP"]=env["TMP"]=str(BASE / "tmp" / TAG)
    env["QML_DISABLE_DISK_CACHE"]="1"
    env["QML2_IMPORT_PATH"]=str(app / "qml")
    results=[]
    try:
        main.write_text(source[:closing]+snippet+source[closing:],encoding="utf-8")
        for phase in ("live","restore"):
            log=out / (phase+".log")
            env["VEYRA_LOG_FILE"]=str(log)
            command=[str(app / "veyra_qml_ui.exe"),"--data-dir",str(profile),"--exit-after","180000"]
            if phase=="restore":
                command.append("--fg-restore")
            started=time.monotonic()
            with (out / (phase+".stdio.log")).open("w",encoding="utf-8") as stream:
                process=subprocess.Popen(command,cwd=app,env=env,stdout=stream,stderr=subprocess.STDOUT)
                print("UI",phase,"pid",process.pid,flush=True)
                try:
                    code=process.wait(timeout=210)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
                    code=124
            lines=log.read_text(encoding="utf-8",errors="replace").splitlines() if log.exists() else []
            sentinel="FG_UI_RESTORE_PASS" if phase=="restore" else "FG_UI_PASS"
            passed=code==0 and any(sentinel in line for line in lines) and not any("FG_UI_FAIL" in line for line in lines)
            result=dict(phase=phase,command=command,pid=process.pid,exit=code,
                        seconds=round(time.monotonic()-started,2),passed=passed,log=str(log))
            results.append(result)
            print(result,flush=True)
            print("\n".join(line for line in lines if "FG_UI_" in line),flush=True)
            (out / "summary.json").write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding="utf-8")
            if not passed:
                break
    finally:
        main.write_bytes(original)
    return len(results)==2 and all(result["passed"] for result in results)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("group", choices=["guard", "unit", "qml", "gpu", "ui"])
    parser.add_argument("--app", type=Path, default=BASE / "tests" / TAG / "candidate")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if not guard():
        return 3
    if args.group == "guard":
        return 0
    out = args.out or BASE / "logs" / TAG / args.group
    for path in (args.app, out):
        if not path.resolve().is_relative_to(BASE.resolve()):
            raise RuntimeError("acceptance paths must stay under E:/项目/Veyra")
    if out.exists() and any(out.iterdir()):
        raise RuntimeError("use a new output directory; prior test evidence is immutable")
    out.mkdir(parents=True, exist_ok=True)
    accepted=run_ui(args.app.resolve(),out) if args.group=="ui" else run_cases(args.group,args.app.resolve(),out)
    return 0 if accepted else 1


if __name__ == "__main__":
    sys.exit(main())
