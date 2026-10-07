#pragma once
#include "veyra/source/CaptureMediaType.h"
#include <vector>

template<class Check> void captureFormatCases(Check check){
    using namespace veyra::source;
    auto fourcc=[](const char* n){return GUID{captureFourcc(n[0],n[1],n[2],n[3]),0,0x10,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};};
    VIDEOINFOHEADER2 vi{};vi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);vi.bmiHeader.biWidth=4;vi.bmiHeader.biHeight=4;vi.AvgTimePerFrame=166667;
    AM_MEDIA_TYPE t{};t.majortype=MEDIATYPE_Video;t.formattype=FORMAT_VideoInfo2;t.pbFormat=reinterpret_cast<BYTE*>(&vi);t.cbFormat=sizeof(vi);
    struct Case{GUID id;int bytes;};
    const Case cases[]={
        {MEDIASUBTYPE_RGB24,48},{MEDIASUBTYPE_RGB32,64},{MEDIASUBTYPE_ARGB32,64},{MEDIASUBTYPE_RGB555,32},{MEDIASUBTYPE_RGB565,32},
        {fourcc("YUY2"),32},{fourcc("UYVY"),32},{fourcc("HDYC"),32},{fourcc("YVYU"),32},
        {fourcc("NV12"),24},{fourcc("NV21"),24},{fourcc("I420"),24},{fourcc("IYUV"),24},{fourcc("YV12"),24},
        {fourcc("P010"),48},{fourcc("P016"),48}};
    for(const auto& item:cases){
        t.subtype=item.id;CaptureMediaLayout l;
        bool ok=captureMediaLayout(t,l);check(ok&&l.sampleBytes==size_t(item.bytes),"raw format negotiated layout/size");
        check(capturePixelName(item.id).find(L'{')==std::wstring::npos,"known raw format has readable name");
        if(!ok)continue;
        std::vector<uint8_t> bytes(l.sampleBytes);for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t(i+1);
        AVFrame* f=av_frame_alloc();f->width=4;f->height=4;f->format=l.format;av_frame_get_buffer(f,32);
        check(copyCaptureSample(l,bytes.data(),bytes.size(),*f),"raw sample copied into owned frame");
        const auto previous=f->data[0][0];bytes[0]=199;
        check(!copyCaptureSample(l,bytes.data(),bytes.size()-1,*f)&&f->data[0][0]==previous,"short sample rejected before writes");bytes[0]=1;
        bool exact=true;
        for(unsigned y=0;y<4;++y){const auto* s=bytes.data()+(l.bottomUp?3-y:y)*l.stride;const auto* d=f->data[0]+y*f->linesize[0];
            // N1: every packed capture format keeps the driver's bytes 1:1;
            // the GPU upload shader does the unpack, so rows compare raw.
            exact&=memcmp(d,s,l.rowBytes)==0;
        }
        for(unsigned p=1;p<l.planes;++p)for(unsigned y=0;y<2;++y){const auto* s=bytes.data()+(p==1?l.chromaOffset:l.secondChromaOffset)+y*l.chromaStride;const auto* d=f->data[p]+y*f->linesize[p];
            if(l.packing==CapturePacking::Nv21)for(unsigned x=0;x<l.chromaRowBytes;x+=2)exact&=d[x]==s[x+1]&&d[x+1]==s[x];else exact&=memcmp(d,s,l.chromaRowBytes)==0;}
        check(exact,"raw plane order, channels and orientation match reference");
        av_frame_free(&f);
    }
    t.subtype=MEDIASUBTYPE_RGB24;vi.bmiHeader.biWidth=3;vi.bmiHeader.biHeight=-2;CaptureMediaLayout l;
    check(captureMediaLayout(t,l)&&l.rowBytes==9&&l.stride==12&&l.sampleBytes==24&&!l.bottomUp,"RGB24 odd width uses DWORD aligned top-down rows");
    vi.bmiHeader.biSizeImage=18;check(!captureMediaLayout(t,l),"RGB24 rejects unaligned declared DIB rows");vi.bmiHeader.biSizeImage=0;
    vi.bmiHeader.biWidth=4;vi.bmiHeader.biHeight=4;t.subtype=fourcc("P010");vi.bmiHeader.biSizeImage=72;
    check(captureMediaLayout(t,l)&&l.stride==12&&l.chromaOffset==48&&l.chromaRowBytes==8,"P010 padded UV starts after full padded Y plane");
    vi.bmiHeader.biSizeImage=66;check(!captureMediaLayout(t,l),"16-bit samples reject odd byte stride");vi.bmiHeader.biSizeImage=0;
    auto other=t;other.subtype=fourcc("P016");check(!equivalentCaptureTypes(t,other),"P010 to P016 requires reconnect despite identical dimensions");
    t.subtype=fourcc("NV12");other=t;other.subtype=fourcc("NV21");check(!equivalentCaptureTypes(t,other),"UV to VU change cannot silently reuse layout");
    t.subtype=fourcc("YV12");check(captureMediaLayout(t,l)&&l.chromaOffset==20&&l.secondChromaOffset==16,"YV12 V precedes U in incoming memory");
    for(const auto id:{MEDIASUBTYPE_RGB555,MEDIASUBTYPE_RGB565}){
        t.subtype=id;captureMediaLayout(t,l);std::vector<uint8_t> raw(l.sampleBytes,255);AVFrame* f=av_frame_alloc();f->width=4;f->height=4;f->format=l.format;av_frame_get_buffer(f,32);
        check(copyCaptureSample(l,raw.data(),raw.size(),*f)&&f->data[0][0]==255&&f->data[0][1]==255,"RGB555/565 keep the driver's white word for the GPU unpack");
        for(unsigned y=0;y<4;++y){raw[y*l.stride]=0;raw[y*l.stride+1]=id==MEDIASUBTYPE_RGB565?0xf8:0x7c;}
        check(copyCaptureSample(l,raw.data(),raw.size(),*f)&&f->data[0][0]==0&&f->data[0][1]==(id==MEDIASUBTYPE_RGB565?0xf8:0x7c),"RGB555/565 red mask bytes are preserved for the GPU unpack");av_frame_free(&f);
    }
    // N1: the packed capture contract and the legacy CPU-unpack rollback.
    {
        vi.bmiHeader.biWidth=4;vi.bmiHeader.biHeight=4;vi.bmiHeader.biSizeImage=0;t.subtype=MEDIASUBTYPE_RGB565;
        CaptureMediaLayout packed;check(captureMediaLayout(t,packed)&&packed.format==AV_PIX_FMT_RGB565LE,"N1 keeps the driver packing in the frame contract");
        CaptureMediaLayout legacy=packed;check(captureLegacyCpuLayout(legacy)&&legacy.format==AV_PIX_FMT_BGR0,"legacy CPU-unpack maps RGB565 back to BGR0");
        std::vector<uint8_t> raw(legacy.sampleBytes,255);AVFrame* f=av_frame_alloc();f->width=4;f->height=4;f->format=legacy.format;av_frame_get_buffer(f,16);
        check(copyCaptureSample(legacy,raw.data(),raw.size(),*f)&&f->data[0][0]==255&&f->data[0][2]==255&&f->data[0][3]==255,"legacy CPU-unpack still expands RGB565 to opaque BGR0");av_frame_free(&f);
        t.subtype=fourcc("UYVY");CaptureMediaLayout uy;check(captureMediaLayout(t,uy)&&uy.format==AV_PIX_FMT_UYVY422,"N1 keeps UYVY packing in the frame contract");
        CaptureMediaLayout uyLegacy=uy;check(captureLegacyCpuLayout(uyLegacy)&&uyLegacy.format==AV_PIX_FMT_YUYV422,"legacy CPU-unpack maps UYVY back to YUY2");vi.bmiHeader.biWidth=4;
    }
    // Orientation contract: the DIB sign decides whether ingest flips, the
    // manual override inverts that decision, and planar chroma follows luma.
    {
        vi.bmiHeader.biWidth=4;vi.bmiHeader.biHeight=4;vi.bmiHeader.biSizeImage=0;t.subtype=MEDIASUBTYPE_RGB24;CaptureMediaLayout rgb;
        check(captureMediaLayout(t,rgb),"RGB24 bottom-up layout for the orientation cases");
        std::vector<uint8_t> raw(rgb.sampleBytes,0);
        for(unsigned y=0;y<4;++y)raw[size_t(y)*rgb.stride]=uint8_t(y+1);
        AVFrame* f=av_frame_alloc();f->width=4;f->height=4;f->format=rgb.format;av_frame_get_buffer(f,32);
        check(rgb.bottomUp&&copyCaptureSample(rgb,raw.data(),raw.size(),*f)&&f->data[0][0]==4,"bottom-up RGB24 ingest reads the last memory row first");
        check(copyCaptureSample(rgb,raw.data(),raw.size(),*f,true)&&f->data[0][0]==1,"manual flip inverts bottom-up RGB24 ingest");
        vi.bmiHeader.biHeight=-4;
        check(captureMediaLayout(t,rgb)&&!rgb.bottomUp&&copyCaptureSample(rgb,raw.data(),raw.size(),*f)&&f->data[0][0]==1,"top-down RGB24 ingest passes rows through");
        check(copyCaptureSample(rgb,raw.data(),raw.size(),*f,true)&&f->data[0][0]==4,"manual flip inverts top-down RGB24 ingest");
        av_frame_free(&f);
        t.subtype=fourcc("NV12");vi.bmiHeader.biWidth=4;vi.bmiHeader.biHeight=4;vi.bmiHeader.biSizeImage=0;CaptureMediaLayout yuv;
        check(captureMediaLayout(t,yuv),"NV12 layout for the chroma flip case");
        std::vector<uint8_t> plane(yuv.sampleBytes,0);plane[yuv.chromaOffset]=10;plane[yuv.chromaOffset+yuv.chromaStride]=11;
        AVFrame* g=av_frame_alloc();g->width=4;g->height=4;g->format=yuv.format;av_frame_get_buffer(g,32);
        check(copyCaptureSample(yuv,plane.data(),plane.size(),*g)&&g->data[1][0]==10,"YUV ingest stays top-down without the override");
        check(copyCaptureSample(yuv,plane.data(),plane.size(),*g,true)&&g->data[1][0]==11,"manual flip reverses chroma rows too");
        av_frame_free(&g);
    }
    t.subtype=fourcc("P010");DXVA2_ExtendedFormat ext{};ext.VideoTransferFunction=16;vi.dwControlFlags=ext.value|AMCONTROL_COLORINFO_PRESENT;
    check(captureMediaLayout(t,l)&&l.color.transfer==veyra::pipeline::TransferFunction::HLG&&!l.color.transferAssumed,"capture HLG metadata is retained, never silently interpreted as SDR");vi.dwControlFlags=0;
    t.subtype=fourcc("ABCD");check(capturePixelName(t.subtype)==L"ABCD"&&!captureMediaLayout(t,l),"unknown FourCC readable but not advertised as native");
    t.subtype.Data2=3;check(capturePixelName(t.subtype).find(L"未知格式")!=std::wstring::npos&&!captureMediaLayout(t,l),"private GUID cannot alias standard FourCC");
}
