// Reproduce the product's exact WebView2 startup path: hidden WS_EX_TOOLWINDOW
// STATIC parent (never shown), %LOCALAPPDATA%\Veyra\xbox-webview2 user data,
// NavigateToString with the real 712KB player bundle. Prints every HRESULT.
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <cstdio>
#include <string>
#include <atomic>
#include <chrono>
#include <fstream>
#include <filesystem>

using namespace Microsoft::WRL;
static std::atomic<int> g_state{0};   // 0 pending, 1 ready, 2 failed
static std::string g_reason;

static std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Same user data dir as the product.
    wchar_t buf[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring udd = std::wstring(buf) + L"\\Veyra\\xbox-webview2";
    std::filesystem::create_directories(udd);
    std::printf("userDataDir=%ls\n", udd.c_str());

    // Same hidden parent as WebView2Host::staMain.
    HWND parent = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    std::printf("hidden parent hwnd=%p err=%lu\n", parent, GetLastError());
    if (!parent) return 2;

    // Load the real bundle (arg1) or a tiny page.
    std::string js;
    if (argc > 1) {
        std::ifstream f(argv[1], std::ios::binary);
        js.assign(std::istreambuf_iterator<char>(f), {});
        for (size_t at = js.find("</script"); at != std::string::npos; at = js.find("</script", at + 8))
            js.replace(at, 8, "<\\/script");
    } else js = "chrome.webview.postMessage(JSON.stringify({type:'ready'}));";
    std::printf("bundle bytes=%zu\n", js.size());
    std::wstring html = widen("<!doctype html><html><head><meta charset=\"utf-8\"></head>"
                              "<body><div id=\"veyraVideoHolder\"></div><script>" + js + "</script></body></html>");

    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;

    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, udd.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&](HRESULT result, ICoreWebView2Environment* e) -> HRESULT {
                std::printf("env callback hr=0x%08X\n", uint32_t(result));
                if (FAILED(result) || !e) { g_reason = "environment"; g_state = 2; return S_OK; }
                env = e;
                const HRESULT hr2 = env->CreateCoreWebView2Controller(parent,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&](HRESULT r2, ICoreWebView2Controller* c) -> HRESULT {
                            std::printf("controller callback hr=0x%08X\n", uint32_t(r2));
                            if (FAILED(r2) || !c) { g_reason = "controller"; g_state = 2; return S_OK; }
                            printf("step: assign controller\n");
                            controller = c;
                            printf("step: put_IsVisible\n");
                            controller->put_IsVisible(FALSE);
                            printf("step: put_IsVisible done\n");
                            // Exact product settings block
                            printf("step: get_CoreWebView2\n");
                            controller->get_CoreWebView2(&webview);
                            printf("step: get_Settings\n");
                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(webview->get_Settings(&settings)) && settings) {
                                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsWebMessageEnabled(TRUE);
                            }
                            controller->put_Bounds(RECT{0,0,1,1});
                            printf("step: get_CoreWebView2\n");
                            controller->get_CoreWebView2(&webview);
                            EventRegistrationToken tok{};
                            printf("step: add_WebMessageReceived\n");
                            webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR json = nullptr;
                                        if (FAILED(args->TryGetWebMessageAsString(&json)) || !json) {
                                            if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) {} else json = nullptr;
                                        }
                                        std::wstring w(json ? json : L""); if (json) CoTaskMemFree(json);
                                        if (w.find(L"ready") != std::wstring::npos) {
                                            std::printf("READY received\n"); g_state = 1;
                                        } else {
                                            std::string s(w.begin(), w.end());
                                            std::printf("msg: %.120s\n", s.c_str());
                                        }
                                        return S_OK;
                                    }).Get(), &tok);
                            EventRegistrationToken pf{};
                            printf("step: add_ProcessFailed\n");
                            webview->add_ProcessFailed(
                                Callback<ICoreWebView2ProcessFailedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                                        COREWEBVIEW2_PROCESS_FAILED_KIND kind;
                                        args->get_ProcessFailedKind(&kind);
                                        std::printf("PROCESS FAILED kind=%d\n", int(kind));
                                        g_reason = "process"; g_state = 2;
                                        return S_OK;
                                    }).Get(), &pf);
                            printf("step: NavigateToString\n");
                            const HRESULT hr3 = webview->NavigateToString(html.c_str());
                            std::printf("NavigateToString hr=0x%08X\n", uint32_t(hr3));
                            if (FAILED(hr3)) { g_reason = "navigate"; g_state = 2; }
                            return S_OK;
                        }).Get());
                std::printf("CreateCoreWebView2Controller hr=0x%08X\n", uint32_t(hr2));
                if (FAILED(hr2)) { g_reason = "controller-call"; g_state = 2; }
                return S_OK;
            }).Get());
    std::printf("CreateCoreWebView2EnvironmentWithOptions hr=0x%08X\n", uint32_t(hr));
    if (FAILED(hr)) { std::printf("RESULT FAIL environment-call\n"); return 2; }

    MSG msg{};
    const auto t0 = std::chrono::steady_clock::now();
    while (g_state == 0 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(30)) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(5);
    }
    std::printf("RESULT %s reason=%s\n", g_state == 1 ? "READY" : "FAIL", g_reason.c_str());
    if (controller) controller->Close();
    DestroyWindow(parent);
    return g_state == 1 ? 0 : 2;
}