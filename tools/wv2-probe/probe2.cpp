// v2 probe 2: determine exact encoded frame format (AVCC vs Annex-B) and metadata.
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <cstdio>
#include <string>
#include <atomic>

using namespace Microsoft::WRL;
static std::atomic<int> g_n{0};
static std::atomic<bool> g_done{false};

static const wchar_t* kHtml = LR"HTML(
<!doctype html><html><body><script>
const log = (m) => chrome.webview.postMessage(JSON.stringify({type:'log',text:m}));
const workerCode = `
self.onrtctransform = (event) => {
  const t = event.transformer;
  const reader = t.readable.getReader();
  const writer = t.writable.getWriter();
  (async () => {
    let n = 0;
    while (true) {
      const {done, value} = await reader.read();
      if (done) break;
      const meta = value.getMetadata ? value.getMetadata() : {};
      const buf = new Uint8Array(value.data);
      n++;
      if (n <= 5 || n % 30 === 0) {
        self.postMessage({n, size: buf.length,
          head: Array.from(buf.slice(0, 24)),
          meta: meta, type: value.type});
      }
      await writer.write(value);
    }
  })();
};
`;
const worker = new Worker(URL.createObjectURL(new Blob([workerCode],{type:'application/javascript'})));
worker.onmessage = (e) => chrome.webview.postMessage(JSON.stringify({type:'frame', d:e.data}));
async function main() {
  const pc1 = new RTCPeerConnection();
  const pc2 = new RTCPeerConnection();
  pc1.onicecandidate = e => e.candidate && pc2.addIceCandidate(e.candidate);
  pc2.onicecandidate = e => e.candidate && pc1.addIceCandidate(e.candidate);
  const canvas = document.createElement('canvas');
  canvas.width = 320; canvas.height = 240;
  const ctx = canvas.getContext('2d');
  let f = 0;
  setInterval(() => { f++; ctx.fillStyle='rgb('+(f*7%255)+',80,120)'; ctx.fillRect(0,0,320,240); ctx.fillStyle='#fff'; ctx.fillText('frame '+f,20,120); }, 33);
  const stream = canvas.captureStream(30);
  pc1.addTrack(stream.getVideoTracks()[0], stream);
  pc2.ontrack = (e) => { e.receiver.transform = new RTCRtpScriptTransform(worker, {kind:'video'}); };
  const offer = await pc1.createOffer();
  await pc1.setLocalDescription(offer);
  await pc2.setRemoteDescription(offer);
  const answer = await pc2.createAnswer();
  await pc2.setLocalDescription(answer);
  await pc1.setRemoteDescription(answer);
  log('connected');
}
main().catch(e => log('ERR '+e));
</script></body></html>
)HTML";

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"v2probe2", WS_OVERLAPPEDWINDOW, 0,0,400,300, nullptr,nullptr, GetModuleHandleW(nullptr), nullptr);
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
                                        std::string s(w.begin(), w.end());
                                        if (s.find("\"type\":\"frame\"") != std::string::npos) {
                                            std::printf("%s\n", s.c_str());
                                            if (++g_n >= 8) g_done = true;
                                        } else std::printf("JS: %s\n", s.c_str());
                                        return S_OK;
                                    }).Get(), nullptr);
                            webview->NavigateToString(kHtml);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
    MSG msg; int ticks=0;
    while (!g_done && ticks < 600) {
        while (PeekMessageW(&msg, nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(16); ticks++;
    }
    return g_done ? 0 : 2;
}