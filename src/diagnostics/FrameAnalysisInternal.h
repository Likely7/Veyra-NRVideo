// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "MiniJson.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace veyra::diagnostics::analysis::detail {

// Where a hitch spent its extra time (see FrameAnalysis.cpp, attribute()).
enum Cause : int16_t { CauseNone = -1, CauseInput, CauseEngine, CauseGpu, CausePresent, CausePacing, CauseDiscard, CauseReset, CauseUnknown, CauseCount };
inline const char* causeId(int cause) {
    static const char* ids[] = {"input", "engine", "gpu", "present", "pacing", "discard", "reset", "unknown"};
    return cause >= 0 && cause < CauseCount ? ids[cause] : "";
}
inline const char* causeZh(int cause) {
    static const char* names[] = {"输入断档（采集/解码没按时送来新画面）", "引擎排队（CPU 侧处理晚了）", "GPU 处理超时", "送显调用阻塞（驱动/系统/叠加层）",
                                  "节拍等待", "生成帧被跳过或丢弃（补帧没赶上）", "重建（改设置/切换/跳转）", "未能归因"};
    return cause >= 0 && cause < CauseCount ? names[cause] : "";
}

struct FrameRow {
    double t = 0, dt = 0;            // ms from the recording start; interval to the previous present
    double target = 0;               // expected interval of the frame's settings revision
    uint8_t kind = 0;                // 0 unknown, 1 real, 2 generated
    uint64_t session = 0, revision = 0, source = 0;
    uint32_t subframe = 0;
    double callMs = -1, waitInMs = -1, gpuMs = -1, pacingMs = -1, lateMs = NAN, latencyMs = -1;
    bool hitch = false, steady = true;
    int16_t cause = CauseNone;
    std::string stage;               // GPU or CPU stage named by the attribution
    int event = -1;                  // index into report.events
};

struct Analysis {
    json::Value report;
    std::vector<FrameRow> frames;
};

std::string readText(const std::filesystem::path& path);
bool writeText(const std::filesystem::path& path, const std::string& text);
void writeFramesCsv(const Analysis& analysis, const std::filesystem::path& path);
void writeSummary(const Analysis& analysis, const std::filesystem::path& path);
void writeHtml(const Analysis& analysis, const std::filesystem::path& path);
void writeCompare(const json::Value& comparison, const std::filesystem::path& outDir);

}
