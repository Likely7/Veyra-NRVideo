// v2 probe 4: bandwidth of the WebMessage frame export path under real-video-like load.
// Sends ~25KB frames at 60fps for 5s, measures C++ receive rate + total bytes.
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <cstdio>
#include <atomic>
#include <chrono>

using namespace Microsoft::WRL;
static std::atomic<uint64_t> g_frames{0}, g_bytes{0};
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
    while (true) {
      const {done, value} = await reader.read();
      if (done) break;
      const meta = value.getMetadata ? value.getMetadata() : {};
      const buf = new Uint8Array(value.data);
      let bin = '';
      for (let i = 0; i < buf.length; i++) bin += String.fromCharCode(buf[i]);
      self.postMessage({b64: btoa(bin), meta, size: buf.length});
      await writer.write(value);
    }
  })();
};
`;
const worker = new Worker(URL.createObjectURL(new Blob([workerCode],{type:'application/javascript'})));
let jsCount = 0, jsBytes = 0, lastReport = performance.now();
worker.onmessage = (e) => {
  jsCount++; jsBytes += e.data.size;
  if (performance.now() - lastReport > 2000) {
    log('JS produced ' + jsCount + ' frames, ' + (jsBytes/1e6).toFixed(2) + ' MB raw, avg ' + Math.round(jsBytes/jsCount) + ' B/frame');
    lastReport = performance.now();
  }
  chrome.webview.postMessage(JSON.stringify({type:'videoFrame', b64:e.data.b64, meta:e.data.meta, size:e.data.size}));
};
async function main() {
  const pc1 = new RTCPeerConnection();
  const pc2 = new RTCPeerConnection();
  pc1.onicecandidate = e => e.candidate && pc2.addIceCandidate(e.candidate);
  pc2.onicecandidate = e => e.candidate && pc1.addIceCandidate(e.candidate);
  const canvas = document.createElement('canvas');
  canvas.width = 1920; canvas.height = 1080;
  const ctx = canvas.getContext('2d');
  let f = 0;
  // High-entropy content to force large encoded frames
  setInterval(() => {
    f++;
    const g = ctx.createLinearGradient(0,0,1920,1080);
    g.addColorStop(0, 'hsl('+(f*3%360)+',70%,40%)');
    g.addColorStop(1, 'hsl('+((f*3+120)%360)+',70%,60%)');
    ctx.fillStyle=g; ctx.fillRect(0,0,1920,1080);
    ctx.fillStyle='#fff'; ctx.font='80px sans-serif';
    ctx.fillText('frame '+f, 100+(f%200), 500);
  }, 16);
  const stream = canvas.captureStream(60);
  const tr = pc1.addTransceiver(stream.getVideoTracks()[0], {direction:'sendonly'});
  const caps = RTCRtpSender.getCapabilities('video');
  tr.setCodecPreferences(caps.codecs.filter(c=>c.mimeType==='video/H264'));
  // crank bitrate
  const params = tr.sender.getParameters();
  params.encodings = [{maxBitrate: 20000000}];
  await tr.sender.setParameters(params);
  pc2.ontrack = (e) => { e.receiver.transform = new RTCRtpScriptTransform(worker, {kind:'video'}); };
  const offer = await pc1.createOffer();
  await pc1.setLocalDescription(offer);
  await pc2.setRemoteDescription(offer);
  const answer = await pc2.createAnswer();
  await pc2.setLocalDescription(answer);
  await pc1.setRemoteDescription(answer);
  log('connected 1080p60 high-bitrate');
}
main().catch(e => log('ERR '+e));
</script></body></html>
)HTML";

int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComPtr<ICoreWebView2Environment> env;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"v2probe4", WS_OVERLAPPEDWINDOW, 0,0,400,300, nullptr,nullptr, GetModuleHandleW(nullptr), nullptr);
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
                                        if (w.find(L"videoFrame") != std::wstring::npos) {
                                            ++g_frames; g_bytes += w.size();
                                        } else {
                                            std::string s(w.begin(), w.end());
                                            std::printf("JS: %s\n", s.c_str());
                                        }
                                        return S_OK;
                                    }).Get(), nullptr);
                            webview->NavigateToString(kHtml);
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());
    auto t0 = std::chrono::steady_clock::now();
    MSG msg;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(6)) {
        while (PeekMessageW(&msg, nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(1);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("RESULT frames=%llu fps=%.1f msgMB=%.1f throughputMBps=%.1f\n",
        (unsigned long long)g_frames.load(), g_frames.load()/secs,
        g_bytes.load()/1e6, g_bytes.load()/1e6/secs);
    return 0;
}