// claude-capture-fps-audio-20261027: does WGC window capture, used the way Veyra's
// ScreenCaptureSource uses it (free-threaded pool of 2, polled, newest kept), deliver
// every frame a window presents on a display whose refresh is not a multiple of 60?
//   wgc_rate_probe run <free60|lock60> <seconds> <occlude 0|1>
//   free60: the source presents on exact 16.667 ms deadlines (Present(0)).
//   lock60: a 60 FPS limiter that measures from a composition-aligned point (sleep
//           16.667 ms, Present(1), DwmFlush) - the in-game VSync + cap interaction.
//   occlude 1: a topmost no-activate window presenting at 55 Hz covers the source,
//           like Veyra's game mode picture over the game.
#include <windows.h>
#include <dwmapi.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "windowsapp.lib")
using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;

double nowMs() {
    static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c); return double(c.QuadPart) * 1000.0 / double(f.QuadPart);
}
void sleepUntil(double t) {
    static thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    for (;;) {
        const double d = t - nowMs(); if (d <= 0) return;
        if (d > 1.5) { LARGE_INTEGER due; due.QuadPart = -LONGLONG((d - 1.0) * 10000); SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0); WaitForSingleObject(timer, INFINITE); }
        else YieldProcessor();
    }
}
void pump() { MSG m; while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } }

struct Presenter {
    HWND hwnd{}; ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx; ComPtr<IDXGISwapChain1> sc; ComPtr<ID3D11RenderTargetView> rtv;
    bool create(const wchar_t* cls, int x, int y, int w, int h, DWORD exStyle) {
        WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = cls; RegisterClassW(&wc);
        hwnd = CreateWindowExW(exStyle, cls, cls, WS_POPUP | WS_VISIBLE, x, y, w, h, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd || FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx))) return false;
        ComPtr<IDXGIDevice> dx; dev.As(&dx); ComPtr<IDXGIAdapter> ad; dx->GetAdapter(&ad); ComPtr<IDXGIFactory2> f; ad->GetParent(IID_PPV_ARGS(&f));
        DXGI_SWAP_CHAIN_DESC1 d{}; d.Width = w; d.Height = h; d.Format = DXGI_FORMAT_B8G8R8A8_UNORM; d.SampleDesc.Count = 1;
        d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; d.BufferCount = 2; d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(f->CreateSwapChainForHwnd(dev.Get(), hwnd, &d, nullptr, nullptr, &sc))) return false;
        ComPtr<ID3D11Texture2D> bb; sc->GetBuffer(0, IID_PPV_ARGS(&bb)); return SUCCEEDED(dev->CreateRenderTargetView(bb.Get(), nullptr, &rtv));
    }
    void frame(unsigned n, UINT sync) {
        const float c[4] = {float(n % 7) / 6.f, float(n % 11) / 10.f, float(n % 13) / 12.f, 1};
        ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr); ctx->ClearRenderTargetView(rtv.Get(), c); sc->Present(sync, 0);
    }
};

int source(const std::string& mode, double seconds) {
    Presenter p; if (!p.create(L"VeyraRateProbeSource", 200, 200, 640, 360, WS_EX_NOACTIVATE)) return 2;
    const bool lock = mode == "lock60";
    const double start = nowMs(); double next = start, mark = start; unsigned n = 0;
    while (nowMs() - start < seconds * 1000) {
        pump();
        if (lock) { sleepUntil(mark + 1000.0 / 60); p.frame(++n, 1); DwmFlush(); mark = nowMs(); }
        else { next += 1000.0 / 60; sleepUntil(next); p.frame(++n, 0); }
    }
    const double s = (nowMs() - start) / 1000;
    std::printf("SOURCE mode=%s presents=%u seconds=%.3f fps=%.2f\n", mode.c_str(), n, s, n / s);
    return 0;
}

int run(const std::string& mode, double seconds, bool occlude, double minUpdateMs) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti); DwmGetCompositionTimingInfo(nullptr, &ti);
    const double refreshHz = ti.rateRefresh.uiDenominator ? double(ti.rateRefresh.uiNumerator) / ti.rateRefresh.uiDenominator : 0;
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" source " + std::wstring(mode.begin(), mode.end()) + L" " + std::to_wstring(int(seconds + 4));
    STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return 3;
    HWND target = nullptr;
    for (int i = 0; i < 200 && !target; ++i) {
        Sleep(20);
        for (HWND w = FindWindowW(L"VeyraRateProbeSource", nullptr); w; w = FindWindowExW(nullptr, w, L"VeyraRateProbeSource", nullptr)) {
            DWORD pid = 0; GetWindowThreadProcessId(w, &pid); if (pid == pi.dwProcessId) { target = w; break; }
        }
    }
    if (!target) return 4;
    Sleep(500);
    std::atomic<bool> stop{false}; std::thread cover;
    if (occlude) cover = std::thread([&] {
        Presenter c; if (!c.create(L"VeyraRateProbeCover", 200, 200, 640, 360, WS_EX_TOPMOST | WS_EX_NOACTIVATE)) return;
        double next = nowMs(); unsigned n = 0;
        while (!stop) { pump(); next += 1000.0 / 55; sleepUntil(next); c.frame(++n, 0); }
        DestroyWindow(c.hwnd);
    });
    ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
    D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &dev, nullptr, &ctx);
    ComPtr<IDXGIDevice> dxgi; dev.As(&dxgi); winrt::com_ptr<IInspectable> insp;
    CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), insp.put());
    auto device = insp.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    GraphicsCaptureItem item{nullptr};
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    winrt::check_hresult(interop->CreateForWindow(target, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
    auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, item.Size());
    auto session = pool.CreateCaptureSession(item); session.IsCursorCaptureEnabled(false);
    double defaultMinMs = -1;
    if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"MinUpdateInterval")) {
        defaultMinMs = session.MinUpdateInterval().count() / 10000.0;
        if (minUpdateMs >= 0) session.MinUpdateInterval(winrt::Windows::Foundation::TimeSpan{int64_t(minUpdateMs * 10000)});
    }
    session.StartCapture();
    Sleep(1000);
    unsigned received = 0, extra = 0; int64_t last = 0; std::vector<double> gaps;
    const double start = nowMs();
    while (nowMs() - start < seconds * 1000) {
        for (int i = 0; i < 2; ++i) {
            auto f = pool.TryGetNextFrame(); if (!f) break;
            if (i) ++extra;
            const int64_t t = f.SystemRelativeTime().count();
            if (last) gaps.push_back((t - last) / 10000.0);
            last = t; ++received; f.Close();
        }
        sleepUntil(nowMs() + 1.0);
    }
    const double s = (nowMs() - start) / 1000;
    session.Close(); pool.Close(); stop = true; if (cover.joinable()) cover.join();
    WaitForSingleObject(pi.hProcess, 15000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    const double tick = refreshHz ? 1000.0 / refreshHz : 0;
    std::map<long, unsigned> ticks; for (double g : gaps) ++ticks[tick ? std::lround(g / tick) : 0];
    std::printf("CAPTURE mode=%s occlude=%d minUpdateMs=%.3f (default %.3f) refreshHz=%.3f received=%u seconds=%.3f fps=%.2f twoAtOnce=%u\n",
                mode.c_str(), int(occlude), minUpdateMs, defaultMinMs, refreshHz, received, s, received / s, extra);
    std::printf("CAPTURE gap-in-refreshes:");
    for (auto [k, v] : ticks) std::printf(" %ld=%u", k, v);
    std::printf("\n");
    return 0;
}

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc == 4 && std::string(argv[1]) == "source") return source(argv[2], std::atof(argv[3]));
    if ((argc == 5 || argc == 6) && std::string(argv[1]) == "run")
        return run(argv[2], std::atof(argv[3]), std::atoi(argv[4]) != 0, argc == 6 ? std::atof(argv[5]) : -1);
    std::printf("usage: wgc_rate_probe run <free60|lock60> <seconds> <occlude 0|1> [minUpdateMs]\n");
    return 1;
}
