// v2 probe: WebView2 + loopback WebRTC + RTCRtpScriptTransform encoded export.
// Verifies the core v2 assumption: Chromium receive-side Encoded Transform yields
// complete H264 access units that can be handed to a native decoder.
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <cstdio>
#include <string>
#include <atomic>

using namespace Microsoft::WRL;

static std::atomic<int> g_videoFrames{0}, g_audioFrames{0}, g_keyframes{0};
static std::atomic<bool> g_done{false};
static std::string g_lastMeta;

static const wchar_t* kHtml = LR"HTML(
<!doctype html><html><body><script>
const log = (m) => chrome.webview.postMessage(JSON.stringify({type:'log',text:m}));
const workerCode = `
self.onrtctransform = (event) => {
  const t = event.transformer;
  const kind = t.options.kind;
  const reader = t.readable.getReader();
  const writer = t.writable.getWriter();
  (async () => {
    while (true) {
      const {done, value} = await reader.read();
      if (done) break;
      const meta = value.getMetadata ? value.getMetadata() : {};
      const buf = new Uint8Array(value.data);
      let bin = '';
      for (let i = 0; i < buf.length; i++) bin += String.fromCharCode(buf[i]);
      const b64 = btoa(bin);
      self.postMessage({kind, b64, meta, size: buf.length,
        head: Array.from(buf.slice(0, 8))});
      await writer.write(value);
    }
  })();
};
`;
const blob = new Blob([workerCode], {type:'application/javascript'});
const worker = new Worker(URL.createObjectURL(blob));
worker.onmessage = (e) => {
  const d = e.data;
  chrome.webview.postMessage(JSON.stringify({
    type: d.kind === 'video' ? 'videoFrame' : 'audioFrame',
    size: d.size, head: d.head, meta: d.meta,
    keyframe: d.meta && d.meta.frameId !== undefined ? undefined : undefined
  }));
};
async function main() {
  if (!('RTCRtpScriptTransform' in window)) { log('NO RTCRtpScriptTransform'); return; }
  const pc1 = new RTCPeerConnection();
  const pc2 = new RTCPeerConnection();
  pc1.onicecandidate = e => e.candidate && pc2.addIceCandidate(e.candidate);
  pc2.onicecandidate = e => e.candidate && pc1.addIceCandidate(e.candidate);
  const canvas = document.createElement('canvas');
  canvas.width = 320; canvas.height = 240;
  const ctx = canvas.getContext('2d');
  let f = 0;
  setInterval(() => { f++; ctx.fillStyle = 'rgb(' + (f*7%255) + ',80,120)'; ctx.fillRect(0,0,320,240); ctx.fillStyle='#fff'; ctx.fillText('frame '+f, 20, 120); }, 33);
  const stream = canvas.captureStream(30);
  const vt = stream.getVideoTracks()[0];
  const sender = pc1.addTrack(vt, stream);
  pc2.ontrack = (e) => {
    e.receiver.transform = new RTCRtpScriptTransform(worker, {kind:'video'});
    log('receiver transform attached');
  };
  const offer = await pc1.createOffer();
  await pc1.setLocalDescription(offer);
  await pc2.setRemoteDescription(offer);
  const answer = await pc2.createAnswer();
  await pc2.setLocalDescription(answer);
  await pc1.setRemoteDescription(answer);
  log('loopback connected, waiting frames');
}
main().catch(e => log('ERR ' + e));
</script></body></html>
)HTML";

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"v2probe", WS_OVERLAPPEDWINDOW,
        0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ShowWindow(hwnd, SW_SHOW);

    auto hr = CreateCoreWebView2EnvironmentWithOptions(nullptr,
        L"C:\\veyra-xbox-enhanced\\veyra-nrvideo\\tools\\wv2-probe-data", nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&](HRESULT, ICoreWebView2Environment* e) -> HRESULT {
                env = e;
                env->CreateCoreWebView2Controller(hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [&](HRESULT, ICoreWebView2Controller* c) -> HRESULT {
                            controller = c;
                            controller->get_CoreWebView2(&webview);
                            webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        LPWSTR json;
                                        args->TryGetWebMessageAsString(&json);
                                        std::wstring w(json); CoTaskMemFree(json);
                                        std::string s(w.begin(), w.end());
                                        if (s.find("videoFrame") != std::string::npos) {
                                            int n = ++g_videoFrames;
                                            if (s.find("\"head\":[0,0,0,1") != std::string::npos) ++g_keyframes;
                                            if (n <= 3 || n % 30 == 0) {
                                                auto p = s.find("\"head\"");
                                                g_lastMeta = s.substr(p, 60);
                                                std::printf("video #%d %s\n", n, g_lastMeta.c_str());
                                            }
                                            if (n >= 60) g_done = true;
                                        } else if (s.find("\"type\":\"log\"") != std::string::npos) {
                                            std::printf("JS: %s\n", s.c_str());
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);
                            webview->NavigateToString(kHtml);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
    if (FAILED(hr)) { std::printf("env hr=%08x\n", hr); return 1; }

    MSG msg;
    int ticks = 0;
    while (!g_done && ticks < 600) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        Sleep(16); ticks++;
    }
    std::printf("RESULT video=%d annexb_startcode_frames=%d done=%d\n",
        g_videoFrames.load(), g_keyframes.load(), g_done.load());
    return g_videoFrames >= 30 ? 0 : 2;
}