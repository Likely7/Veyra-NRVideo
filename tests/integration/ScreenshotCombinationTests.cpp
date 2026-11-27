#include "veyra/engine/EngineController.h"
#include "veyra/Log.h"
#include "veyra/gfx/PresentSink.h"
#include "veyra/sink/ImageExportSink.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <iostream>
#include <string_view>
using namespace veyra::engine;

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

int liveScreenshot(HWND window, const wchar_t* input, const std::filesystem::path& dir,
                   bool capture, bool vfg, bool hdr) {
    EngineController engine;
    engine.setVolume(0, true);
    PresentationSettings presentation;
    presentation.display = DisplaySync::Tearing;
    engine.requestPresentation(presentation);
    PlayerOptions options;
    options.fg = vfg;
    options.captureReplayForTest = true;
    options.settings.content = ContentRate::Transport;
    if (vfg) {
        options.settings.frameGenerationBackend = FrameGenerationBackend::Vfg;
        options.settings.vfgQuality = 1;
        options.settings.vfgAutoRegulate = false;
    }
    RecordingSettings replay;
    replay.directory = dir.wstring();
    replay.audio = false;
    std::wstring error;
    if (!engine.setReplay(true, replay, error)) {
        std::wcerr << L"Cannot enable replay: " << error << L'\n';
        return 1;
    }
    engine.open(window, input, options);
    auto wait = [&](auto predicate, auto timeout) {
        const auto deadline = Clock::now() + timeout;
        while (Clock::now() < deadline) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            const auto status = engine.snapshot();
            const auto buffer = engine.replaySnapshot();
            if (status.failed || buffer.failed) {
                std::wcerr << status.status << L" / " << buffer.error << L'\n';
                return false;
            }
            if (predicate(status, buffer)) return true;
            std::this_thread::sleep_for(3ms);
        }
        return false;
    };
    const auto path = dir / L"live.png";
    auto actual = path;
    if (hdr) actual.replace_extension(L".jxr");
    bool ok = wait([&](const auto& status, const auto& buffer) {
        return status.frames >= 120 && buffer.frames >= 90 &&
               (!vfg || status.metrics.flow.counters.generatedPresented >= 20);
    }, 10s);
    if (ok) {
        const auto head = engine.snapshot();
        ok = head.sourceWidth == 3840 && head.sourceHeight == 2160 &&
             !head.nrActive && !head.srActive && head.fgActive == vfg && head.sourceHdr == hdr;
        const auto replayBefore = engine.replaySnapshot();
        const auto mark = Clock::now();
        const auto mark100ns = std::chrono::duration_cast<std::chrono::nanoseconds>(mark.time_since_epoch()).count() / 100;
        if (capture) engine.saveFrame(path.wstring());
        std::cout << "SCREENSHOT_BEGIN capture=" << capture << " host100ns=" << mark100ns
                  << " source=" << head.sourceWidth << 'x' << head.sourceHeight
                  << " vfg=" << vfg << " hdr=" << hdr
                  << " generated=" << head.metrics.flow.counters.generatedPresented
                  << " replayFrames=" << replayBefore.frames << std::endl;
        if (capture) {
            uint64_t framesWhileSaving = 0;
            const bool saved = wait([&](const auto& status, const auto&) {
                if (status.savedFramePath == actual.wstring()) return true;
                framesWhileSaving = status.frames - head.frames;
                return false;
            }, 6s);
            const auto elapsed = Clock::now() - mark;
            std::cout << "SCREENSHOT_DONE saved=" << saved << " elapsedMs="
                      << std::chrono::duration<double, std::milli>(elapsed).count()
                      << " framesWhileSaving=" << framesWhileSaving << std::endl;
            // A fast encode can legitimately finish before the next live frame.
            ok = saved && (elapsed < 250ms || framesWhileSaving >= 3) && ok;
        }
        ok = wait([&](const auto&, const auto& buffer) {
            return Clock::now() >= mark + 2s && buffer.frames >= replayBefore.frames + 20;
        }, 6s) && ok;
        const auto replayAfter = engine.replaySnapshot();
        std::cout << "SCREENSHOT_WINDOW replayFrames=" << replayAfter.frames - replayBefore.frames
                  << " replayDrops=" << replayAfter.dropped - replayBefore.dropped << std::endl;
    }
    engine.setReplay(false, replay, error);
    engine.stop();
    const bool stopped = wait([&](const auto&, const auto&) { return engine.idle(); }, 6s);
    if (ok && capture) {
        veyra::sink::RgbaImage image;
        ok = veyra::sink::loadImage(actual.wstring(), image) && image.width == 3840 && image.height == 2160;
    }
    return ok && stopped ? 0 : 1;
}
}

int wmain(int argc,wchar_t** argv){
    constexpr std::wstring_view screenshotMode = L"--live-screenshot";
    constexpr std::wstring_view controlMode = L"--live-control";
    const auto mode = argc == 4 ? std::wstring_view(argv[3]) : std::wstring_view{};
    const bool screenshot = mode.starts_with(screenshotMode);
    const bool control = mode.starts_with(controlMode);
    const auto suffix = mode.substr(screenshot ? screenshotMode.size() : control ? controlMode.size() : 0);
    const bool live = (screenshot || control) &&
                      (suffix.empty() || suffix == L"-vfg" || suffix == L"-vfg-hdr");
    if(argc!=3&&!live)return 2;
    const std::filesystem::path dir=argv[2];std::filesystem::create_directories(dir);
    if(live)veyra::Logger::instance().startAsync();
    veyra::Logger::instance().openFile((dir/L"engine.log").wstring());
    if(FAILED(CoInitializeEx(nullptr,COINIT_MULTITHREADED)))return 2;
    HWND window=CreateWindowExW(0,L"STATIC",L"Screenshot combination regression",WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,80,960,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window){CoUninitialize();return 2;}
    if(live){
        const int result=liveScreenshot(window, argv[1], dir, screenshot,
                                        !suffix.empty(), suffix == L"-vfg-hdr");
        DestroyWindow(window);CoUninitialize();return result;
    }
    int failures=0;
    for(bool nr:{false,true})for(bool hdr:{false,true}){
        EngineController engine;engine.setVolume(0,true);EnhancementSettings settings;settings.nr=nr;settings.videoHdr.enabled=hdr;
        engine.open(window,argv[1],PlayerOptions::from(settings));
        auto wait=[&](auto predicate){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
            while(std::chrono::steady_clock::now()<end){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}const auto s=engine.snapshot();if(s.failed)return false;if(predicate(s))return true;std::this_thread::sleep_for(std::chrono::milliseconds(10));}return false;};
        bool ok=wait([&](const auto& s){return s.frames>=12&&(!nr||s.nrActive);});
        for(bool paused:{false,true}){
            if(paused){engine.pause(true);ok=wait([](const auto& s){return s.transport==TransportState::Paused;})&&ok;}
            const auto base=dir/(std::to_wstring(nr)+L"-"+std::to_wstring(hdr)+L"-"+std::to_wstring(paused)+L".png");
            auto actual=base;const bool hdrOutput=hdr&&veyra::gfx::PresentSink::hdrDisplayActive(window);if(hdrOutput)actual.replace_extension(L".jxr");
            engine.saveFrame(base.wstring());
            const bool saved=wait([&](const auto&){return std::filesystem::exists(actual)&&std::filesystem::file_size(actual)>100;});
            veyra::sink::RgbaImage decoded;const bool reopened=saved&&veyra::sink::loadImage(actual.wstring(),decoded)&&decoded.width>0&&decoded.height>0;
            std::cout<<"SCREENSHOT nr="<<nr<<" hdrRequested="<<hdr<<" hdrOutput="<<hdrOutput<<" paused="<<paused<<" saved="<<saved<<" reopened="<<reopened<<std::endl;ok=reopened&&ok;
        }
        failures+=!ok;engine.stop();
    }
    DestroyWindow(window);CoUninitialize();return failures?1:0;
}
