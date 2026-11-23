// v2 probe 7: isolate the WebMessage channel throughput (no WebRTC encode).
// Synthesizes 30KB frames at 60fps in JS, base64 + postMessage; C++ counts receipt.
// Answers: can the export channel sustain 60fps x ~30KB (1080p H264 @15Mbps)?
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <cstdio>
#include <atomic>
#include <chrono>

using namespace Microsoft::WRL;
static std::atomic<uint64_t> g_frames{0}, g_bytes{0};

static const wchar_t* kHtml = LR"HTML(
<!doctype html><html><body><script>
const log = (m) => chrome.webview.postMessage(JSON.stringify({type:'log',text:m}));
// 30KB synthetic frame, like a 1080p H264 AU at ~15Mbps/60fps
const N = 30000;
const buf = new Uint8Array(N);
for (let i = 0; i < N; i++) buf[i] = (i * 7) & 255;
let bin = '';
for (let i = 0; i < N; i++) bin += String.fromCharCode(buf[i]);
const b64 = btoa(bin);
let produced = 0;
const t0 = performance.now();
const timer = setInterval(() => {
  produced++;
  chrome.webview.postMessage(JSON.stringify({type:'videoFrame', b64, size:N, seq:produced}));
  if (performance.now() - t0 > 5000) {
    clearInterval(timer);
    log('JS produced ' + produced + ' frames in 5s = ' + (produced/5).toFixed(1) + ' fps');
    chrome.webview.postMessage(JSON.stringify({type:'done'}));
  }
}, 16);
log('synthetic 30KB @60fps started');
</script></body></html>
)HTML";

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"v2probe7", WS_OVERLAPPEDWINDOW, 0,0,400,300, nullptr,nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(hwnd, SW_SHOW);
    CreateCoreWebView2EnvironmentWithOptions(nullptr, L"C:\\veyra-xbox-enhanced\\veyra-nrvideo\\tools\\wv2-probe-data", nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&](HRESULT, ICoreWebView2Environment* e) -> HRESULT {
                env = e;
                env->CreateCoreWebView2Controller(hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&](HRESULT, ICoreWebView2Controller* c) -> HRESULT {
                            controller = c; controller->get_CoreWebView2(&webview);
                            webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR json; args->TryGetWebMessageAsString(&json);
                                        std::wstring w(json); CoTaskMemFree(json);
                                        if (w.find(L"videoFrame") != std::wstring::npos) { ++g_frames; g_bytes += w.size(); }
                                        else { std::string s(w.begin(), w.end()); std::printf("JS: %s\n", s.c_str()); }
                                        return S_OK;
                                    }).Get(), nullptr);
                            webview->NavigateToString(kHtml);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
    auto t0 = std::chrono::steady_clock::now();
    MSG msg;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(7)) {
        while (PeekMessageW(&msg, nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(1);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("RESULT received=%llu fps=%.1f MBps=%.2f (channel-only, no encode)\n",
        (unsigned long long)g_frames.load(), g_frames.load()/secs, g_bytes.load()/1e6/secs);
    return g_frames >= 250 ? 0 : 2;
}