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
#include <stdexcept>
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
// Own a fresh directory for each archive. Re-analyzing an archive must never
// remove a previously saved analysis or another directory selected by the user.
class ExtractionDirectory {
public:
    ExtractionDirectory() = default;
    ExtractionDirectory(const ExtractionDirectory&) = delete;
    ExtractionDirectory& operator=(const ExtractionDirectory&) = delete;

    const fs::path& create() {
        const auto root = fs::temp_directory_path();
        const auto prefix = L"VeyraAnalyze-" + std::to_wstring(GetCurrentProcessId())
                          + L"-" + std::to_wstring(GetTickCount64()) + L"-";
        for (unsigned n = 0; n < 100; ++n) {
            const auto candidate = root / (prefix + std::to_wstring(n));
            if (!fs::create_directory(candidate)) continue;
            path_ = candidate;
            return path_;
        }
        throw std::runtime_error("no temporary directory available for analysis");
    }

    ~ExtractionDirectory() {
        if (!path_.empty()) {
            std::error_code ec;
            fs::remove_all(path_, ec);
        }
    }

private:
    fs::path path_;
};

// Use an ASCII archive name so system tar can handle a non-ASCII input name.
bool extract(const fs::path& zip, const fs::path& target, std::string& error) {
    if (!CopyFileW(zip.c_str(), (target / L"input.zip").c_str(), TRUE)) { error = "cannot copy " + utf8(zip.wstring()); return false; }
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    const std::wstring tar = std::wstring(system) + L"\\tar.exe";
    std::wstring command = L"\"" + tar + L"\" -x -k -f input.zip";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, target.c_str(), &startup, &process)) {
        error = "cannot start tar.exe";
        return false;
    }
    CloseHandle(process.hThread);
    const auto wait = WaitForSingleObject(process.hProcess, 120000);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 5000);
    }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    if (wait != WAIT_OBJECT_0) { error = "archive extraction did not finish"; return false; }
    if (code) { error = "tar.exe failed to extract " + utf8(zip.wstring()); return false; }
    return true;
}
bool isZip(const fs::path& path) {
    const auto extension = path.extension().wstring();
    return CompareStringOrdinal(extension.c_str(), -1, L".zip", -1, TRUE) == CSTR_EQUAL;
}

fs::path packageFolder(const fs::path& input, ExtractionDirectory& scratch, std::string& error) {
    if (fs::is_directory(input)) return input;
    if (isZip(input)) {
        const auto& folder = scratch.create();
        if (!extract(input, folder, error)) return {};
        return folder;
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
            ExtractionDirectory scratch;
            if (isZip(input)) {
                if (out.empty()) out = input.parent_path() / (input.stem().wstring() + L".analysis");
                input = packageFolder(input, scratch, error);
                if (input.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 3; }
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
            ExtractionDirectory scratchA, scratchB;
            const fs::path fa = packageFolder(a, scratchA, error);
            if (fa.empty()) { std::fprintf(stderr, "%s\n", error.c_str()); return 3; }
            const fs::path fb = packageFolder(b, scratchB, error);
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
