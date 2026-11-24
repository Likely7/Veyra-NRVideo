# SPDX-License-Identifier: GPL-3.0-only
"""Schema and per-frame correlation fixtures. All rows are synthetic."""

import csv
import io
import unittest

from veyra_frame_analyzer import NATIVE_COLUMNS, analyze, read_legacy, read_native


BASE = 70_000_000


def example_trace(complete=True, dropped=0):
    rows = []

    def event(kind, source, offset, **fields):
        row = dict(event=kind, host=BASE + offset, session=1, epoch=9, revision=4,
                   source=source, batch=source, frameKind="real")
        row.update(fields)
        rows.append(row)

    for source in range(1, 25):
        offset = source * 100_000
        arrival = BASE + offset
        present = arrival + 45_000
        event("Source", source, offset + 1000, stage="ScreenCapture", arrivalHost=arrival,
              processHost=arrival + 1500)
        event("Submitted", source, offset + 30_000, count=1, ms=1.2)
        event("Gpu", source, offset + 40_000, stage="Nr", ms=9 if source == 20 else 1.4,
              gpuBegin=54_000, gpuEnd=63_000, gpuFrequency=1_000_000)
        event("Cpu", source, offset + 44_000, stage="SlotWait", ms=.1)
        event("Cpu", source, offset + 44_500, detail=1, frameKind="generated", stage="DeadlineWait", ms=1.0)
        event("Present", source, offset + 42_000, detail=1, frameKind="generated", pts=source * 100_000,
              presentBeginHost=present - 30_000, presentEndHost=present - 3000, arrivalHost=arrival,
              sourceA=source-1, sourceAHost=arrival-100_000)
        event("Present", source, offset + 55_000, pts=source * 100_000 + 50_000,
              presentBeginHost=present, presentCallHost=present+5000, presentEndHost=present+10_000,
              readyHost=present+1000, arrivalHost=arrival)
    event("GpuUnavailable", 20, 2_000_100, stage="FgBatch")
    event("Discarded", 21, 2_150_000, detail=2, frameKind="generated", stage="Deadline", count=4, ms=3)
    event("SourceSkipped", 42, 2_401_000, epoch=0, batch=0, stage="ContentDedup")
    event("Source", 3, 2_600_000, epoch=10, arrivalHost=BASE+2_590_000)
    event("Present", 3, 2_650_000, epoch=10, presentBeginHost=BASE+2_649_000, presentEndHost=BASE+2_650_000)
    event("ProviderOutput", 0, 2_655_000, session=0, epoch=0, revision=0, ms=1.2)
    result = io.StringIO()
    result.write(f"# schema=veyra-frame-trace/1\n# pid=42\n# host_ticks_per_second=10000000\n"
                 f"# started_host={BASE}\n# ended_host={BASE+3_000_000}\n# requested_ms=300\n"
                 f"# termination=duration\n# recorded={len(rows)}\n# dropped={dropped}\n# capacity=1000\n")
    writer = csv.DictWriter(result, NATIVE_COLUMNS + ["presentCallHost"], lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
    if complete:
        result.write("# complete=1\n")
    return result.getvalue()


class TraceTests(unittest.TestCase):
    def test_incomplete_matching_count_rejected(self):
        with self.assertRaisesRegex(ValueError, "complete=1"):
            read_native(io.StringIO(example_trace(complete=False)))
        lines = example_trace().splitlines(keepends=True)
        del lines[-2]
        with self.assertRaisesRegex(ValueError, "declared"):
            read_native(io.StringIO("".join(lines)))

    def test_warmup_loss_scope_and_single_span(self):
        meta, events = read_native(io.StringIO(example_trace(dropped=2)))
        report = analyze(meta, events, input_fps=100, output_fps=200, warmup_seconds=.03, tail_seconds=.001)
        self.assertEqual(report["coverage"]["trace_event_loss"], 2)
        self.assertGreater(report["coverage"]["row_phase_counts"]["startup"], 0)
        sources = [row for row in report["frames"] if row["role"] == "source" and row["source"] == 20]
        self.assertEqual(len(sources), 1)
        row = sources[0]
        self.assertIn("gpu/Nr", {flag["metric"] for flag in row["anomalies"]})
        self.assertTrue(any(stage["stage"] == "FgBatch" and stage["ms"] is None for stage in row["stages"]))
        self.assertNotIn("source/gpu/FgBatch", report["timing_ms"])
        later = next(row for row in report["frames"] if row["role"] == "source" and row["epoch"] == 10)
        self.assertNotIn("ingress_interval", later["metrics_ms"])
        self.assertTrue(any(row["metric"] == "ingress_interval" for row in report["phase_boundaries"]))
        self.assertEqual(report["steady_input_skips"], {"ContentDedup": 1})
        self.assertEqual(report["provider_events"][0]["source"], 0)

    def test_subframe_association_and_clock_order(self):
        meta, events = read_native(io.StringIO(example_trace()))
        report = analyze(meta, events, warmup_seconds=0)
        outputs = [row for row in report["frames"] if row["role"] == "output" and row["source"] == 20]
        generated, real = outputs
        self.assertEqual(generated["subframe"], 1)
        self.assertEqual(generated["source_a"], 19)
        self.assertEqual(generated["frame_kind"], "generated")
        self.assertEqual([stage["stage"] for stage in generated["stages"]], ["DeadlineWait"])
        self.assertEqual(real["stages"], [])
        self.assertEqual(real["metrics_ms"]["present_prepare_wall"], .5)
        self.assertEqual(real["metrics_ms"]["present_sink_wall"], .5)
        self.assertNotIn("observed_ready_to_present", real["metrics_ms"])
        self.assertIn("readiness_sampled_after_present_entry", real["issues"])
        discarded = next(row for row in report["frames"] if row.get("status") == "discarded")
        self.assertEqual(discarded["anomalies"][0]["reasons"], ["output_deadline_expired"])

    def test_legacy_overwrite_and_gpu_stage_map(self):
        raw = ("Veyra diagnostics\r\nFrame trace: records=2 capacity=8192 overwritten=25\r\n"
               "event=Gpu host=5000000 session=1 revision=8 epoch=2 source=42 batch=0 detail=3 ms=1.2\r\n"
               "event=Present host=5001000 session=1 revision=8 epoch=2 source=42 batch=42 detail=1 "
               "presentBeginHost=5000020 presentEndHost=5001000\r\n")
        meta, events = read_legacy(io.StringIO(raw))
        report = analyze(meta, events, warmup_seconds=0)
        self.assertEqual(report["coverage"]["trace_event_loss"], 25)
        self.assertEqual(report["timing_ms"]["source/gpu/Nr"]["samples"], 1)
        output = next(row for row in report["frames"] if row["role"] == "output")
        self.assertEqual(output["frame_kind"], "generated")
        self.assertIsNone(output["arrival_host"])
        self.assertIn("source_ingress_unobserved", output["issues"])


if __name__ == "__main__":
    unittest.main()
