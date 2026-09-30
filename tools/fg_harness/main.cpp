// veyra_fg_harness: Phase 6 probe entry point.
//   --fg-cap       DLSSG capability query (JSON evidence)
//   --fg-test      DLSSG 2X truth harness (translation + scene cut)
//   --audio-test   WASAPI event-mode audio probe

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "audio_test.h"
#include "fg_test.h"
#include "../nr_harness/harness_util.h"
#include "veyra/Log.h"

namespace {

void printUsage()
{
    std::fprintf(stderr,
        "usage: veyra_fg_harness --fg-cap | --fg-test | --fg-planar6 | --fg-planar-alt | --audio-test\n"
        "  [--runtime-dir DIR] [--run-id ID] [--log-file FILE] [--json-file FILE]\n"
        "  [--capture-dir DIR] [--dlssg-transfusion ada|ampere]\n");
}

} // namespace

int main(int argc, char** argv)
{
    using namespace veyra::harness;

    bool doCap = false, doFgTest = false, doAudio = false;
    bool planarSix = false, alternateGroups = false;
    std::wstring runtimeDir;
    std::string runId = "fg-harness";
    std::wstring logFile, jsonFile, captureDir;
    std::string transfusion;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--fg-cap") doCap = true;
        else if (arg == "--fg-test") doFgTest = true;
        else if (arg == "--fg-planar6") { doFgTest = true; planarSix = true; }
        else if (arg == "--fg-planar-alt") { doFgTest = true; planarSix = true; alternateGroups = true; }
        else if (arg == "--audio-test") doAudio = true;
        else if (arg == "--run-id" && i + 1 < argc) runId = argv[++i];
        else if (arg == "--dlssg-transfusion" && i + 1 < argc) transfusion = argv[++i];
        else if (arg == "--runtime-dir" && i + 1 < argc) {
            const std::string value = argv[++i];
            runtimeDir.assign(value.begin(), value.end());
        }
        else if (arg == "--log-file" && i + 1 < argc) {
            const std::string value = argv[++i];
            logFile.assign(value.begin(), value.end());
        }
        else if (arg == "--json-file" && i + 1 < argc) {
            const std::string value = argv[++i];
            jsonFile.assign(value.begin(), value.end());
        }
        else if (arg == "--capture-dir" && i + 1 < argc) {
            const std::string value = argv[++i];
            captureDir.assign(value.begin(), value.end());
        }
        else { printUsage(); return 2; }
    }

    if (doCap + doFgTest + doAudio != 1) {
        printUsage();
        return 2;
    }
    if (runtimeDir.empty()) runtimeDir = L"runtime_local\\nvidia";
    if (!logFile.empty()) (void)veyra::Logger::instance().openFile(logFile);

    if (doCap) {
        FgTestArgs args{};
        args.runtimeDir = runtimeDir;
        args.runId = runId + "-cap";
        args.jsonFile = jsonFile;
        args.capabilityOnly = true;
        return runFgTest(args);
    }
    if (doFgTest) {
        FgTestArgs args{};
        args.runtimeDir = runtimeDir;
        args.runId = runId;
        args.jsonFile = jsonFile;
        args.captureDir = captureDir;
        args.planarSix = planarSix;
        args.alternateGroups = alternateGroups;
        args.transfusion = transfusion;
        return runFgTest(args);
    }
    AudioTestArgs args{};
    args.runId = runId;
    args.jsonFile = jsonFile;
    return runAudioTest(args);
}
