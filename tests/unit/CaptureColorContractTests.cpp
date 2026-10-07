#include "veyra/source/CaptureMediaType.h"
#include "veyra/source/NativeCaptureSink.h"
#include "CaptureFormatCases.h"
#include "ElgatoHdrCases.h"
#include "BlackmagicCaptureCases.h"
#include "veyra/sink/AudioFormat.h"
#include <iostream>
#include <vector>
#include <atomic>
using namespace veyra;
using Microsoft::WRL::ComPtr;
// Minimal upstream peer: the production input pin must validate its direction.
struct OutputPin final:IPin,IAMBufferNegotiation {
    std::atomic<ULONG> refs{1};
    bool supportsBuffering=false;HRESULT bufferResult=S_OK;unsigned suggestions=0;ALLOCATOR_PROPERTIES suggested{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** p)override{if(!p)return E_POINTER;*p=nullptr;if(id==IID_IUnknown||id==IID_IPin)*p=static_cast<IPin*>(this);else if(id==IID_IAMBufferNegotiation&&supportsBuffering)*p=static_cast<IAMBufferNegotiation*>(this);else return E_NOINTERFACE;AddRef();return S_OK;}
    HRESULT STDMETHODCALLTYPE SuggestAllocatorProperties(const ALLOCATOR_PROPERTIES* p)override{if(!p)return E_POINTER;suggested=*p;++suggestions;return bufferResult;}
    HRESULT STDMETHODCALLTYPE GetAllocatorProperties(ALLOCATOR_PROPERTIES* p)override{if(!p)return E_POINTER;*p=suggested;return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Connect(IPin*,const AM_MEDIA_TYPE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ReceiveConnection(IPin*,const AM_MEDIA_TYPE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Disconnect()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE ConnectedTo(IPin**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE ConnectionMediaType(AM_MEDIA_TYPE*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryPinInfo(PIN_INFO*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryDirection(PIN_DIRECTION* p)override{if(!p)return E_POINTER;*p=PINDIR_OUTPUT;return S_OK;}
    HRESULT STDMETHODCALLTYPE QueryId(LPWSTR*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryAccept(const AM_MEDIA_TYPE*)override{return S_OK;}
    HRESULT STDMETHODCALLTYPE EnumMediaTypes(IEnumMediaTypes**)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE QueryInternalConnections(IPin**,ULONG*)override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE EndOfStream()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE BeginFlush()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE EndFlush()override{return S_OK;}
    HRESULT STDMETHODCALLTYPE NewSegment(REFERENCE_TIME,REFERENCE_TIME,double)override{return S_OK;}
};
int main(){
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);int failures=0;
    auto check=[&](bool pass,const char* name){std::cout<<(pass?"PASS ":"FAIL ")<<name<<'\n';if(!pass)++failures;};
    captureFormatCases(check);
    blackmagicTest::run(check);
    elgatoHdrCases(check);
    VIDEOINFOHEADER2 vi{};vi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);vi.bmiHeader.biWidth=4;vi.bmiHeader.biHeight=2;vi.bmiHeader.biBitCount=16;vi.bmiHeader.biSizeImage=24;vi.AvgTimePerFrame=166667;
    AM_MEDIA_TYPE type{};type.majortype=MEDIATYPE_Video;type.subtype=MEDIASUBTYPE_YUY2;type.formattype=FORMAT_VideoInfo2;type.pbFormat=reinterpret_cast<BYTE*>(&vi);type.cbFormat=sizeof(vi);
    source::CaptureMediaLayout layout;check(source::captureMediaLayout(type,layout)&&layout.stride==12&&layout.rowBytes==8&&!layout.bottomUp,"VideoInfo2 YUY2 padded row contract");
    check(layout.color.transfer==pipeline::TransferFunction::SRGB&&layout.color.transferAssumed,"display referred default remains explicit assumption");
    AVFrame* frame=av_frame_alloc();frame->format=AV_PIX_FMT_YUYV422;frame->width=4;frame->height=2;av_frame_get_buffer(frame,32);
    std::vector<uint8_t> raw(24,0xee);for(int y=0;y<2;++y)for(int x=0;x<8;++x)raw[y*12+x]=uint8_t(y*32+x);
    check(source::copyCaptureSample(layout,raw.data(),raw.size(),*frame)&&frame->data[0][0]==0&&frame->data[0][frame->linesize[0]]==32,"YUY2 positive height is top down; exclude padding");
    check(!source::copyCaptureSample(layout,raw.data(),23,*frame),"reject short sample before any copy");
    vi.bmiHeader.biHeight=-2;check(source::captureMediaLayout(type,layout)&&!layout.bottomUp,"negative YUY2 also top down");vi.bmiHeader.biHeight=2;
    DXVA2_ExtendedFormat ext{};ext.NominalRange=DXVA2_NominalRange_0_255;ext.VideoTransferMatrix=DXVA2_VideoTransferMatrix_BT709;vi.dwControlFlags=ext.value|AMCONTROL_COLORINFO_PRESENT;
    check(source::captureMediaLayout(type,layout)&&layout.color.range==pipeline::ColorRange::Full&&!layout.color.rangeAssumed&&layout.color.matrix==pipeline::YuvMatrix::BT709&&!layout.color.matrixAssumed,"honor explicit matrix and range");
    auto resolved=pipeline::resolveFrameColor(*frame,layout.color);check(resolved.transfer==pipeline::TransferFunction::SRGB&&resolved.transferAssumed&&resolved.range==pipeline::ColorRange::Full,"source contract survives graph resolution");
    ext.VideoTransferFunction=DXVA2_VideoTransFunc_709;vi.dwControlFlags=ext.value|AMCONTROL_COLORINFO_PRESENT;
    check(source::captureMediaLayout(type,layout)&&layout.color.preserveSdrCodeValues&&!layout.color.transferAssumed&&pipeline::workingTransferCode(layout.color)==1,"explicit BT709 metadata retained; working decode reverses the SDR sink encode");
    ext.VideoTransferFunction=DXVA2_VideoTransFunc_Unknown;
    ext.NominalRange=DXVA2_NominalRange_48_208;vi.dwControlFlags=ext.value|AMCONTROL_COLORINFO_PRESENT;check(!source::captureMediaLayout(type,layout),"unsupported range fails closed");vi.dwControlFlags=0;
    vi.bmiHeader.biWidth=3;check(!source::captureMediaLayout(type,layout),"odd YUY2 width rejected");vi.bmiHeader.biWidth=4;
    vi.bmiHeader.biSizeImage=23;check(!source::captureMediaLayout(type,layout),"ambiguous byte pitch rejected");vi.bmiHeader.biSizeImage=24;
    ComPtr<IBaseFilter> filter;ComPtr<IPin> input,output;ComPtr<IMemInputPin> memory;ComPtr<IMemAllocator> allocator;
    output.Attach(new OutputPin);unsigned delivered=0;
    check(SUCCEEDED(source::createNativeCaptureSink(type,[&](IMediaSample* sample){BYTE* bytes=nullptr;sample->GetPointer(&bytes);++delivered;return bytes?S_OK:E_FAIL;},filter,input)),"create native sink (no SampleGrabber dependency)");
    check(filter&&SUCCEEDED(filter.As(&memory))&&input->ReceiveConnection(output.Get(),&type)==S_OK,"native input directly accepts VideoInfo2");
    vi.bmiHeader.biSizeImage=32;check(input->QueryAccept(&type)==S_FALSE,"connected pin rejects same-size incompatible stride");vi.bmiHeader.biSizeImage=24;
    check(input->QueryAccept(&type)==S_OK,"connected pin accepts equivalent type");
    if(memory){
        ALLOCATOR_PROPERTIES wanted{},actual{};check(memory->GetAllocatorRequirements(&wanted)==S_OK&&wanted.cbBuffer==24,"allocator size includes negotiated padding");
        bool ready=memory->GetAllocator(&allocator)==S_OK&&allocator->SetProperties(&wanted,&actual)==S_OK&&allocator->Commit()==S_OK;
        check(ready,"owned sample allocator");
        if(ready){ComPtr<IMediaSample> sample;allocator->GetBuffer(&sample,nullptr,nullptr,0);BYTE* bytes=nullptr;sample->GetPointer(&bytes);memcpy(bytes,raw.data(),24);sample->SetActualDataLength(24);
            check(memory->Receive(sample.Get())==VFW_E_WRONG_STATE&&delivered==0,"stopped sink does not invoke callback");filter->Run(0);
            sample->SetMediaType(&type);check(memory->Receive(sample.Get())==S_OK&&delivered==1,"initial repeated media type accepted");sample->SetMediaType(nullptr);
            input->BeginFlush();check(memory->Receive(sample.Get())==S_FALSE&&delivered==1,"flush does not deliver");input->EndFlush();
            sample->SetActualDataLength(23);check(FAILED(memory->Receive(sample.Get()))&&delivered==1,"short native sample rejected");sample->SetActualDataLength(24);
            vi.bmiHeader.biWidth=8;sample->SetMediaType(&type);check(memory->Receive(sample.Get())==VFW_E_INVALIDMEDIATYPE&&delivered==1,"dynamic mode cannot reuse stale layout");sample->SetMediaType(nullptr);vi.bmiHeader.biWidth=4;
            check(memory->Receive(sample.Get())==S_OK&&delivered==2,"callback recovers for unchanged sample");
            filter->Stop();sample.Reset();allocator->Decommit();
        }
        check(input->Disconnect()==S_OK,"disconnect releases allocator and upstream peer");
    }
    memory.Reset();input.Reset();filter.Reset();output.Reset();allocator.Reset();av_frame_free(&frame);
    // NV12 uses separate planes; padding must not be copied into the UV plane.
    type.subtype=MEDIASUBTYPE_NV12;vi.bmiHeader.biHeight=4;vi.bmiHeader.biSizeImage=36;
    check(source::captureMediaLayout(type,layout)&&layout.stride==6&&layout.sampleBytes==36,"NV12 plane pitch derived without RGB conversion");
    frame=av_frame_alloc();frame->format=AV_PIX_FMT_NV12;frame->width=4;frame->height=4;av_frame_get_buffer(frame,32);raw.assign(36,0xee);for(int y=0;y<6;++y)for(int x=0;x<4;++x)raw[y*6+x]=uint8_t(y*10+x);
    check(source::copyCaptureSample(layout,raw.data(),raw.size(),*frame)&&frame->data[1][0]==40&&frame->data[1][frame->linesize[1]]==50,"NV12 UV offset respects input stride");av_frame_free(&frame);
    type.subtype=MEDIASUBTYPE_RGB32;vi.bmiHeader.biHeight=2;vi.bmiHeader.biSizeImage=32;check(source::captureMediaLayout(type,layout)&&layout.bottomUp&&layout.rowBytes==16,"RGB DIB keeps bottom-up compatibility");
    WAVEFORMATEX wave{};wave.wFormatTag=WAVE_FORMAT_PCM;wave.nChannels=2;wave.nSamplesPerSec=48000;wave.wBitsPerSample=16;wave.nBlockAlign=4;wave.nAvgBytesPerSec=192000;
    AM_MEDIA_TYPE audio{};audio.majortype=MEDIATYPE_Audio;audio.subtype=MEDIASUBTYPE_PCM;audio.formattype=FORMAT_WaveFormatEx;audio.cbFormat=sizeof(wave);audio.pbFormat=reinterpret_cast<BYTE*>(&wave);
    output.Attach(new OutputPin);unsigned audioReceived=0;
    auto* audioPeer=static_cast<OutputPin*>(output.Get());
    check(source::suggestCaptureAudioBuffering(output.Get(),wave)==E_NOINTERFACE,"missing upstream negotiation reports fallback without requesting allocator");
    audioPeer->supportsBuffering=true;
    check(source::suggestCaptureAudioBuffering(output.Get(),wave)==S_OK&&audioPeer->suggestions==1&&audioPeer->suggested.cbBuffer==1920&&audioPeer->suggested.cBuffers==-1&&audioPeer->suggested.cbAlign==-1&&audioPeer->suggested.cbPrefix==-1,"upstream 48k stereo16 request is 10ms with driver-owned pool and alignment");
    auto unusual=wave;unusual.nSamplesPerSec=11025;unusual.nAvgBytesPerSec=44100;
    check(source::suggestCaptureAudioBuffering(output.Get(),unusual)==S_OK&&audioPeer->suggested.cbBuffer==444,"uncommon rate rounds up to whole PCM frames");
    auto floatStereo=wave;floatStereo.wFormatTag=WAVE_FORMAT_IEEE_FLOAT;floatStereo.wBitsPerSample=32;floatStereo.nBlockAlign=8;floatStereo.nAvgBytesPerSec=384000;
    check(source::suggestCaptureAudioBuffering(output.Get(),floatStereo)==S_OK&&audioPeer->suggested.cbBuffer==3840,"float stereo uses byte rate rather than hardcoded 1920 bytes");
    audioPeer->bufferResult=E_FAIL;
    check(source::suggestCaptureAudioBuffering(output.Get(),wave)==E_FAIL,"driver rejection is visible to diagnostics");
    check(SUCCEEDED(source::createNativeAudioSink(audio,[&](IMediaSample*){++audioReceived;return S_OK;},filter,input)),"create shared native PCM terminal");
    check(filter&&SUCCEEDED(filter.As(&memory))&&input->ReceiveConnection(output.Get(),&audio)==S_OK,"PCM connection remains supported after rejected advisory request");
    wave.nSamplesPerSec=44100;wave.nAvgBytesPerSec=176400;check(input->QueryAccept(&audio)==S_FALSE,"audio rate change requires reconnect");wave.nSamplesPerSec=48000;wave.nAvgBytesPerSec=192000;
    ALLOCATOR_PROPERTIES audioWanted{},audioActual{};
    check(memory->GetAllocatorRequirements(&audioWanted)==S_OK&&audioWanted.cbBuffer==1920&&audioWanted.cbBuffer%wave.nBlockAlign==0,"audio allocator requests aligned 10ms blocks instead of a single sample");
    audioWanted.cbBuffer=1920;
    bool audioReady=memory->GetAllocator(&allocator)==S_OK&&allocator->SetProperties(&audioWanted,&audioActual)==S_OK&&allocator->Commit()==S_OK;
    check(audioReady,"PCM allocator initialized");
    if(audioReady){ComPtr<IMediaSample> sample;allocator->GetBuffer(&sample,nullptr,nullptr,0);sample->SetActualDataLength(1920);filter->Run(0);
        check(memory->Receive(sample.Get())==S_OK&&audioReceived==1,"variable length aligned PCM delivered");
        sample->SetActualDataLength(1919);check(FAILED(memory->Receive(sample.Get()))&&audioReceived==1,"partial PCM sample is rejected");
        filter->Stop();sample.Reset();allocator->Decommit();input->Disconnect();
    }
    memory.Reset();input.Reset();filter.Reset();output.Reset();allocator.Reset();
    {
        auto hdrVi=vi;hdrVi.bmiHeader.biHeight=4;hdrVi.bmiHeader.biSizeImage=48;hdrVi.bmiHeader.biBitCount=24;
        auto hdrType=type;hdrType.subtype={source::captureFourcc('P','0','1','0'),0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};hdrType.pbFormat=reinterpret_cast<BYTE*>(&hdrVi);
        DXVA2_ExtendedFormat hdrFlags{};hdrFlags.NominalRange=DXVA2_NominalRange_16_235;hdrFlags.VideoTransferMatrix=4;hdrFlags.VideoPrimaries=9;
        for(unsigned transfer:{15u,16u}){hdrFlags.VideoTransferFunction=transfer;hdrVi.dwControlFlags=hdrFlags.value|AMCONTROL_COLORINFO_PRESENT;
            check(source::captureMediaLayout(hdrType,layout)&&layout.color.isHdrPath()&&layout.color.primaries==pipeline::ColorPrimaries::BT2020&&layout.color.matrix==pipeline::YuvMatrix::BT2020NCL&&!layout.color.transferAssumed,"capture negotiates explicit PQ/HLG BT2020 metadata");
        }
        hdrFlags.VideoTransferMatrix=0;hdrFlags.VideoPrimaries=0;
        for(unsigned transfer:{15u,16u}){hdrFlags.VideoTransferFunction=transfer;hdrVi.dwControlFlags=hdrFlags.value|AMCONTROL_COLORINFO_PRESENT;
            check(source::captureMediaLayout(hdrType,layout)&&layout.color.isHdrPath()&&layout.color.matrix==pipeline::YuvMatrix::BT2020NCL&&layout.color.primaries==pipeline::ColorPrimaries::BT2020&&layout.color.matrixAssumed&&layout.color.primariesAssumed&&!layout.color.transferAssumed,"explicit HDR transfer supplies labelled BT2100 fallback for missing gamut");
        }
        hdrFlags.VideoTransferMatrix=DXVA2_VideoTransferMatrix_BT709;hdrFlags.VideoPrimaries=DXVA2_VideoPrimaries_BT709;hdrVi.dwControlFlags=hdrFlags.value|AMCONTROL_COLORINFO_PRESENT;
        check(source::captureMediaLayout(hdrType,layout)&&layout.color.matrix==pipeline::YuvMatrix::BT709&&layout.color.primaries==pipeline::ColorPrimaries::BT709&&!layout.color.primariesAssumed,"explicit conflicting gamut is never overwritten by HDR fallback");
        hdrVi.dwControlFlags=0;
        check(source::captureMediaLayout(hdrType,layout)&&!layout.color.isHdrPath()&&layout.color.transferAssumed,"untagged P010 is not proof of HDR");
        for(auto [code,location]:{std::pair{DXVA2_VideoChromaSubsampling_MPEG2,pipeline::ChromaLocation::Left},std::pair{DXVA2_VideoChromaSubsampling_MPEG1,pipeline::ChromaLocation::Center},std::pair{DXVA2_VideoChromaSubsampling_Cosited,pipeline::ChromaLocation::TopLeft}}){
            hdrFlags.VideoChromaSubsampling=code|DXVA2_VideoChromaSubsampling_ProgressiveChroma;hdrVi.dwControlFlags=hdrFlags.value|AMCONTROL_COLORINFO_PRESENT;
            check(source::captureMediaLayout(hdrType,layout)&&layout.color.chromaLocation==location,"capture preserves explicit chroma siting");
            auto otherVi=hdrVi;otherVi.dwControlFlags&=~(15u<<8);auto otherType=hdrType;otherType.pbFormat=reinterpret_cast<BYTE*>(&otherVi);
            check(!source::equivalentCaptureTypes(hdrType,otherType),"chroma siting change cannot reuse stale capture contract");
        }
        auto multi=sink::floatWave({6,0x60f});AM_MEDIA_TYPE mt=audio;mt.subtype=KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;mt.cbFormat=sizeof(multi);mt.pbFormat=reinterpret_cast<BYTE*>(&multi);
        output.Attach(new OutputPin);auto* peer=static_cast<OutputPin*>(output.Get());peer->supportsBuffering=true;
        check(source::suggestCaptureAudioBuffering(output.Get(),multi.Format)==S_OK&&peer->suggested.cbBuffer==11520,"six-channel float upstream negotiation retains 10ms target");
        check(SUCCEEDED(source::createNativeAudioSink(mt,[](IMediaSample*){return S_OK;},filter,input))&&input->ReceiveConnection(output.Get(),&mt)==S_OK,"native sink directly connects six-channel extensible PCM");
        auto changed=multi;changed.dwChannelMask=0x3f;auto alternative=mt;alternative.pbFormat=reinterpret_cast<BYTE*>(&changed);
        check(input->QueryAccept(&alternative)==S_FALSE,"side-to-back layout change requires reconnect even at identical byte stride");
        input->Disconnect();input.Reset();filter.Reset();output.Reset();
    }
    {
        AM_MEDIA_TYPE compressed{};compressed.majortype=MEDIATYPE_Audio;
        compressed.subtype={0x2000,0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
        compressed.formattype=FORMAT_None;
        unsigned packets=0;output.Attach(new OutputPin);
        check(source::createNativeAudioSink(compressed,[](IMediaSample*){return S_OK;},filter,input)==VFW_E_INVALIDMEDIATYPE,"PCM terminal rejects compressed packets");
        check(SUCCEEDED(source::createBitstreamAudioSink(compressed,[&](IMediaSample*){++packets;return S_OK;},filter,input)),"compressed audio terminal accepts AC3 without PCM WAVEFORMAT");
        if(filter&&input){
            check(input->ReceiveConnection(output.Get(),&compressed)==S_OK&&SUCCEEDED(filter.As(&memory)),"compressed audio connects directly");
            ALLOCATOR_PROPERTIES wanted{},actual{};memory->GetAllocatorRequirements(&wanted);
            const bool ready=memory->GetAllocator(&allocator)==S_OK&&allocator->SetProperties(&wanted,&actual)==S_OK&&allocator->Commit()==S_OK;
            check(ready,"compressed allocator initializes without PCM byte rate");
            if(ready){
                ComPtr<IMediaSample> sample;allocator->GetBuffer(&sample,nullptr,nullptr,0);filter->Run(0);
                sample->SetActualDataLength(101);check(memory->Receive(sample.Get())==S_OK&&packets==1,"odd-sized untimed compressed packet delivered");
                sample->SetActualDataLength(0);check(FAILED(memory->Receive(sample.Get()))&&packets==1,"empty compressed packet rejected");
                auto changed=compressed;changed.subtype.Data1=8;check(input->QueryAccept(&changed)==S_FALSE,"codec change requires reconnect");
                filter->Stop();sample.Reset();allocator->Decommit();
            }
            input->Disconnect();
        }
        memory.Reset();input.Reset();filter.Reset();output.Reset();allocator.Reset();
        compressed.subtype.Data1=0xdeadbeef;
        check(source::createBitstreamAudioSink(compressed,[](IMediaSample*){return S_OK;},filter,input)==VFW_E_INVALIDMEDIATYPE,"unknown codec rejected");
    }
    CoUninitialize();std::cout<<"failures="<<failures<<'\n';return failures?1:0;
}
