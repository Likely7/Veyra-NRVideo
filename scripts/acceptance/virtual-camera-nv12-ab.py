#!/usr/bin/env python3
"""Compare the original NV12 path with the optional tiled cubic shader in DirectShow."""

import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time


CASES = {
    "native1080": (1920, 1080, 1920, 1080, 0, 0),
    "native4k": (3840, 2160, 3840, 2160, 0, 0),
    "linear4k": (3840, 2160, 2560, 1440, 0, 0),
    "cubic4k": (3840, 2160, 2560, 1440, 0, 1),
    "pq-cubic4k": (3840, 2160, 2560, 1440, 2, 1),
}
TILED_RESIZE = "using tiled cubic NV12 resize"


def run_logged(argv, env, output):
    with output.open("w", encoding="utf-8") as stream:
        subprocess.run(argv, env=env, stdout=stream, stderr=subprocess.STDOUT,
                       check=True, timeout=120)


def ready(process, output):
    deadline = time.monotonic() + 12
    while process.poll() is None and time.monotonic() < deadline:
        if b"PULL_READY" in output.read_bytes():
            return
        time.sleep(0.05)
    raise RuntimeError(f"Producer did not open its private queue. See {output}")


def fields(log, prefix):
    line = next(line for line in log.read_text(encoding="utf-8", errors="replace").splitlines()
                if line.startswith(prefix + " "))
    return dict(field.split("=", 1) for field in line.split()[1:])


def measure(args, case, folder, env):
    width, height, *_ = CASES[case]
    stdout = folder / "producer.log"
    prefix = folder / "consumer"
    command = [args.producer, "--directshow-pull", *map(str, CASES[case]), str(args.seconds)]
    with stdout.open("w", encoding="utf-8") as output:
        with subprocess.Popen(command, env=env, stdout=output, stderr=subprocess.STDOUT) as producer:
            try:
                ready(producer, stdout)
                run_logged([args.consumer, "consume", args.module, str(args.seconds + 1), "60",
                            str(width), str(height), str(height // 2), prefix], env, folder / "consumer.log")
                if producer.wait(timeout=10):
                    raise RuntimeError(f"Producer failed. See {stdout}")
            finally:
                if producer.poll() is None:
                    producer.kill()
                    producer.wait(timeout=10)
    with prefix.with_suffix(".csv").open(newline="", encoding="utf-8") as stream:
        samples = list(csv.DictReader(stream))
    pixels = prefix.with_suffix(".rows").read_bytes()
    if len(samples) < 90 or len(pixels) != len(samples) * width:
        raise RuntimeError(f"Incomplete consumer samples in {folder}")
    # Skip graph acquisition/first-frame startup and the producer's final drain.
    begin = 60
    end = min(len(samples), args.seconds * 60 - 15)
    window = samples[begin:end]
    if len(window) < 30:
        raise RuntimeError(f"Insufficient steady-state samples in {folder}")
    arrivals = [int(sample["arrival100ns"]) for sample in window]
    timestamps = [int(sample["start100ns"]) for sample in window]
    intervals = [(b - a) / 10000 for a, b in zip(arrivals, arrivals[1:])]
    rows = [pixels[i * width:(i + 1) * width] for i in range(begin, end)]
    return {
        "gpu_conversion_and_copy": fields(stdout, "PULL_GPU"),
        "producer_frames": fields(stdout, "CADENCE"),
        "tiled_cubic": TILED_RESIZE in stdout.read_text(encoding="utf-8", errors="replace"),
        "consumer_samples": len(window),
        "consumer_bytes_per_sample": sorted({int(sample["bytes"]) for sample in window}),
        "sample_timestamp_backsteps": sum(b <= a for a, b in zip(timestamps, timestamps[1:])),
        "repeated_picture_rows": sum(a == b for a, b in zip(rows, rows[1:])),
        "arrival_interval_p50_ms": statistics.median(intervals),
        "arrival_interval_p95_ms": sorted(intervals)[(len(intervals) - 1) * 95 // 100],
    }


def install_shader(source, destination):
    stream = tempfile.NamedTemporaryFile(dir=destination.parent, delete=False)
    temporary = Path(stream.name)
    try:
        with stream:
            with source.open("rb") as original:
                shutil.copyfileobj(original, stream)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--producer", required=True, type=Path, help="test-stage veyra_virtualcam_gpu_tests.exe")
    parser.add_argument("--consumer", required=True, type=Path, help="veyra_virtualcam_cadence.exe")
    parser.add_argument("--module", required=True, type=Path, help="trusted x64 camera module loaded directly")
    parser.add_argument("--baseline", required=True, type=Path, help="original base NV12 DXIL")
    parser.add_argument("--candidate", required=True, type=Path, help="optional tiled cubic NV12 DXIL")
    parser.add_argument("--results", required=True, type=Path, help="new directory under the test task")
    parser.add_argument("--seconds", default=8, type=int, choices=range(3, 31))
    parser.add_argument("--case", action="append", choices=CASES)
    parser.add_argument("--pixels-only", action="store_true")
    parser.add_argument("--candidate-first", action="store_true")
    args = parser.parse_args()
    args.producer = args.producer.resolve(strict=True)
    producer_image = args.producer.read_bytes()
    if not all(marker in producer_image for marker in
               (b"RegOverridePredefKey", "--pixel-fixtures".encode("utf-16le"), b"PULL_READY", TILED_RESIZE.encode())):
        parser.error("Rebuild the producer with the private registry, DirectShow benchmark, and tiled cubic variant.")
    args.consumer = args.consumer.resolve(strict=True)
    args.module = args.module.resolve(strict=True)
    args.baseline = args.baseline.resolve(strict=True)
    args.candidate = args.candidate.resolve(strict=True)
    args.results = args.results.resolve()
    shader = args.producer.parent / "shaders" / "VirtualCameraNv12.dxil"
    variant = shader.with_name("VirtualCameraNv12Cubic.dxil")
    saved = variant.with_name(f"{variant.name}.ab-{os.getpid()}.original")
    if not shader.is_file() or shader.resolve() != shader or variant.resolve() != variant or saved.exists():
        parser.error("Use an isolated test stage with its own shader path.")
    if shader in (args.baseline, args.candidate) or variant in (args.baseline, args.candidate):
        parser.error("Store the comparison shaders outside the producer shader path.")
    if shader.read_bytes() != args.baseline.read_bytes():
        parser.error("The test stage base shader must match the original DXIL byte for byte.")
    args.results.mkdir()
    env = {name: value for name, value in os.environ.items()
           if not name.upper().startswith(("VEYRA_TEST_VIRTUALCAM_", "VEYRA_LOG_FILE"))}
    env["TEMP"] = env["TMP"] = str(args.results)
    env["VEYRA_TEST_VIRTUALCAM_MODULE_DIR"] = str(args.module.parent)
    report = {}
    if variant.exists():
        os.replace(variant, saved)
    try:
        for label in (("candidate", "baseline") if args.candidate_first else ("baseline", "candidate")):
            if label == "candidate":
                install_shader(args.candidate, variant)
            else:
                variant.unlink(missing_ok=True)
            folder = args.results / label
            folder.mkdir()
            env["VEYRA_TEST_VIRTUALCAM_NAMESPACE"] = f"nv12ab-{os.getpid()}-{time.perf_counter_ns()}"
            run_logged([args.producer, "--pixel-fixtures", folder / "pixels"], env, folder / "pixels.log")
            report[label] = {}
            for case in (() if args.pixels_only else (args.case or CASES)):
                test = folder / case
                test.mkdir()
                report[label][case] = measure(args, case, test, env)
    finally:
        variant.unlink(missing_ok=True)
        if saved.exists():
            os.replace(saved, variant)
    baseline = args.results / "baseline" / "pixels"
    candidate = args.results / "candidate" / "pixels"
    names = sorted(path.name for path in baseline.glob("*.nv12"))
    report["pixel_cases"] = names
    report["pixel_differences"] = [name for name in names
                                   if (baseline / name).read_bytes() != (candidate / name).read_bytes()]
    report["routing_differences"] = [f"{label}/{case}" for label in ("baseline", "candidate")
                                     for case, measurements in report[label].items()
                                     if measurements["tiled_cubic"] != (label == "candidate" and CASES[case][-1] == 1)]
    for label in ("baseline", "candidate"):
        pixels_log = (args.results / label / "pixels.log").read_text(encoding="utf-8", errors="replace")
        if (TILED_RESIZE in pixels_log) != (label == "candidate"):
            report["routing_differences"].append(f"{label}/pixels")
    result = args.results / "result.json"
    result.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(result)
    candidate_names = sorted(path.name for path in candidate.glob("*.nv12"))
    return 0 if (len(names) == 12 and candidate_names == names and not report["pixel_differences"]
                 and not report["routing_differences"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
