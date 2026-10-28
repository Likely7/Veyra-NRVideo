// claude-capture-fps-audio-20261027: what a DirectShow capture card's driver does with a
// smaller buffer request, and how old its frames are when they reach user mode.
//   dshow_latency_probe list <device-substring>
//   dshow_latency_probe run <device-substring> <width> <height> <fps> <NV12|YUY2|MJPG|any> <buffers 0=none> <seconds>
// Age = graph clock at the callback - (explicit run start + sample start time): what the
// driver stamped the frame with versus when it arrives (same measure as Veyra's
// [capture-driver-age], but with an exact run start). Interval stats show queueing:
// a queue delivers in bursts or with ages that step by whole frames.
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
#pragma comment(lib, "strmiids.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
using Microsoft::WRL::ComPtr;

MIDL_INTERFACE("0579154A-2B53-4994-B0D0-E773148EFF85")
ISampleGrabberCB : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample*) = 0;
    virtual HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE*, long) = 0;
};
MIDL_INTERFACE("6B652FFF-11FE-4fce-92AD-0266B5D7C78F")
ISampleGrabber : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long*, long*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB*, long) = 0;
};
static const CLSID kSampleGrabber = {0xC1F400A0, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};
static const CLSID kNullRenderer = {0xC1F400A4, 0x3F08, 0x11d3, {0x9F, 0x0B, 0x00, 0x60, 0x08, 0x03, 0x9E, 0x37}};

GUID fourccGuid(const char* f) { GUID g = MEDIASUBTYPE_YUY2; g.Data1 = DWORD(f[0]) | DWORD(f[1]) << 8 | DWORD(f[2]) << 16 | DWORD(f[3]) << 24; return g; }
std::string fourccName(const GUID& g) {
    if (g == MEDIASUBTYPE_RGB24) return "RGB24"; if (g == MEDIASUBTYPE_RGB32) return "RGB32";
    char s[5] = {char(g.Data1 & 255), char((g.Data1 >> 8) & 255), char((g.Data1 >> 16) & 255), char((g.Data1 >> 24) & 255), 0};
    for (char& c : s) if (c && (c < 32 || c > 126)) c = '?';
    return s;
}
void freeType(AM_MEDIA_TYPE* t) { if (!t) return; if (t->cbFormat) CoTaskMemFree(t->pbFormat); if (t->pUnk) t->pUnk->Release(); CoTaskMemFree(t); }
bool dims(const AM_MEDIA_TYPE* t, LONG& w, LONG& h, REFERENCE_TIME*& interval) {
    if (t->formattype == FORMAT_VideoInfo && t->cbFormat >= sizeof(VIDEOINFOHEADER)) {
        auto* v = reinterpret_cast<VIDEOINFOHEADER*>(t->pbFormat); w = v->bmiHeader.biWidth; h = std::abs(v->bmiHeader.biHeight); interval = &v->AvgTimePerFrame; return true;
    }
    if (t->formattype == FORMAT_VideoInfo2 && t->cbFormat >= sizeof(VIDEOINFOHEADER2)) {
        auto* v = reinterpret_cast<VIDEOINFOHEADER2*>(t->pbFormat); w = v->bmiHeader.biWidth; h = std::abs(v->bmiHeader.biHeight); interval = &v->AvgTimePerFrame; return true;
    }
    return false;
}
double nowMs() { static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f); LARGE_INTEGER c; QueryPerformanceCounter(&c); return double(c.QuadPart) * 1000.0 / double(f.QuadPart); }

ComPtr<IBaseFilter> findDevice(const std::wstring& part, std::wstring& name) {
    ComPtr<ICreateDevEnum> devEnum; ComPtr<IEnumMoniker> e;
    if (FAILED(CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&devEnum))) ||
        devEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &e, 0) != S_OK) return nullptr;
    for (ComPtr<IMoniker> m; e->Next(1, &m, nullptr) == S_OK; m.Reset()) {
        ComPtr<IPropertyBag> bag; VARIANT v; VariantInit(&v);
        if (FAILED(m->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag))) || FAILED(bag->Read(L"FriendlyName", &v, nullptr))) continue;
        std::wstring n = v.bstrVal; VariantClear(&v);
        if (n.find(part) == std::wstring::npos) continue;
        ComPtr<IBaseFilter> f; if (SUCCEEDED(m->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&f)))) { name = n; return f; }
    }
    return nullptr;
}

struct Grab : ISampleGrabberCB {
    std::mutex mutex; std::vector<double> ages, arrivals, streamMs; ComPtr<IReferenceClock> clock; REFERENCE_TIME start = 0;
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; } ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** p) override {
        if (id == IID_IUnknown || id == __uuidof(ISampleGrabberCB)) { *p = this; return S_OK; } *p = nullptr; return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE SampleCB(double, IMediaSample* s) override {
        const double arrival = nowMs(); REFERENCE_TIME now = 0, b = 0, e = 0;
        if (clock && SUCCEEDED(clock->GetTime(&now)) && SUCCEEDED(s->GetTime(&b, &e))) {
            std::lock_guard lock(mutex); ages.push_back(double(now - start - b) / 1e4); arrivals.push_back(arrival); streamMs.push_back(double(b) / 1e4);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferCB(double, BYTE*, long) override { return S_OK; }
};

double pct(std::vector<double> v, double q) { if (v.empty()) return -1; std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, size_t(q * (v.size() - 1) + .5))]; }

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (argc < 3) { std::printf("usage: list <device> | run <device> <w> <h> <fps> <fourcc|any> <buffers> <seconds>\n"); return 1; }
    const std::string mode = argv[1]; std::wstring part(argv[2], argv[2] + strlen(argv[2])), name;
    auto device = findDevice(part, name);
    if (!device) { std::printf("DEVICE not found\n"); return 2; }
    ComPtr<IGraphBuilder> graph; ComPtr<ICaptureGraphBuilder2> builder;
    CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&graph));
    CoCreateInstance(CLSID_CaptureGraphBuilder2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&builder));
    builder->SetFiltergraph(graph.Get()); graph->AddFilter(device.Get(), L"device");
    ComPtr<IPin> pin; if (FAILED(builder->FindPin(device.Get(), PINDIR_OUTPUT, &PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, FALSE, 0, &pin))) { std::printf("PIN none\n"); return 3; }
    ComPtr<IAMStreamConfig> config; pin.As(&config); int count = 0, size = 0; config->GetNumberOfCapabilities(&count, &size);
    std::vector<BYTE> caps(size);
    if (mode == "list") {
        for (int i = 0; i < count; ++i) {
            AM_MEDIA_TYPE* t = nullptr; if (FAILED(config->GetStreamCaps(i, &t, caps.data()))) continue;
            LONG w = 0, h = 0; REFERENCE_TIME* iv = nullptr;
            if (dims(t, w, h, iv)) {
                auto* c = reinterpret_cast<VIDEO_STREAM_CONFIG_CAPS*>(caps.data());
                std::printf("CAP %d %s %ldx%ld fps=%.2f..%.2f sample=%lu\n", i, fourccName(t->subtype).c_str(), w, h,
                            c->MaxFrameInterval ? 1e7 / double(c->MaxFrameInterval) : 0, c->MinFrameInterval ? 1e7 / double(c->MinFrameInterval) : 0, t->lSampleSize);
            }
            freeType(t);
        }
        return 0;
    }
    if (argc < 9) return 1;
    const LONG wantW = atol(argv[3]), wantH = atol(argv[4]); const double fps = atof(argv[5]); const std::string fourcc = argv[6];
    const long buffers = atol(argv[7]); const double seconds = atof(argv[8]);
    AM_MEDIA_TYPE* chosen = nullptr;
    for (int i = 0; i < count && !chosen; ++i) {
        AM_MEDIA_TYPE* t = nullptr; if (FAILED(config->GetStreamCaps(i, &t, caps.data()))) continue;
        LONG w = 0, h = 0; REFERENCE_TIME* iv = nullptr;
        if (dims(t, w, h, iv) && w == wantW && h == wantH && (fourcc == "any" || t->subtype == fourccGuid(fourcc.c_str()))) { *iv = REFERENCE_TIME(1e7 / fps + .5); chosen = t; }
        else freeType(t);
    }
    if (!chosen) { std::printf("FORMAT not offered\n"); return 4; }
    const HRESULT setHr = config->SetFormat(chosen);
    HRESULT suggestHr = S_FALSE;
    if (buffers > 0) {
        ComPtr<IAMBufferNegotiation> negotiation;
        if (SUCCEEDED(pin.As(&negotiation))) { ALLOCATOR_PROPERTIES want{buffers, LONG(chosen->lSampleSize ? chosen->lSampleSize : -1), -1, -1}; suggestHr = negotiation->SuggestAllocatorProperties(&want); }
        else suggestHr = E_NOINTERFACE;
    }
    ComPtr<IBaseFilter> grabFilter, nullFilter; ComPtr<ISampleGrabber> grab;
    CoCreateInstance(kSampleGrabber, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&grabFilter)); grabFilter.As(&grab);
    CoCreateInstance(kNullRenderer, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&nullFilter));
    graph->AddFilter(grabFilter.Get(), L"grab"); graph->AddFilter(nullFilter.Get(), L"null");
    AM_MEDIA_TYPE want{}; want.majortype = MEDIATYPE_Video; want.subtype = chosen->subtype; grab->SetMediaType(&want);
    Grab cb; grab->SetBufferSamples(FALSE); grab->SetOneShot(FALSE); grab->SetCallback(&cb, 0);
    ComPtr<IPin> grabIn, grabOut, nullIn;
    builder->FindPin(grabFilter.Get(), PINDIR_INPUT, nullptr, nullptr, TRUE, 0, &grabIn);
    builder->FindPin(grabFilter.Get(), PINDIR_OUTPUT, nullptr, nullptr, TRUE, 0, &grabOut);
    builder->FindPin(nullFilter.Get(), PINDIR_INPUT, nullptr, nullptr, TRUE, 0, &nullIn);
    HRESULT hr = graph->ConnectDirect(pin.Get(), grabIn.Get(), chosen);
    if (SUCCEEDED(hr)) hr = graph->ConnectDirect(grabOut.Get(), nullIn.Get(), nullptr);
    if (FAILED(hr)) { std::printf("CONNECT hr=0x%08lX\n", hr); return 5; }
    ALLOCATOR_PROPERTIES actual{}; { ComPtr<IMemInputPin> mem; ComPtr<IMemAllocator> alloc; if (SUCCEEDED(grabIn.As(&mem)) && SUCCEEDED(mem->GetAllocator(&alloc))) alloc->GetProperties(&actual); }
    ComPtr<IMediaControl> control; graph.As(&control); ComPtr<IMediaFilter> filter; graph.As(&filter);
    graph->SetDefaultSyncSource(); filter->GetSyncSource(&cb.clock);
    hr = control->Pause(); REFERENCE_TIME now = 0; if (cb.clock) cb.clock->GetTime(&now);
    cb.start = now + 200000; hr = filter->Run(cb.start);
    const double begin = nowMs(); while (nowMs() - begin < seconds * 1000) Sleep(50);
    control->Stop();
    std::vector<double> ages, intervals; double stampRate = 0, arrivalRate = 0, ageDrift = 0;
    { std::lock_guard lock(cb.mutex); ages.assign(cb.ages.begin() + std::min<size_t>(30, cb.ages.size()), cb.ages.end());
        for (size_t i = 31; i < cb.arrivals.size(); ++i) intervals.push_back(cb.arrivals[i] - cb.arrivals[i - 1]);
        const size_t n = cb.arrivals.size();
        if (n > 60) {
            // Frames per second by the driver's stamps and by actual arrival: synthetic
            // stamps (frame count x nominal interval) drift away from real arrivals.
            stampRate = double(n - 31) * 1000.0 / (cb.streamMs[n - 1] - cb.streamMs[30]);
            arrivalRate = double(n - 31) * 1000.0 / (cb.arrivals[n - 1] - cb.arrivals[30]);
            ageDrift = cb.ages[n - 1] - cb.ages[30];
        } }
    size_t bursts = 0; for (double d : intervals) bursts += d < 4.0;
    std::printf("RUN device=\"%ls\" %ldx%ld %s fps=%.2f setFormat=0x%08lX suggest=%ld hr=0x%08lX allocator=%ld run=0x%08lX clock=%d frames=%zu "
                "ageP50=%.2f ageP95=%.2f ageMin=%.2f ageMax=%.2f ageDrift=%.2f stampFps=%.3f arrivalFps=%.3f intervalP50=%.2f intervalP95=%.2f intervalMax=%.2f bursts=%zu\n",
                name.c_str(), wantW, wantH, fourccName(chosen->subtype).c_str(), fps, setHr, buffers, suggestHr, actual.cBuffers, hr, cb.clock ? 1 : 0, ages.size(),
                pct(ages, .5), pct(ages, .95), pct(ages, 0), pct(ages, 1), ageDrift, stampRate, arrivalRate, pct(intervals, .5), pct(intervals, .95), pct(intervals, 1), bursts);
    freeType(chosen);
    return 0;
}
