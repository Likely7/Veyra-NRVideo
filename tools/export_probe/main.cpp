// Headless export probe: runs the production export path (VideoExportJob) on
// one file so frame-timestamp policy can be verified without the GUI. It is a
// diagnostic harness, not a second implementation: features stay disabled so
// the run only exercises decode -> ingress -> encode -> mux -> verify.
//
//   veyra_export_probe <input> <output> [--hevc] [--max-frames N]
//                         [--trim-start seconds] [--trim-end seconds]
//                         [--audio-stream index]
//
// Exit codes: 0 completed, 1 export refused/failed, 2 bad arguments.
#include "veyra/engine/VideoExportJob.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) {
        std::fwprintf(stderr, L"usage: veyra_export_probe <input> <output> [--hevc] [--video-hdr] [--max-frames N] [--trim-start seconds] [--trim-end seconds] [--audio-stream index]\n");
        return 2;
    }
    const std::wstring input = argv[1];
    const std::wstring output = argv[2];
    bool hevc = false, videoHdr = false;
    unsigned maxFrames = 0;
    double trimStart = 0.0, trimEnd = 0.0;
    int audioStream = -1;
    for (int i = 3; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--hevc") {
            hevc = true;
        } else if (arg == L"--video-hdr") {
            videoHdr = true;
        } else if (arg == L"--max-frames" && i + 1 < argc) {
            maxFrames = unsigned(std::wcstoul(argv[++i], nullptr, 10));
        } else if (arg == L"--trim-start" && i + 1 < argc) {
            trimStart = std::wcstod(argv[++i], nullptr);
        } else if (arg == L"--trim-end" && i + 1 < argc) {
            trimEnd = std::wcstod(argv[++i], nullptr);
        } else if (arg == L"--audio-stream" && i + 1 < argc) {
            audioStream = int(std::wcstol(argv[++i], nullptr, 10));
        } else {
            std::fwprintf(stderr, L"unknown argument: %ls\n", arg.c_str());
            return 2;
        }
    }

    veyra::engine::PlayerOptions options; // nr/sr/fg all off: decode -> encode only
    options.settings.videoHdr.enabled = videoHdr;
    options.exportStartSeconds = trimStart;
    options.exportEndSeconds = trimEnd;
    options.audioStreamIndex = audioStream;
    std::atomic<bool> cancel{false};
    const bool ok = veyra::engine::exportVideo(input, output, options, hevc, cancel,
        [](double progress, const std::wstring& message) {
            std::wprintf(L"[%5.1f%%] %ls\n", progress * 100.0, message.c_str());
            std::fflush(stdout);
        },
        maxFrames);
    std::wprintf(L"export result=%ls\n", ok ? L"ok" : L"failed");
    return ok ? 0 : 1;
}
