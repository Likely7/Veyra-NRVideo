#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Analyze private Windows Veyra frame traces, entirely offline."""

import argparse
import bisect
import csv
import json
import math
import re
import statistics
import sys
from collections import Counter, defaultdict
from itertools import chain
from pathlib import Path


SCHEMA = "veyra-frame-trace/1"
LEGACY_HEADER = re.compile(r"Frame trace: records=(\d+) capacity=(\d+) overwritten=(\d+)")
NATIVE_COLUMNS = (
    "event host session revision epoch source batch fence pts detail count ms decodedHost "
    "processHost readyHost presentBeginHost presentEndHost entryDeviationMs returnDeviationMs "
    "queueDepth mediaDeviationValid providerInstance providerCycle preparationCycle "
    "providerCallerRva providerScheduleBeginHost providerFenceSampled providerFenceBefore "
    "providerFenceAtDeadline providerFenceTarget frameKind stage arrivalHost deadlineHost "
    "sourceA sourceAHost gpuBegin gpuEnd gpuFrequency"
).split()
GPU_STAGES = (
    "Color Sr Flow Nr Residual Fg1 Fg2 Fg3 Fg4 Fg5 Fg6 Fg7 FgBatch Blit VideoHdr "
    "NrLayer0 NrLayer1 NrLayer2 NrLayer3 ColorNode0 ColorNode1 ColorNode2 ColorNode3 "
    "ColorNode4 ColorNode5 AaNode0 AaNode1 BeautyNode0 BeautyNode1 BeautyNode2 "
    "BeautyNode3 BeautyNode4 BeautyNode5 BeautyDetect"
).split()  # FrameMetrics.h at Veyra c6633121, for legacy reports with numeric stage IDs.
OUTPUT_EVENTS = {"Present", "FrameReady", "Discarded"}
OUTPUT_CPU_STAGES = {"DeadlineWait", "DisplayCapacityWait"}
BATCH_EVENTS = {"Submitted", "Ready", "Cancelled"}
SOURCE_EVENTS = BATCH_EVENTS | OUTPUT_EVENTS | {"Source", "Cpu", "Gpu", "GpuUnavailable"}
PROVIDER_EVENTS = {"ProviderOutput", "ProviderDeadline", "XessSleep", "XessBind", "XessPresent"}
READINESS = {0: "ready", 1: "pending", 2: "invalid", 3: "suppressed", 4: "expired"}
FLOAT_FIELDS = {"ms", "entryDeviationMs", "returnDeviationMs"}
STRING_FIELDS = {"event", "stage", "frameKind"}
NUMERIC_FIELDS = (set(NATIVE_COLUMNS) | {"presentCallHost"}) - FLOAT_FIELDS - STRING_FIELDS
OBSERVATION_NOTES = [
    "App Present intervals and provider calls describe software submissions. Physical display is unmeasured.",
    "CPU durations are elapsed wall time. Observed readiness is an upper bound and can be sampled after an application submission.",
    "GPU durations use D3D12 timestamp spans. GPU start/end ticks belong to the GPU clock. Stage samples can overlap and have gaps.",
    "A source is an accepted engine frame identity. Capture ingress covers accepted observations, including WGC packet-read stamps. The upstream source can produce additional frames.",
    "Empty stage fields mean unobserved. A batch, source, or trace can have only part of its expected observations.",
    "Anomaly labels indicate budget breaches or spikes against a same-scope reference. Inspect linked evidence for attribution.",
]


def number(value, label, numeric_type=int):
    try:
        converted = numeric_type(value)
        if numeric_type is float and not math.isfinite(converted):
            raise ValueError
        return converted
    except (ValueError, TypeError, OverflowError) as error:
        raise ValueError(f"Invalid {label}: {value!r}") from error


def normalize_event(row, index):
    event = {key: value for key, value in row.items() if value not in (None, "")}
    if not event.get("event") or "host" not in event:
        raise ValueError(f"Event {index} is missing its kind or host timestamp")
    for key in event.keys() & NUMERIC_FIELDS:
        event[key] = number(event[key], f"{key} in event {index}")
    for key in event.keys() & FLOAT_FIELDS:
        event[key] = number(event[key], f"{key} in event {index}", float)
    if event["host"] <= 0:
        raise ValueError(f"Nonpositive host clock in event {index}")
    event["event_number"] = index
    return event


def read_native(lines):
    metadata = {}
    index = 0
    for line in lines:
        if not line.startswith("#"):
            header = line
            break
        key, separator, value = line[1:].strip().partition("=")
        if not separator or key in metadata:
            raise ValueError(f"Invalid trace metadata: {line.strip()}")
        metadata[key] = value.strip()
    else:
        raise ValueError("No trace column header")
    if metadata.get("schema") != SCHEMA:
        raise ValueError(f"Unsupported schema: {metadata.get('schema', '<missing>')}")
    for key in ("pid", "host_ticks_per_second", "started_host", "ended_host", "requested_ms",
                "recorded", "dropped", "capacity"):
        metadata[key] = number(metadata.get(key), key)
    if (metadata["host_ticks_per_second"] <= 0 or metadata["started_host"] < 0
            or (metadata["recorded"] > 0 and metadata["started_host"] == 0)
            or metadata["ended_host"] < metadata["started_host"] or metadata["requested_ms"] <= 0
            or metadata["pid"] <= 0 or metadata["capacity"] <= 0 or metadata["dropped"] < 0
            or not 0 <= metadata["recorded"] <= metadata["capacity"]):
        raise ValueError("Invalid trace time window or record counters")
    if metadata.get("termination") not in ("duration", "shutdown"):
        raise ValueError("Capture termination is missing or unknown")
    reader = csv.reader(chain((header,), lines), strict=True)
    columns = next(reader)
    missing = set(NATIVE_COLUMNS) - set(columns)
    if len(columns) != len(set(columns)) or missing:
        raise ValueError(f"Trace column header is invalid. Missing: {', '.join(sorted(missing))}")
    events = []
    completed = False
    for row in reader:
        if not row:
            continue
        if completed:
            raise ValueError("Unexpected data after the trace completion record")
        if row == ["# complete=1"]:
            completed = True
            continue
        if len(row) != len(columns):
            raise ValueError(f"Invalid column count at CSV line {reader.line_num}")
        index += 1
        events.append(normalize_event(dict(zip(columns, row)), index))
    if len(events) != metadata["recorded"]:
        raise ValueError(f"Incomplete trace: declared {metadata['recorded']} events, read {len(events)}")
    if not completed:
        raise ValueError("Incomplete trace: missing final # complete=1 record")
    metadata["complete_marker"] = completed
    return metadata, events


def read_legacy(lines):
    header = None
    events = []
    for line in lines:
        match = LEGACY_HEADER.fullmatch(line.strip())
        if match:
            if header:
                raise ValueError("Provide a single diagnostic snapshot")
            header = match
        elif line.startswith("event="):
            events.append(normalize_event(dict(token.split("=", 1) for token in line.split()),
                                          len(events) + 1))
    if not header:
        raise ValueError("Expected a Veyra diagnostics snapshot or private Veyra CSV trace")
    recorded, capacity, overwritten = map(int, header.groups())
    if recorded != len(events):
        raise ValueError(f"Incomplete diagnostic snapshot: declared {recorded} events, read {len(events)}")
    metadata = {"schema": "legacy-diagnostic-report", "host_ticks_per_second": 10_000_000,
                "recorded": recorded, "capacity": capacity, "overwritten": overwritten}
    return metadata, events


def read_trace(path):
    with path.open(encoding="utf-8-sig", newline="") as file:
        first = file.readline()
        return (read_native if first.startswith("#") else read_legacy)(chain((first,), file))


def identity(event):
    return tuple(event.get(key, 0) for key in ("session", "epoch", "revision", "source"))


def measured_delta(begin, end, scale):
    if begin and end and 0 < begin <= end:
        return (end - begin) * scale
    return None


def unique_stamp(events, field):
    values = {event.get(field, 0) for event in events}
    values.discard(0)
    return next(iter(values)) if len(values) == 1 else None


def frame_record(key, role, timestamp, host_origin, scale, **fields):
    session, epoch, revision, source = key
    return dict(
        role=role, source_key="/".join(map(str, key)), session=session, epoch=epoch, revision=revision,
        source=source, host=timestamp, time_ms=(timestamp - host_origin) * scale, frame_kind="unknown",
        metrics_ms={}, stages=[], events=[], anomalies=[], issues=[], **fields,
    )


def set_metric(row, name, value):
    if value is not None:
        row["metrics_ms"][name] = value


def stage_sample(event):
    domain = "cpu" if event["event"] == "Cpu" else "gpu"
    stage = event.get("stage")
    if not stage and domain == "gpu":
        index = event.get("detail", -1)
        stage = GPU_STAGES[index] if 0 <= index < len(GPU_STAGES) else f"Stage {index}"
    if not stage:
        return None
    measured = event["event"] != "GpuUnavailable" and event.get("ms", -1) >= 0
    sample = dict(domain=domain, stage=stage, ms=event["ms"] if measured else None,
                  status="measured" if measured else "unavailable",
                  observed_host=event["host"], event=event["event_number"])
    if event.get("batch"):
        sample["batch"] = event["batch"]
    if domain == "gpu" and event.get("gpuFrequency", 0) > 0:
        sample.update(gpu_begin=event.get("gpuBegin"), gpu_end=event.get("gpuEnd"),
                      gpu_ticks_per_second=event["gpuFrequency"])
    return sample


def output_kind(event, legacy):
    kind = event.get("frameKind", "unknown").lower()
    if legacy and "detail" in event:
        return "generated" if event["detail"] else "real"
    return kind if kind in ("real", "generated", "hold") else "unknown"


def source_records(sources, host_origin, scale, legacy):
    for key, items in sources.items():
        items.sort(key=lambda item: item["host"])
        inputs = [event for event in items if event["event"] == "Source"]
        submits = [event for event in items if event["event"] == "Submitted"]
        presents = [event for event in items if event["event"] == "Present"]
        # A stage-only record can survive a bounded snapshot. Keep it as
        # unanchored evidence so it cannot silently extend frame coverage.
        anchor = (inputs or submits or presents or items)[0]
        row = frame_record(key, "source", anchor["host"], host_origin, scale,
                           anchored=bool(inputs or submits or presents))
        if inputs:
            row["frame_kind"] = output_kind(inputs[0], False)
            if all(event.get("count", 0) for event in inputs):
                row["frame_kind"] = "hold"
            row["input_kind"] = inputs[0].get("stage") or None
        row["events"] = [event["event_number"] for event in items]
        row["stages"] = [stage for event in items if event["event"] in ("Cpu", "Gpu", "GpuUnavailable")
                         if event["event"] != "Cpu" or event.get("stage") not in OUTPUT_CPU_STAGES
                         if (stage := stage_sample(event)) is not None]
        row["batches"] = sorted({event["batch"] for event in items if event.get("batch")})
        batch_readies = [event for event in items if event["event"] == "Ready"]
        row["source_observations"] = len(inputs)
        row["graph_submissions"] = len(submits)
        row["fg_evaluated"] = sum(event.get("count", 0) for event in submits)
        row["fg_skipped_before_eval"] = sum(event.get("detail", 0) for event in submits)
        row["fg_valid"] = sum(event.get("count", 0) for event in batch_readies)
        row["fg_invalid"] = sum(event.get("detail", 0) for event in batch_readies)
        row["cancelled_output_count"] = sum(event.get("count", 0) for event in items
                                            if event["event"] == "Cancelled")
        arrival = unique_stamp(inputs, "arrivalHost")
        decoded = unique_stamp(inputs if inputs else presents, "decodedHost")
        process = (unique_stamp(inputs, "processHost") or unique_stamp(submits, "processHost")
                   or unique_stamp(presents, "processHost"))
        row["arrival_host"], row["decoded_host"], row["process_host"] = arrival, decoded, process
        row["source_a"] = unique_stamp(inputs, "sourceA")
        row["source_a_host"] = unique_stamp(inputs, "sourceAHost")
        row["ingress_host"] = arrival if row["frame_kind"] != "hold" else None
        if not inputs:
            row["issues"].append("source_ingress_unobserved")
        if not submits:
            row["issues"].append("graph_submission_unobserved")
        elif len(submits) == 1:
            set_metric(row, "graph_submit", submits[0].get("ms"))
        if len(batch_readies) == 1:
            set_metric(row, "graph_to_observed_ready", batch_readies[0].get("ms"))
        set_metric(row, "ingress_to_process", measured_delta(arrival, process, scale))
        if not legacy:
            set_metric(row, "decode_ready_to_process", measured_delta(decoded, process, scale))
        yield row


def output_records(outputs, host_origin, scale, legacy):
    for key, items in outputs.items():
        items.sort(key=lambda item: item["host"])
        ready_observations = [event["host"] for event in items if event["event"] == "FrameReady"]
        terminal = [event for event in items if event["event"] in {"Present", "Discarded"}]
        anchor = terminal or [event for event in items if event["event"] == "FrameReady"][:1] or items[:1]
        for event in anchor:
            present = event["event"] == "Present"
            discarded = event["event"] == "Discarded"
            is_ready = event["event"] == "FrameReady"
            begin = event.get("presentBeginHost") or None
            call = event.get("presentCallHost") or None
            end = event.get("presentEndHost") or (event["host"] if present else None)
            ready = event.get("readyHost") or (
                ready_observations[0] if len(ready_observations) == 1 else None
            )
            row = frame_record(
                key[:4], "output", begin or event["host"], host_origin, scale,
                anchored=present or discarded or is_ready,
                batch=key[4], subframe=key[5], pts_100ns=event.get("pts"), queue_depth=event.get("queueDepth"),
                status=("submitted" if present else "discarded" if discarded else
                        "ready_only" if is_ready else "stage_only"),
                present_begin_host=begin, present_call_host=call, present_end_host=end, ready_host=ready,
                arrival_host=event.get("arrivalHost") or None,
                deadline_host=event.get("deadlineHost") or None,
                source_a=event.get("sourceA") or None, source_a_host=event.get("sourceAHost") or None,
                decoded_host=event.get("decodedHost") or None, process_host=event.get("processHost") or None,
            )
            row["frame_kind"] = output_kind(event, legacy)
            row["stages"] = [sample for item in items if item["event"] == "Cpu"
                             if (sample := stage_sample(item)) is not None]
            row["events"] = [event["event_number"]] + [other["event_number"] for other in items
                                                      if other not in terminal and other is not event]
            if len(terminal) > 1:
                row["issues"].append("repeated_output_identity")
            if not present and not discarded:
                row["issues"].append("output_disposition_unobserved")
            if discarded:
                row["discard_reason"] = event.get("stage") or READINESS.get(event.get("count"), "unknown")
                set_metric(row, "discard_lateness", event.get("ms"))
            if present:
                set_metric(row, "present_api", measured_delta(begin, end, scale))
                if begin and call and end and begin <= call <= end:
                    set_metric(row, "present_prepare_wall", measured_delta(begin, call, scale))
                    set_metric(row, "present_sink_wall", measured_delta(call, end, scale))
                set_metric(row, "observed_ready_to_present", measured_delta(ready, begin, scale))
                set_metric(row, "process_to_present_return", measured_delta(row["process_host"], end, scale))
                set_metric(row, "ingress_to_present_return", measured_delta(row["arrival_host"], end, scale))
                set_metric(row, "source_a_to_present_return", measured_delta(row["source_a_host"], end, scale))
                if not ready:
                    row["issues"].append("output_readiness_unobserved")
                elif begin and ready > begin:
                    row["issues"].append("readiness_sampled_after_present_entry")
                if event.get("mediaDeviationValid"):
                    set_metric(row, "entry_deviation", event.get("entryDeviationMs"))
                    set_metric(row, "return_deviation", event.get("returnDeviationMs"))
            yield row


def add_intervals(rows, clock, name, scale, boundaries):
    previous = None
    for row in rows:
        if not row.get(clock):
            previous = None
            continue
        if previous:
            old_scope = (previous["session"], previous["epoch"], previous["revision"])
            scope = (row["session"], row["epoch"], row["revision"])
            duration = measured_delta(previous[clock], row[clock], scale)
            if scope != old_scope:
                boundaries.append(dict(metric=name, previous_scope=old_scope, scope=scope,
                                       old_phase=previous["phase"], phase=row["phase"],
                                       prior_host=previous[clock], current_host=row[clock], interval_ms=duration))
            elif duration is None:
                row["issues"].append(f"{clock}_nonmonotonic")
            elif previous["phase"] == row["phase"]:
                set_metric(row, name, duration)
        previous = row


def distribution(values):
    data = sorted(values)
    if not data:
        return {"samples": 0}
    # Linear quantiles make small data sets comparable; counts always accompany them.
    def quantile(fraction):
        index = fraction * (len(data) - 1)
        lower, upper = math.floor(index), math.ceil(index)
        return data[lower] + (data[upper] - data[lower]) * (index - lower)

    return {name: round(value, 6) for name, value in
            (("samples", len(data)), ("min", data[0]), ("mean", statistics.mean(data)),
             ("p50", quantile(.5)), ("p95", quantile(.95)), ("p99", quantile(.99)), ("max", data[-1]))}


def sample_groups(rows):
    all_groups, scoped_groups = defaultdict(list), defaultdict(list)
    for row in rows:
        if row["phase"] != "steady":
            continue
        scope = (row["session"], row["epoch"], row["revision"], row["role"])
        samples = list(row["metrics_ms"].items())
        samples += [(f'{st["domain"]}/{st["stage"]}', st["ms"]) for st in row["stages"]
                    if st["ms"] is not None]
        for name, value in samples:
            all_groups[f'{row["role"]}/{name}'].append(value)
            scoped_groups[scope, name].append((row, value))
    return all_groups, scoped_groups


def flag_anomalies(scoped_groups, input_fps, output_fps):
    budgets = {"ingress_interval": 1500 / input_fps if input_fps else None,
               "present_entry_interval": 1500 / output_fps if output_fps else None}
    for (scope, name), samples in scoped_groups.items():
        if name in {"entry_deviation", "return_deviation", "discard_lateness"}:
            continue
        values = [value for _, value in samples]
        threshold = None
        if len(values) >= 16:
            center = statistics.median(values)
            mad = statistics.median(abs(value - center) for value in values)
            threshold = center + max(0.25, 0.5 * center, 6 * 1.4826 * mad)
        budget = budgets.get(name)
        if name == "source_observation_interval" and (scope, "ingress_interval") not in scoped_groups:
            budget = budgets["ingress_interval"]
        for row, value in samples:
            reasons = []
            if threshold is not None and value > threshold:
                reasons.append("same_scope_spike")
            if budget is not None and value > budget:
                reasons.append("target_interval")
            if reasons:
                row["anomalies"].append(dict(metric=name, ms=value,
                    reference_threshold_ms=threshold, target_threshold_ms=budget, reasons=reasons))


def summarize_scope(rows, scoped):
    counters = defaultdict(Counter)
    for row in rows:
        key = (row["session"], row["epoch"], row["revision"], row["role"])
        counters[key][f'{row["phase"]}_rows'] += 1
        if row["phase"] == "steady":
            counters[key]["anomaly_rows"] += bool(row["anomalies"])
            if row["role"] == "output":
                counters[key][row["status"]] += 1
    scopes = {key: dict(session=key[0], epoch=key[1], revision=key[2], role=key[3],
                        counts=dict(counts), metrics={}) for key, counts in sorted(counters.items())}
    for (key, name), values in scoped.items():
        scopes[key]["metrics"][name] = distribution([value for _, value in values])
    return list(scopes.values())


def analyze(meta, events, input_fps=None, output_fps=None, warmup_seconds=2.0, tail_seconds=0.0):
    if not events:
        raise ValueError("Complete capture contains no events. Start a capture during active Veyra playback.")
    ticks = meta["host_ticks_per_second"]
    scale = 1000 / ticks
    legacy = meta["schema"] != SCHEMA
    origin = meta.get("started_host", min(event["host"] for event in events))
    sources, outputs = defaultdict(list), defaultdict(list)
    ancillary, skipped = [], []
    for event in events:
        key, kind = identity(event), event["event"]
        if kind == "SourceSkipped" and key[0] and key[-1]:
            row = frame_record(key, "skipped", event["host"], origin, scale, anchored=True,
                               status="skipped", discard_reason=event.get("stage") or "unknown",
                               arrival_host=event.get("arrivalHost") or None,
                               decoded_host=event.get("decodedHost") or None)
            row["frame_kind"] = output_kind(event, legacy=False)
            row["events"].append(event["event_number"])
            skipped.append(row)
            continue
        if kind == "Cpu" and event.get("stage") in OUTPUT_CPU_STAGES and key[0] and key[-1]:
            outputs[key + (event.get("batch", 0), event.get("detail", 0))].append(event)
            sources[key].append(event)
            continue
        if kind not in SOURCE_EVENTS or not key[0] or not key[-1]:
            ancillary.append(event)
            continue
        sources[key].append(event)
        if kind in OUTPUT_EVENTS:
            outputs[key + (event.get("batch", 0), event.get("detail", 0))].append(event)

    rows = sorted(chain(source_records(sources, origin, scale, legacy),
                        output_records(outputs, origin, scale, legacy), skipped), key=lambda row: row["host"])
    if not rows:
        raise ValueError("No frame-linked events in this trace")
    session_start, session_end = {}, {}
    for row in rows:
        if row["anchored"]:
            session_start.setdefault(row["session"], row["host"])
            end = row.get("present_end_host") or row["host"]
            session_end[row["session"]] = max(session_end.get(row["session"], end), end)
    source_by_key = {row["source_key"]: row for row in rows if row["role"] == "source"}
    for row in rows:
        start = session_start.get(row["session"], row["host"])
        end = session_end.get(row["session"], row["host"])
        row["phase"] = ("unanchored" if not row["anchored"] else
                        "startup" if (row["host"] - start) / ticks < warmup_seconds else
                        "tail" if tail_seconds and (end - row["host"]) / ticks < tail_seconds else "steady")
        if row["role"] == "output":
            source = source_by_key.get(row["source_key"])
            has_ingress = source and source["source_observations"]
            if not has_ingress:
                row["issues"].append("source_ingress_unobserved")
            if not source or not source["graph_submissions"]:
                row["issues"].append("graph_submission_unobserved")
            row["source_stage_count"] = len(source["stages"]) if source else 0

    boundaries = []
    add_intervals([row for row in rows if row["role"] == "source" and row["anchored"]],
                  "ingress_host", "ingress_interval", scale, boundaries)
    accepted = [row for row in rows if row["role"] == "source" and row["anchored"]
                and row["frame_kind"] != "hold"]
    add_intervals(accepted, "host", "source_observation_interval", scale, boundaries)
    present = [row for row in rows if row.get("status") == "submitted"]
    add_intervals(present, "present_begin_host", "present_entry_interval", scale, boundaries)
    add_intervals(present, "present_end_host", "present_return_interval", scale, boundaries)
    add_intervals([row for row in present if row["frame_kind"] == "real"],
                  "present_begin_host", "real_present_entry_interval", scale, boundaries)
    all_groups, scoped = sample_groups(rows)
    flag_anomalies(scoped, input_fps, output_fps)
    for row in rows:
        if row["phase"] == "steady" and row.get("discard_reason") in {"Deadline", "expired"}:
            row["anomalies"].append(dict(metric="discard_lateness", ms=row["metrics_ms"].get("discard_lateness"),
                                         reasons=["output_deadline_expired"]))
    annotate_gap_evidence(rows, source_by_key, scale)
    first, last = min(event["host"] for event in events), max(event["host"] for event in events)
    reset_events = [event for event in ancillary if event["event"] == "Reset"]
    provider = [event for event in ancillary if event["event"] in PROVIDER_EVENTS]
    other = [event for event in ancillary if event["event"] not in PROVIDER_EVENTS | {"Reset"}]
    counts = Counter(row["phase"] for row in rows)
    event_counts = Counter(event["event"] for event in events)
    timing = {name: distribution(values) for name, values in sorted(all_groups.items())}
    cadence = {name: round(1000 / timing[name]["mean"], 3) if timing[name]["mean"] > 0 else None
               for name in ("source/ingress_interval", "source/source_observation_interval",
                            "output/present_entry_interval", "output/real_present_entry_interval")
               if name in timing}
    estimate_loss = meta.get("dropped", 0) + meta.get("overwritten", 0)
    if not counts["steady"]:
        evidence = "No steady frame rows in the configured analysis window."
    elif estimate_loss:
        evidence = "Trace events were lost. Distributions describe observed frames only."
    elif legacy:
        evidence = "Legacy snapshot covers the retained window. Use a private capture for explicit source ingress."
    elif meta["termination"] != "duration":
        evidence = "Application shutdown ended this trace before the requested duration."
    else:
        evidence = "Trace writer completed the requested interval. Counts reflect instrumented observations."
    return dict(
        report_version=1, capture=meta, observation_notes=OBSERVATION_NOTES, timing_ms=timing,
        budgets=dict(input_fps=input_fps, software_output_fps=output_fps,
                     warmup_seconds=warmup_seconds, trim_end_seconds=tail_seconds),
        coverage=dict(evidence=evidence, trace_event_loss=estimate_loss, first_event_host=first,
                      last_event_host=last, event_span_ms=(last - first) * scale, event_counts=dict(event_counts),
                      row_phase_counts=dict(counts), anomaly_rows=sum(bool(row["anomalies"]) for row in rows),
                      orphan_stage_only_sources=sum(not row["anchored"] for row in rows),
                      rows_with_issues=sum(bool(row["issues"]) for row in rows),
                      unassigned_events=len(other), provider_events=len(provider)),
        rate_fps=cadence, phase_boundaries=boundaries, scopes=summarize_scope(rows, scoped),
        steady_output_counts=dict(Counter(f'{row["frame_kind"]}/{row["status"]}' for row in rows
                                         if row["phase"] == "steady" and row["role"] == "output")),
        steady_input_skips=dict(Counter(row["discard_reason"] for row in skipped if row["phase"] == "steady")),
        reset_events=reset_events, provider_events=provider, unassigned_events=other, frames=rows,
    )


def annotate_gap_evidence(rows, source_by_key, scale):
    presents = [row for row in rows if row.get("status") == "submitted" and row.get("present_begin_host")]
    present_hosts = [row["present_begin_host"] for row in presents]
    for row in rows:
        if not row["anomalies"]:
            continue
        source = source_by_key[row["source_key"]]
        context = {"source_measurements_ms": dict(source["metrics_ms"]),
                   "nearby_present_wall_ms": None, "intersection_with_source_graph_wall_ms": None}
        latency = row["metrics_ms"].get("present_entry_interval")
        if latency is not None and row.get("present_begin_host"):
            now = row["present_begin_host"]
            index = bisect.bisect_left(present_hosts, now)
            if index:
                prior = presents[index - 1]
                gap_intersection = measured_delta(prior["present_begin_host"],
                    min(prior.get("present_end_host") or now, now), scale)
                context["nearby_present_wall_ms"] = gap_intersection
                if row.get("process_host") and source["metrics_ms"].get("graph_submit") is not None:
                    process = row["process_host"]
                    graph_end = process + round(source["metrics_ms"]["graph_submit"] / scale)
                    context["intersection_with_source_graph_wall_ms"] = max(
                        0, min(now, graph_end) - max(process, prior["present_begin_host"])) * scale
        row["anomaly_context"] = context


# Redistributed with the analyzer as a self-contained GPL-3.0-only template.
REPORT_TEMPLATE = r"""
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Veyra Frame Analyzer</title>
<style>
    :root {
        color-scheme: dark;
        --bg: #10151e;
        --panel: #181f2b;
        --text: #edf2f7;
        --muted: #a7b7c8;
        --line: #354256;
        --accent: #85d6d6;
        --alert: #ffc08d;
        font: 14px/1.5 system-ui, sans-serif;
    }
    * { box-sizing: border-box }
    body { margin: 0; background: var(--bg); color: var(--text) }
    main { max-width: 1600px; margin: auto; padding: 28px clamp(12px, 3vw, 44px) 40px }
    h1 { margin: 0; font-size: 28px; line-height: 1.15 }
    h2 { margin: 0; font-size: 16px }
    h3 { margin: 0; font-size: 13px }
    p { margin: 8px 0 }
    a { color: var(--accent) }
    strong, th { font-weight: 650 }
    code { font-family: ui-monospace, Consolas, monospace }
    header { display: flex; flex-wrap: wrap; align-items: flex-start; justify-content: space-between; gap: 12px }
    header p { color: var(--muted); overflow-wrap: anywhere }
    header nav { display: flex; gap: 16px }
    .cards { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 10px; margin: 18px 0 }
    .card, section, details.panel { background: var(--panel); border: 1px solid var(--line); border-radius: 8px }
    .card { padding: 9px 13px }
    .card span { display: block; color: var(--muted) }
    .card strong { font-size: 22px; font-variant-numeric: tabular-nums }
    section, details.panel { padding: 15px; margin: 12px 0 }
    .scroll { overflow: auto }
    table { border-collapse: collapse; width: 100%; white-space: nowrap; font-variant-numeric: tabular-nums }
    th, td { text-align: left; padding: 7px 9px; border-bottom: 1px solid var(--line) }
    th { color: var(--muted) }
    tbody tr:last-child td { border-bottom: none }
    td.number, th.number { text-align: right }
    .controls { display: flex; flex-wrap: wrap; align-items: end; gap: 10px 14px; margin: 12px 0 }
    label { display: grid; gap: 3px; color: var(--muted) }
    label.toggle { display: flex; align-items: center; padding: 6px 0 }
    input, select, button { font: inherit; color: var(--text); background: var(--bg); border: 1px solid var(--line); border-radius: 5px; padding: 5px 9px }
    input[type=checkbox] { accent-color: var(--accent) }
    button { cursor: pointer }
    button:disabled { opacity: .58; cursor: default }
    summary { cursor: pointer }
    .frame-link { color: var(--accent); border: none; padding: 0; background: transparent; text-decoration: underline }
    tr.selected { outline: 1px solid var(--accent); outline-offset: -1px }
    .flag { color: var(--alert) }
    .muted { color: var(--muted) }
    .taskline { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 6px }
    #chart { width: 100%; height: 210px; display: block; cursor: crosshair }
    #hover { min-height: 21px; color: var(--muted); font-variant-numeric: tabular-nums }
    .detail-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 300px), 1fr)); gap: 10px 16px; margin: 10px 0 }
    .detail-grid table { white-space: normal }
    .detail-grid h3 { margin-bottom: 4px }
    ul { padding-left: 22px }
    summary > span { font-weight: 650 }
    :focus-visible { outline: 2px solid var(--accent); outline-offset: 3px }
</style>
</head>
<body>
<main>
    <header>
        <div><h1>Veyra Frame Analyzer</h1><p id="capture"></p></div>
        <nav aria-label="Report downloads"><a href="frames.csv">Frame CSV</a><a href="summary.json">Analysis JSON</a></nav>
    </header>
    <div id="cards" class="cards"></div>
    <details class="panel" open>
        <summary><span>Capture coverage</span></summary>
        <p id="coverage"></p>
        <p id="settings" class="muted"></p>
        <ul id="notes"></ul>
    </details>
    <section aria-labelledby="timeline-label">
        <h2 id="timeline-label">Frame timeline</h2>
        <div class="controls">
            <label>Phase <select id="phase">
                <option value="">All</option><option value="steady">Steady</option><option value="startup">Startup</option>
                <option value="tail">End trim</option><option value="unanchored">Unanchored</option>
            </select></label>
            <label>Frame <select id="role">
                <option value="">All</option><option value="source">Source</option><option value="output">Output</option>
                <option value="skipped">Skipped input</option>
            </select></label>
            <label>Session / epoch / revision <select id="scope"><option value="">All</option></select></label>
            <label>Source ID <input id="search" type="search" placeholder="e.g. 124"></label>
            <label class="toggle"><input id="only-anomalies" type="checkbox"> Anomalies</label>
            <label>Chart metric <select id="metric"></select></label>
        </div>
        <canvas id="chart" aria-label="Observed frame metric over elapsed time. Use the frame table to inspect a frame by keyboard."></canvas>
        <div id="hover"></div>
        <div class="scroll"><table aria-label="Observed frames">
            <thead><tr>
                <th>Time (ms)</th><th>Frame</th><th>Scope</th><th>Source</th><th>Batch</th><th>Subframe</th>
                <th>Status</th><th>Gap (ms)</th><th>Graph / Present (ms)</th><th>Flags</th>
            </tr></thead><tbody id="frames"></tbody>
        </table></div>
        <div class="taskline">
            <p id="page" class="muted"></p>
            <div><button id="prev" type="button">Previous</button> <button id="next" type="button">Next</button></div>
        </div>
    </section>
    <section id="frame-details" aria-labelledby="detail-title">
        <h2 id="detail-title">Frame detail</h2>
        <p id="detail-message" class="muted">Select a frame from the timeline or table.</p>
        <div class="detail-grid" id="detail-grid"></div>
    </section>
    <details class="panel">
        <summary><span>Steady stage and cadence distributions</span></summary>
        <p class="muted">Each row is one measurement population. Scopes and frame observations are available in Analysis JSON.</p>
        <div class="scroll"><table aria-label="Metric distributions">
            <thead><tr>
                <th>Measurement</th><th>Samples</th><th>P50 (ms)</th><th>P95 (ms)</th><th>P99 (ms)</th><th>Max (ms)</th>
            </tr></thead><tbody id="distribution"></tbody>
        </table></div>
    </details>
    <details class="panel">
        <summary><span>Resets and scope transitions</span></summary>
        <p id="transitions" class="muted"></p>
        <div class="scroll"><table aria-label="Largest boundary intervals">
            <thead><tr>
                <th>Metric</th><th>Previous → current scope</th><th>Time (ms)</th><th>Gap (ms)</th>
            </tr></thead><tbody id="boundaries"></tbody>
        </table></div>
    </details>
</main>
<script id="analysis-data" type="application/json">__VEYRA_REPORT_JSON__</script>
<script>
'use strict'
const model = JSON.parse(document.getElementById('analysis-data').textContent)
const el = id => document.getElementById(id)
const create = (tag, content, cls) => {
    const node = document.createElement(tag)
    if (content !== undefined && content !== null) node.textContent = content
    if (cls) node.className = cls
    return node
}
const fmt = value => Number.isFinite(value) ? value.toFixed(3) : '—'
const scopeKey = row => [row.session, row.epoch, row.revision].join('/')
const metrics = {
    ingress_interval: 'Accepted input ingress interval',
    source_observation_interval: 'Engine input observation interval',
    present_entry_interval: 'Application Present entry interval',
    present_return_interval: 'Application Present return interval',
    real_present_entry_interval: 'Real-frame Present entry interval',
    graph_submit: 'Graph submission wall time',
    graph_to_observed_ready: 'Process start → observed batch ready',
    ingress_to_process: 'Accepted input ingress → process',
    decode_ready_to_process: 'Decode ready → process',
    present_api: 'Application Present path wall time',
    present_prepare_wall: 'Present preparation wall time',
    present_sink_wall: 'Present sink and return wall time',
    observed_ready_to_present: 'Observed ready → Present entry',
    process_to_present_return: 'Process start → Present return',
    ingress_to_present_return: 'Input ingress → Present return',
    source_a_to_present_return: 'Source A observation → Present return',
    entry_deviation: 'Present entry deviation',
    return_deviation: 'Present return deviation',
    discard_lateness: 'Discard deadline lateness'
}
const metricLabel = key => metrics[key] || key.replace(/^cpu\//, 'CPU · ').replace(/^gpu\//, 'GPU · ')
const body = el('frames')
const pageSize = 60
const canvas = el('chart')
const context = canvas.getContext('2d')
const sourceByKey = new Map()
let page = 0, selected = null, activeRows = [], plotted = []
let sweep = null
const coverage = model.coverage
const begin = coverage.first_event_host
const scale = 1000 / model.capture.host_ticks_per_second
const origin = model.capture.started_host || begin

el('capture').textContent = model.source_file
el('coverage').textContent = [
    coverage.evidence, coverage.event_counts ? Object.values(coverage.event_counts).reduce((a,b)=>a+b,0) + ' events' : '',
    coverage.trace_event_loss + ' events lost', 'observed span ' + (coverage.event_span_ms / 1000).toFixed(2) + ' s',
    coverage.rows_with_issues + ' frame rows with partial or qualified observations',
    coverage.provider_events + ' provider observations', coverage.unassigned_events + ' unassigned events'
].filter(Boolean).join(' · ')
el('settings').textContent = [
    model.budgets.warmup_seconds + ' s session warmup',
    model.budgets.trim_end_seconds + ' s end trim',
    model.budgets.input_fps ? model.budgets.input_fps + ' FPS accepted input target' : 'automatic input reference',
    model.budgets.software_output_fps ? model.budgets.software_output_fps + ' FPS software output target' : 'automatic output reference'
].join(' · ')
model.observation_notes.forEach(note => el('notes').append(create('li', note)))
function addCard(label, value) {
    const card = create('div', null, 'card')
    card.append(create('span', label), create('strong', value))
    el('cards').append(card)
}
addCard('Steady frame rows', coverage.row_phase_counts.steady || 0)
addCard('Rows with anomalies', coverage.anomaly_rows)
const presentRate = model.rate_fps['output/present_entry_interval']
const inputRate = model.rate_fps['source/ingress_interval']
addCard('Accepted ingress / s', Number.isFinite(inputRate) ? inputRate.toFixed(2) : '—')
addCard('Application Present entries / s', Number.isFinite(presentRate) ? presentRate.toFixed(2) : '—')

function appendRow(target, values) {
    const row = create('tr')
    values.forEach(value => {
        const cell = create('td')
        if (value instanceof Node) cell.append(value)
        else cell.textContent = value ?? '—'
        row.append(cell)
    })
    target.append(row)
    return row
}
for (const [key, timing] of Object.entries(model.timing_ms)) {
    const [role, ...rest] = key.split('/')
    appendRow(el('distribution'), [role + ' · ' + metricLabel(rest.join('/')), timing.samples,
        fmt(timing.p50), fmt(timing.p95), fmt(timing.p99), fmt(timing.max)])
}
el('transitions').textContent = model.reset_events.length + ' reset records and ' +
    model.phase_boundaries.length + ' cadence transitions across session, epoch or revision boundaries. ' +
    'Their intervals are recorded separately. The table lists up to 30 largest transitions.'
;[...model.phase_boundaries].sort((a, b) => (b.interval_ms || 0) - (a.interval_ms || 0)).slice(0, 30)
    .forEach(row => appendRow(el('boundaries'), [metricLabel(row.metric),
        row.previous_scope.join('/') + ' → ' + row.scope.join('/'),
        fmt((row.current_host - origin) * scale), fmt(row.interval_ms)]))

const scopes = [...new Set(model.frames.map(scopeKey))]
for (const scope of scopes) {
    const option = create('option', scope)
    option.value = scope
    el('scope').append(option)
}
const chartMetrics = new Set()
for (const row of model.frames) {
    if (row.role === 'source') sourceByKey.set(row.source_key, row)
    Object.keys(row.metrics_ms).forEach(metric => chartMetrics.add(metric))
    row.stages.forEach(stage => {
        if (stage.ms !== null) chartMetrics.add(stage.domain + '/' + stage.stage)
    })
}
for (const metric of chartMetrics) {
    const option = create('option', metricLabel(metric))
    option.value = metric
    el('metric').append(option)
}
if (chartMetrics.has('present_entry_interval')) el('metric').value = 'present_entry_interval'
if (coverage.row_phase_counts.steady) el('phase').value = 'steady'

function filterRows() {
    const search = el('search').value.trim()
    return model.frames.filter(row => (!el('phase').value || row.phase === el('phase').value)
        && (!el('role').value || row.role === el('role').value)
        && (!el('scope').value || scopeKey(row) === el('scope').value)
        && (!el('only-anomalies').checked || row.anomalies.length)
        && (!search || String(row.source) === search))
}
function getValue(row, key) {
    if (key in row.metrics_ms) return row.metrics_ms[key]
    const values = row.stages.filter(st => st.domain + '/' + st.stage === key && st.ms !== null)
    return values.length ? Math.max(...values.map(st => st.ms)) : null
}
function filterAndDraw() {
    page = 0
    activeRows = filterRows()
    drawPage()
    drawChart()
}
function drawPage() {
    body.replaceChildren()
    const start = page * pageSize
    const stop = Math.min(start + pageSize, activeRows.length)
    for (const row of activeRows.slice(start, stop)) {
        const pick = create('button', fmt(row.time_ms), 'frame-link')
        pick.type = 'button'
        pick.onclick = () => showDetail(row)
        const interval = row.metrics_ms.present_entry_interval ?? row.metrics_ms.ingress_interval ??
            row.metrics_ms.source_observation_interval
        const flags = create('span', row.anomalies.length ? row.anomalies.length + ' anomalies' :
            row.issues.length ? row.issues.length + ' observations to review' : '—', row.anomalies.length ? 'flag' : 'muted')
        const item = appendRow(body, [pick, row.role + ' · ' + row.frame_kind, scopeKey(row), row.source,
            row.batch, row.subframe, row.status || row.phase, fmt(interval),
            fmt(row.metrics_ms.graph_submit ?? row.metrics_ms.present_api), flags])
        if (selected === row) item.classList.add('selected')
    }
    el('page').textContent = activeRows.length ? (start + 1) + '–' + stop + ' of ' + activeRows.length : 'No matching frame rows'
    el('prev').disabled = page === 0
    el('next').disabled = stop >= activeRows.length
}
function niceLimits(series) {
    let xMin = Infinity, xMax = -Infinity, yMin = 0, yMax = 1
    for (const {row, val} of series) {
        xMin = Math.min(xMin, row.time_ms)
        xMax = Math.max(xMax, row.time_ms)
        yMin = Math.min(yMin, val)
        yMax = Math.max(yMax, val)
    }
    return [xMin, Math.max(xMax, xMin + 1), yMin, yMax]
}
function drawChart() {
    const selectedMetric = el('metric').value
    const width = canvas.clientWidth
    const height = canvas.clientHeight
    const dpi = window.devicePixelRatio || 1
    canvas.width = Math.round(width * dpi)
    canvas.height = Math.round(height * dpi)
    context.setTransform(dpi,0,0,dpi,0,0)
    plotted = []
    sweep = null
    const series = activeRows.map(row => ({row, val: getValue(row, selectedMetric)}))
        .filter(({val}) => Number.isFinite(val))
    context.font = '12px system-ui'
    context.fillStyle = '#a7b7c8'
    if (!series.length || width < 80) {
        context.fillText('No matching observations for this metric', 6, 21)
        return
    }
    const [xMin, xMax, yMin, yMax] = niceLimits(series)
    const left = 61, top = 12, plotW = width - left - 10, plotH = height - 43
    const x = value => left + (value - xMin) * plotW / (xMax - xMin)
    const y = value => top + (yMax - value) * plotH / (yMax - yMin)
    for (let index = 0; index <= 4; index++) {
        const value = yMin + (yMax - yMin) * index / 4, drawY = y(value)
        context.strokeStyle = '#354256'
        context.beginPath()
        context.moveTo(left, drawY)
        context.lineTo(width - 10, drawY)
        context.stroke()
        context.fillStyle = '#a7b7c8'
        context.fillText(value.toFixed(2) + ' ms', 1, drawY + 4)
    }
    context.fillText((xMin / 1000).toFixed(2) + ' s', left, height - 9)
    context.textAlign = 'right'
    context.fillText((xMax / 1000).toFixed(2) + ' s', width - 10, height - 9)
    context.textAlign = 'left'
    for (const {row, val} of series) {
        const point = {row, val, x: x(row.time_ms), y: y(val)}
        const alert = row.anomalies.some(flag => flag.metric === selectedMetric)
        const radius = row === selected ? 4.5 : alert ? 2.9 : 1.6
        context.fillStyle = row === selected ? '#fff' : alert ? '#ffc08d' : '#85d6d6'
        context.beginPath()
        context.arc(point.x, point.y, radius, 0, Math.PI * 2)
        context.fill()
        plotted.push(point)
    }
    sweep = [left, left + plotW, top, top + plotH]
}
function pointAt(event) {
    if (!sweep || event.offsetX < sweep[0] - 8 || event.offsetX > sweep[1] + 8
        || event.offsetY < sweep[2] - 8 || event.offsetY > sweep[3] + 8) return null
    let distance = Infinity, nearest = null
    for (const point of plotted) {
        const delta = Math.hypot(point.x - event.offsetX, point.y - event.offsetY)
        if (delta < distance) { distance = delta; nearest = point }
    }
    return distance <= 20 ? nearest : null
}
function showDetail(row) {
    selected = row
    const source = sourceByKey.get(row.source_key)
    const detail = el('detail-grid')
    detail.replaceChildren()
    el('detail-message').textContent = [
        row.role + ' · ' + row.frame_kind + ' · ' + row.phase,
        'scope ' + scopeKey(row) + ', source ' + row.source,
        row.batch == null ? '' : 'batch ' + row.batch + ', subframe ' + row.subframe,
        fmt(row.time_ms) + ' ms'
    ].filter(Boolean).join(' | ')

    const times = create('div')
    times.append(create('h3', 'Timing'))
    const timingTable = create('table')
    for (const [name, duration] of Object.entries(row.metrics_ms)) {
        appendRow(timingTable, [metricLabel(name), fmt(duration) + ' ms'])
    }
    if (row.role === 'output' && source) {
        for (const name of ['ingress_interval', 'ingress_to_process', 'graph_submit', 'graph_to_observed_ready']) {
            if (name in source.metrics_ms) appendRow(timingTable, ['Source · ' + metricLabel(name), fmt(source.metrics_ms[name]) + ' ms'])
        }
    }
    if (row.anomaly_context?.nearby_present_wall_ms !== null && row.anomaly_context?.nearby_present_wall_ms !== undefined) {
        appendRow(timingTable, ['Prior Present overlapping gap', fmt(row.anomaly_context.nearby_present_wall_ms) + ' ms'])
    }
    if (row.anomaly_context?.intersection_with_source_graph_wall_ms !== null &&
        row.anomaly_context?.intersection_with_source_graph_wall_ms !== undefined) {
        appendRow(timingTable, ['Current source graph overlapping gap', fmt(row.anomaly_context.intersection_with_source_graph_wall_ms) + ' ms'])
    }
    times.append(timingTable)
    if (!timingTable.rows.length) times.append(create('p', 'No timing measurements for this frame.', 'muted'))

    const stages = create('div')
    stages.append(create('h3', 'Associated CPU and GPU stage samples'))
    const stageTable = create('table')
    const linkedStages = row.role === 'output' && source ? row.stages.concat(source.stages) : row.stages
    for (const stage of linkedStages) {
        appendRow(stageTable, [metricLabel(stage.domain + '/' + stage.stage),
            stage.ms === null ? 'unavailable' : fmt(stage.ms) + ' ms'])
    }
    stages.append(stageTable)
    if (!stageTable.rows.length) stages.append(create('p', 'No associated stage samples.', 'muted'))

    const flags = create('div')
    flags.append(create('h3', 'Observations'))
    const observations = create('ul')
    row.anomalies.forEach(f => observations.append(create('li',
        metricLabel(f.metric) + ' · ' + fmt(f.ms) + ' ms · ' + f.reasons.join(', ').replaceAll('_', ' '), 'flag')))
    row.issues.forEach(issue => observations.append(create('li', issue.replaceAll('_', ' '))))
    if (row.discard_reason) observations.append(create('li', 'Discard reason · ' + row.discard_reason))
    if (row.input_kind) observations.append(create('li', 'Input route · ' + row.input_kind))
    if (row.status) observations.append(create('li', 'Output status · ' + row.status))
    if (row.queue_depth != null) observations.append(create('li', 'Queue depth · ' + row.queue_depth))
    if (row.source_a) observations.append(create('li', 'Source A · ' + row.source_a))
    observations.append(create('li', 'Trace event numbers · ' + row.events.join(', ')))
    flags.append(observations)
    detail.append(times, stages, flags)
    drawPage()
    drawChart()
    el('frame-details').scrollIntoView({block: 'nearest'})
}
for (const id of ['phase','role','scope','only-anomalies']) el(id).onchange = filterAndDraw
el('search').oninput = filterAndDraw
el('metric').onchange = drawChart
el('prev').onclick = () => { page--; drawPage() }
el('next').onclick = () => { page++; drawPage() }
canvas.onmousemove = event => {
    const point = pointAt(event)
    el('hover').textContent = point ? fmt(point.row.time_ms) + ' ms · source ' + point.row.source +
        ' · ' + metricLabel(el('metric').value) + ' ' + fmt(point.val) + ' ms' : ''
}
canvas.onmouseleave = () => { el('hover').textContent = '' }
canvas.onclick = event => { const point = pointAt(event); if (point) showDetail(point.row) }
window.addEventListener('resize', drawChart)
filterAndDraw()
</script>
</body>
</html>
"""


def write_report(directory, report):
    directory.mkdir(parents=True, exist_ok=True)
    compact = json.dumps(report, ensure_ascii=False, allow_nan=False, separators=(",", ":"))
    directory.joinpath("summary.json").write_text(compact + "\n", encoding="utf-8")
    fields = ("time_ms phase role session epoch revision source batch subframe frame_kind status input_kind "
              "host pts_100ns arrival_host decoded_host process_host ready_host present_begin_host present_call_host "
              "present_end_host deadline_host source_a source_a_host queue_depth discard_reason "
              "source_observations graph_submissions fg_evaluated fg_skipped_before_eval fg_valid "
              "fg_invalid cancelled_output_count metrics_ms stages anomalies issues events").split()
    with directory.joinpath("frames.csv").open("w", newline="", encoding="utf-8-sig") as file:
        writer = csv.DictWriter(file, fields)
        writer.writeheader()
        for row in report["frames"]:
            writer.writerow({field: json.dumps(value, ensure_ascii=False, separators=(",", ":"))
                             if isinstance(value, (dict, list)) else value
                             for field in fields if (value := row.get(field)) is not None})
    escaped = compact.replace("&", r"\u0026").replace("<", r"\u003c").replace(">", r"\u003e")
    directory.joinpath("report.html").write_text(REPORT_TEMPLATE.replace("__VEYRA_REPORT_JSON__", escaped, 1),
                                                 encoding="utf-8")


def bounded_float(text, *, minimum):
    value = number(text, "analysis option", float)
    if value < minimum:
        raise argparse.ArgumentTypeError(f"Expected a number ≥ {minimum:g}")
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path, help="Private Veyra CSV or clipboard diagnostics .txt")
    parser.add_argument("--output-dir", type=Path, help="Default is a new <trace>.analysis subdirectory")
    parser.add_argument("--input-fps", "--target-fps", type=lambda text: bounded_float(text, minimum=.001),
                        help="Expected accepted source cadence for anomaly hints")
    parser.add_argument("--output-fps", type=lambda text: bounded_float(text, minimum=.001),
                        help="Expected software Present cadence for anomaly hints")
    parser.add_argument("--warmup", type=lambda text: bounded_float(text, minimum=0),
                        default=2.0, help="Exclude the first seconds of each session from steady statistics")
    parser.add_argument("--trim-end", type=lambda text: bounded_float(text, minimum=0), default=0.0,
                        help="Exclude the final seconds of each session from steady statistics")
    args = parser.parse_args()
    destination = args.output_dir or args.trace.with_name(args.trace.stem + ".analysis")
    try:
        metadata, events = read_trace(args.trace)
        report = analyze(metadata, events, args.input_fps, args.output_fps, args.warmup, args.trim_end)
        report["source_file"] = args.trace.name
        # Refuse an explicit output directory that would overwrite the input.
        if args.trace.resolve() in {destination.joinpath(name).resolve()
                                    for name in ("summary.json", "report.html", "frames.csv")}:
            raise ValueError("An output file would overwrite the input trace")
        write_report(destination, report)
    except (ValueError, OSError, csv.Error) as error:
        print(f"Cannot analyze frame trace: {error}", file=sys.stderr)
        return 2
    print(f"{len(report['frames'])} frame rows, {report['coverage']['anomaly_rows']} flagged.")
    print(destination.joinpath("report.html").resolve())
    return 0


if __name__ == "__main__":
    sys.exit(main())
