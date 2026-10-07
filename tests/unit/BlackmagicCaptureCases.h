#pragma once
#include "veyra/source/DirectShowCaptureSetup.h"
#include "veyra/source/CaptureSignalProbe.h"
#include <atomic>

namespace blackmagicTest {
using Microsoft::WRL::ComPtr;
// No physical devices or windows: these peers exercise production topology,
// direction/medium matching and failed connection propagation.
struct Pin final:IPin,IKsPin {
    std::atomic<ULONG> refs{1};PIN_DIRECTION direction;Pin* peer=nullptr;std::vector<REGPINMEDIUM> media;
    explicit Pin(PIN_DIRECTION d,std::vector<REGPINMEDIUM> m={}):direction(d),media(std::move(m)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_IPin)*p=static_cast<IPin*>(this);else if(id==__uuidof(IKsPin))*p=static_cast<IKsPin*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Connect(IPin*,const AM_MEDIA_TYPE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReceiveConnection(IPin*,const AM_MEDIA_TYPE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Disconnect()override{peer=nullptr;return S_OK;}
    HRESULT STDMETHODCALLTYPE ConnectedTo(IPin** p)override{if(!p)return E_POINTER;*p=peer;if(peer){peer->AddRef();return S_OK;}return VFW_E_NOT_CONNECTED;}
    HRESULT STDMETHODCALLTYPE ConnectionMediaType(AM_MEDIA_TYPE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryPinInfo(PIN_INFO*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryDirection(PIN_DIRECTION* p)override{if(!p)return E_POINTER;*p=direction;return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryId(LPWSTR*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryAccept(const AM_MEDIA_TYPE*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE EnumMediaTypes(IEnumMediaTypes**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryInternalConnections(IPin**,ULONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EndOfStream()override{return S_OK;}HRESULT STDMETHODCALLTYPE BeginFlush()override{return S_OK;}HRESULT STDMETHODCALLTYPE EndFlush()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE NewSegment(REFERENCE_TIME,REFERENCE_TIME,double)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE KsQueryMediums(KSMULTIPLE_ITEM** p)override{
        if(!p)return E_POINTER;const size_t size=sizeof(KSMULTIPLE_ITEM)+media.size()*sizeof(REGPINMEDIUM);
        auto* block=static_cast<KSMULTIPLE_ITEM*>(CoTaskMemAlloc(size));if(!block)return E_OUTOFMEMORY;
        block->Size=ULONG(size);block->Count=ULONG(media.size());if(!media.empty())memcpy(block+1,media.data(),media.size()*sizeof(REGPINMEDIUM));*p=block;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE KsQueryInterfaces(KSMULTIPLE_ITEM**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE KsCreateSinkPinHandle(KSPIN_INTERFACE&,KSPIN_MEDIUM&)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE KsGetCurrentCommunication(KSPIN_COMMUNICATION*,KSPIN_INTERFACE*,KSPIN_MEDIUM*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE KsPropagateAcquire()override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE KsDeliver(IMediaSample*,ULONG)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE KsMediaSamplesCompleted(PKSSTREAM_SEGMENT)override{return E_NOTIMPL;}
    IMemAllocator* STDMETHODCALLTYPE KsPeekAllocator(KSPEEKOPERATION)override{return nullptr;}
    HRESULT STDMETHODCALLTYPE KsReceiveAllocator(IMemAllocator*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE KsRenegotiateAllocator()override{return E_NOTIMPL;}
    LONG STDMETHODCALLTYPE KsIncrementPendingIoCount()override{return 0;}LONG STDMETHODCALLTYPE KsDecrementPendingIoCount()override{return 0;}
    HRESULT STDMETHODCALLTYPE KsQualityNotify(ULONG,REFERENCE_TIME)override{return E_NOTIMPL;}
};
struct PinEnum final:IEnumPins {
    ULONG refs=1;size_t at=0;std::vector<ComPtr<IPin>> values;
    explicit PinEnum(std::vector<ComPtr<IPin>> p):values(std::move(p)){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=IID_IEnumPins)return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Next(ULONG n,IPin** p,ULONG* fetched)override{if(!p||(!fetched&&n!=1))return E_POINTER;ULONG count=0;while(count<n&&at<values.size()){p[count]=values[at++].Get();p[count++]->AddRef();}if(fetched)*fetched=count;return count==n?S_OK:S_FALSE;}
    HRESULT STDMETHODCALLTYPE Skip(ULONG n)override{at=std::min(at+size_t(n),values.size());return S_OK;}
    HRESULT STDMETHODCALLTYPE Reset()override{at=0;return S_OK;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumPins** p)override{if(!p)return E_POINTER;auto* clone=new PinEnum(values);clone->at=at;*p=clone;return S_OK;}
};
struct Filter final:IBaseFilter {
    ULONG refs=1;std::vector<ComPtr<IPin>> pinList;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=IID_IBaseFilter&&id!=IID_IMediaFilter&&id!=IID_IPersist)return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Stop()override{return S_OK;}HRESULT STDMETHODCALLTYPE Pause()override{return S_OK;}HRESULT STDMETHODCALLTYPE Run(REFERENCE_TIME)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetState(DWORD,FILTER_STATE* s)override{if(!s)return E_POINTER;*s=State_Stopped;return S_OK;}
    HRESULT STDMETHODCALLTYPE SetSyncSource(IReferenceClock*)override{return S_OK;}HRESULT STDMETHODCALLTYPE GetSyncSource(IReferenceClock**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EnumPins(IEnumPins** p)override{if(!p)return E_POINTER;*p=new PinEnum(pinList);return S_OK;}
    HRESULT STDMETHODCALLTYPE FindPin(LPCWSTR,IPin**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryFilterInfo(FILTER_INFO* p)override{if(!p)return E_POINTER;*p={};return S_OK;}
    HRESULT STDMETHODCALLTYPE JoinFilterGraph(IFilterGraph*,LPCWSTR)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryVendorInfo(LPWSTR*)override{return E_NOTIMPL;}
};
struct Graph final:IFilterGraph {
    ULONG refs=1;unsigned connections=0;HRESULT connectResult=S_OK;IPin* connectedOutput=nullptr;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=IID_IUnknown&&id!=IID_IFilterGraph)return E_NOINTERFACE;*p=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE AddFilter(IBaseFilter*,LPCWSTR)override{return S_OK;}HRESULT STDMETHODCALLTYPE RemoveFilter(IBaseFilter*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE EnumFilters(IEnumFilters**)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE FindFilterByName(LPCWSTR,IBaseFilter**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ConnectDirect(IPin* output,IPin* input,const AM_MEDIA_TYPE*)override{++connections;connectedOutput=output;if(SUCCEEDED(connectResult)){static_cast<Pin*>(input)->peer=static_cast<Pin*>(output);static_cast<Pin*>(output)->peer=static_cast<Pin*>(input);}return connectResult;}
    HRESULT STDMETHODCALLTYPE Reconnect(IPin*)override{return E_NOTIMPL;}HRESULT STDMETHODCALLTYPE Disconnect(IPin*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetDefaultSyncSource()override{return S_OK;}
};
template<class Check>void run(Check check){
    using namespace veyra;using namespace veyra::source;
    const REGPINMEDIUM a{{0xc0ffee,1,2,{3,4,5,6,7,8,9,10}},7,2};auto b=a;b.dw1=8;
    REGPINMEDIUM standard{};standard.clsMedium=KSMEDIUMSETID_Standard;
    check(dshow::sameMedium(a,a)&&!dshow::sameMedium(a,b),"WDM medium includes hardware instance IDs");
    struct {KSMULTIPLE_ITEM header;REGPINMEDIUM m[3];} block{{sizeof(block),3},{standard,b,a}};
    check(dshow::captureMediums(&block.header).size()==2,"WDM skips standard medium and retains all device mediums");
    block.header.Size=sizeof(KSMULTIPLE_ITEM);check(dshow::captureMediums(&block.header).empty(),"WDM rejects truncated medium arrays");
    block.header.Size=sizeof(block);block.header.Count=100000;check(dshow::captureMediums(&block.header).empty(),"WDM rejects oversized medium count");
    ComPtr<Pin> input,wrong,output;input.Attach(new Pin(PINDIR_INPUT,{a}));wrong.Attach(new Pin(PINDIR_OUTPUT,{b}));output.Attach(new Pin(PINDIR_OUTPUT,{standard,b,a}));
    ComPtr<Filter> source,upstream;source.Attach(new Filter);upstream.Attach(new Filter);
    source->pinList={input.Get()};upstream->pinList={input.Get(),wrong.Get(),output.Get()};
    ComPtr<Graph> graph;graph.Attach(new Graph);
    check(dshow::connectUpstream(graph.Get(),upstream.Get(),source.Get())==S_OK&&graph->connections==1&&graph->connectedOutput==output.Get(),"WDM connects matching output, skips wrong direction/card, checks later mediums");
    check(dshow::connectUpstream(graph.Get(),upstream.Get(),source.Get())==S_FALSE&&graph->connections==1,"WDM retains existing upstream connection");
    check(dshow::connectUpstream(graph.Get(),source.Get(),source.Get())==S_FALSE&&graph->connections==1,"WDM never connects a filter to itself");
    input->peer=output->peer=nullptr;graph->connectResult=E_ACCESSDENIED;
    check(dshow::connectUpstream(graph.Get(),upstream.Get(),source.Get())==E_ACCESSDENIED,"WDM connection refusal is propagated, not success");
    upstream->pinList={wrong.Get()};const unsigned calls=graph->connections;
    check(FAILED(dshow::connectUpstream(graph.Get(),upstream.Get(),source.Get()))&&graph->connections==calls,"WDM does not attempt another card's medium");
    ComPtr<IGraphBuilder> realGraph;ComPtr<ICaptureGraphBuilder2> builder;ComPtr<Filter> uvc;uvc.Attach(new Filter);uvc->pinList={output.Get()};
    const bool ready=SUCCEEDED(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&realGraph)))&&SUCCEEDED(CoCreateInstance(CLSID_CaptureGraphBuilder2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&builder)));
    const auto setup=ready?dshow::connectCaptureCrossbar(realGraph.Get(),builder.Get(),uvc.Get()):dshow::CrossbarSetup{E_FAIL};
    check(ready&&setup.hr==S_FALSE&&!setup.filter&&setup.inputMediums==0,"ordinary UVC with no upstream input remains untouched");
    check(dshow::showPropertyPages(nullptr,nullptr,L"test")==E_POINTER&&dshow::showPropertyPages(source.Get(),nullptr,L"test")==E_NOINTERFACE,"missing driver property pages are explicit (no modal window)");
    check(isBlackmagicCaptureDevice(L"Blackmagic WDM Capture")&&isBlackmagicCaptureDevice(L"Decklink Video Capture")&&!isBlackmagicCaptureDevice(L"USB Video"),"two Blackmagic interface names recognized without merging device IDs");
    VIDEOINFOHEADER2 vi{};vi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);vi.bmiHeader.biWidth=4;vi.bmiHeader.biHeight=4;vi.bmiHeader.biSizeImage=48;vi.AvgTimePerFrame=333667;
    AM_MEDIA_TYPE t{};t.majortype=MEDIATYPE_Video;t.formattype=FORMAT_VideoInfo2;t.cbFormat=sizeof(vi);t.pbFormat=reinterpret_cast<BYTE*>(&vi);t.subtype={captureFourcc('H','D','Y','C'),0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
    CaptureMediaLayout l;check(captureMediaLayout(t,l)&&l.packing==CapturePacking::Uyvy&&l.stride==12&&!l.bottomUp&&l.color.matrix==pipeline::YuvMatrix::BT709&&!l.color.matrixAssumed,"HDYC SD dimensions keep BT709 and native padded UYVY");
    const auto pKey=captureFormatIdentity(t);vi.dwInterlaceFlags=AMINTERLACE_IsInterlaced|AMINTERLACE_Field1First;
    const auto iKey=captureFormatIdentity(t);vi.dwInterlaceFlags=AMINTERLACE_IsInterlaced;
    check(pKey!=iKey&&iKey!=captureFormatIdentity(t),"1080p/i and field order have distinct stable identities");
    DXVA2_ExtendedFormat ext{};ext.VideoTransferMatrix=DXVA2_VideoTransferMatrix_BT601;vi.dwControlFlags=ext.value|AMCONTROL_COLORINFO_PRESENT;
    check(captureMediaLayout(t,l)&&l.color.matrix==pipeline::YuvMatrix::BT601,"explicit HDYC color metadata overrides subtype default");vi.dwControlFlags=0;captureMediaLayout(t,l);
    std::vector<uint8_t> bytes(l.sampleBytes,255);for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;x+=2){auto* px=bytes.data()+y*l.stride+x*2;px[0]=px[2]=128;px[1]=px[3]=16;}
    const auto black=sampleCaptureSignal(l,bytes.data(),bytes.size());
    check(black.known&&black.blackLike&&black.maximum==16&&black.points==16,"source probe sees limited-range black and ignores padded bytes");
    CaptureSignalProbeState state;state.observe(black);state.observe(black);check(!state.persistentBlack(),"startup black samples do not immediately warn");state.observe(black);check(state.persistentBlack(),"three low-frequency black samples produce an observation");
    bytes[1]=180;const auto bright=sampleCaptureSignal(l,bytes.data(),bytes.size());state.observe(bright);
    check(bright.known&&!bright.blackLike&&!state.persistentBlack(),"source color immediately clears black observation");
    check(!sampleCaptureSignal(l,bytes.data(),bytes.size()-1).known,"source probe refuses incomplete samples");
    bytes[1]=16;bytes[0]=220;check(!sampleCaptureSignal(l,bytes.data(),bytes.size()).blackLike,"low luma with chroma is not misreported as black");
    t.subtype=MEDIASUBTYPE_ARGB32;vi.bmiHeader.biSizeImage=64;captureMediaLayout(t,l);bytes.assign(l.sampleBytes,0);bytes[2]=200;
    check(!sampleCaptureSignal(l,bytes.data(),bytes.size()).blackLike,"ARGB capture diagnostic ignores unused zero alpha");
    bytes.assign(l.sampleBytes,0);check(sampleCaptureSignal(l,bytes.data(),bytes.size()).blackLike,"RGB zero-alpha black sample remains measurable");
}
}
