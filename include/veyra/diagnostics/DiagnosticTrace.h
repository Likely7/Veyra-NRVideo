// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Runtime-started frame trace for 性能诊断 (performance diagnostics).
//
// The environment-variable recorder (VEYRA_PRIVATE_FRAME_TRACE_FILE) keeps its
// in-memory v1 behaviour. This recorder streams the same CSV schema
// (veyra-frame-trace/1, see docs/PRIVATE_FRAME_TRACE.md) to disk while the
// session runs, so a 10-minute recording does not live in memory. The header
// is rewritten in place when the recording ends; a process that dies leaves a
// readable "<path>.partial" whose counters are still zero.
#include <chrono>
#include <cstdint>
#include <filesystem>

namespace veyra::diagnostics {

struct DiagnosticTraceStatus {
    bool active=false;
    // True once the final file (without .partial) was published.
    bool published=false;
    uint64_t recorded=0,dropped=0;
    int64_t startedHost=0,endedHost=0;
    // Wall clock (Unix ms, UTC) taken at startedHost; aligns log timestamps.
    int64_t startedUtcMs=0;
    bool durationReached=false;
};

// Starts streaming to `path` (absolute, must not exist; "<path>.partial" is
// written first). Fails when a diagnostic recording is already active.
bool startDiagnosticTrace(const std::filesystem::path& path,std::chrono::milliseconds maxDuration) noexcept;
// Seals producers, flushes the remaining events, finalizes and renames the
// file. Blocks until written. Safe to call when nothing is recording.
DiagnosticTraceStatus stopDiagnosticTrace() noexcept;
DiagnosticTraceStatus diagnosticTraceStatus() noexcept;
// The trace clock: 100 ns steady_clock ticks, the same clock as every
// host100ns field the engine records.
int64_t traceHostNow() noexcept;
// A user mark (Ctrl+Alt+M). Recorded as event "Mark", detail = number.
void recordDiagnosticMark(uint32_t number) noexcept;

}
