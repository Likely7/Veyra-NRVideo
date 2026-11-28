// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Offline analysis of a 性能诊断 package (veyra_frame_analyzer.exe). Standard
// C++ only, so the same code runs inside the shipped analyzer and in tests.
#include <filesystem>
#include <string>

namespace veyra::diagnostics::analysis {

struct Options {
    bool quiet = false;
    // Seconds after a session start or a pipeline reset left out of the
    // steady-state statistics (the full timeline is still reported).
    double warmupSeconds = 2.0;
    // 0 = infer the expected output interval from the data.
    double targetFps = 0;
};

// `input` is a diagnostic folder (raw/trace.csv, raw/telemetry.csv,
// raw/events.json, raw/snapshot.json, raw/veyra-log.txt), a single frame trace
// CSV (VEYRA_PRIVATE_FRAME_TRACE_FILE) or a Veyra log (log-only analysis, e.g.
// for versions without the recorder). Writes report.json, SUMMARY.md,
// report.html and frames.csv into `outDir`. Returns 0 on success.
int analyze(const std::filesystem::path& input, const std::filesystem::path& outDir, const Options& options, std::string& error);

// Two analysed folders (or packages extracted by the caller): metric changes,
// settings and environment differences. Writes compare.json, COMPARE.md and
// compare.html into `outDir`.
int compare(const std::filesystem::path& a, const std::filesystem::path& b, const std::filesystem::path& outDir,
            const Options& options, std::string& error);

}
