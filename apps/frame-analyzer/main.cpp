// SPDX-License-Identifier: GPL-3.0-only
// veyra_frame_analyzer — offline analysis of Veyra 性能诊断 packages.
//
//   veyra_frame_analyzer analyze <folder | trace.csv | veyra-qml.log> [--out <dir>] [--quiet]
//                                [--warmup <seconds>] [--target-fps <fps>]
//   veyra_frame_analyzer compare <A> <B> [--out <dir>]     (folders or Veyra诊断_*.zip)
//
// Reads only the files it is given and writes only into the output folder.
// Nothing is sent anywhere.
#include "veyra/diagnostics/FrameAnalysis.h"
#include "../../src/diagnostics/MiniJson.h"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace analysis = veyra::diagnostics::analysis;

namespace {
void usage() {
    std::fputs("usage:\n"
               "  veyra_frame_analyzer analyze <folder|trace.csv|veyra-qml.log> [--out <dir>] [--quiet] [--warmup <s>] [--target-fps <fps>]\n"
               "  veyra_frame_analyzer compare <A> <B> [--out <dir>]\n", stderr);
}
std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
// A package is extracted with Windows' own tar.exe. The archive is copied to an
// ASCII name first so the code page never matters.
bool extract(const fs::path& zip, const fs::path& target, std::string& error) {
    std::error_code ec;
    fs::remove_all(target, ec);
    fs::create_directories(target, ec);
    if (!CopyFileW(zip.c_str(), (target / L"input.zip").c_str(), FALSE)) { error = "cannot copy " + utf8(zip.wstring()); return false; }
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring tar = std::wstring(system) + L"\\tar.exe";
    std::wstring command = L"\"" + tar + L"\" -x -f input.zip";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, target.c_str(), &startup, &process)) {
        error = "cannot start tar.exe";
        return false;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    fs::remove(target / L"input.zip", ec);
    if (code) { error = "tar.exe failed to extract " + utf8(zip.wstring()); return false; }
    return true;
}
fs::path packageFolder(const fs::path& input, const fs::path& scratch, std::string& error) {
    if (fs::is_directory(input)) return input;
    if (input.extension() == L".zip" || input.extension() == L".ZIP") {
        if (!extract(input, scratch, error)) return {};
        return scratch;
    }
    error = "not a folder or .zip: " + utf8(input.wstring());
    return {};
}
void printSummary(const fs::path& out) {
    std::ifstream in(out / "report.json", std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    try {
        const auto report = veyra::diagnostics::json::parse(text.str());
        std::printf("%s\n", report["summary"]["headline"].string().c_str());
        int k = 0;
        for (const auto& f : report["findings"].array()) {
            std::printf("  [%s %s] %s\n", f["id"].string().c_str(), f["severity"].string().c_str(), f["title"].string().c_str());
            if (++k == 5) break;
        }
    } catch (...) {}
    std::printf("report: %s\n", utf8((out / "report.html").wstring()).c_str());
}
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    if (argc < 3) { usage(); return 2; }
    const std::wstring command = argv[1];
    std::vector<std::wstring> positional;
    fs::path out;
    analysis::Options options;
    for (int i = 2; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--out" && i + 1 < argc) out = argv[++i];
        else if (a == L"--quiet") options.quiet = true;
        else if (a == L"--warmup" && i + 1 < argc) options.warmupSeconds = std::wcstod(argv[++i], nullptr);
        else if (a == L"--target-fps" && i + 1 < argc) options.targetFps = std::wcstod(argv[++i], nullptr);
        else if (a.rfind(L"--", 0) == 0) { usage(); return 2; }
        else positional.push_back(a);
    }
    std::string error;
    try {
        if (command == L"analyze" && positional.size() == 1) {
            fs::path input = fs::absolute(positional[0]);
            if (input.extension() == L".zip") {
                const fs::path folder = input.parent_path() / (input.stem().wstring() + L".analysis");
                if (!extract(input, folder, error)) { std::fprintf(stderr, "%s\n", error.c_str()); return 3; }
                input = folder;
            }
            if (out.empty()) out = fs::is_directory(input) ? input : input.parent_path() / (input.filename().wstring() + L".analysis");
            const int code = analysis::analyze(input, out, options, error);
            if (code) { std::fprintf(stderr, "analyze failed: %s\n", error.c_str()); return code; }
            if (!options.quiet) printSummary(out);
            return 0;
        }
        if (command == L"compare" && positional.size() == 2) {
            const fs::path a = fs::absolute(positional[0]), b = fs::absolute(positional[1]);
            if (out.empty()) out = a.parent_path() / L"Veyra诊断对比";
            fs::create_directories(out);
            const fs::path fa = packageFolder(a, out / L"A", error);
            if (fa.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 3; }
            const fs::path fb = packageFolder(b, out / L"B", error);
            if (fb.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 3; }
            const int code = analysis::compare(fa, fb, out, options, error);
            if (code) { std::fprintf(stderr, "compare failed: %s\n", error.c_str()); return code; }
            if (!options.quiet) std::printf("comparison: %s\n", utf8((out / L"COMPARE.md").wstring()).c_str());
            return 0;
        }
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "error: %s\n", ex.what());
        return 5;
    }
    usage();
    return 2;
}
