#pragma once
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/source/CapturePixelFormat.h"
#include <windows.h>
#include <dshow.h>
#include <dvdmedia.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <cstring>
#include <limits>
#include <cmath>

namespace veyra::source {
struct CaptureMediaLayout {
    unsigned width=0,height=0,stride=0,rowBytes=0;
    size_t sampleBytes=0;
    bool bottomUp=false;
    AVPixelFormat format=AV_PIX_FMT_NONE;
    REFERENCE_TIME duration=0;
    DWORD colorControlFlags=0;
    pipeline::ColorDescription color;
    CapturePacking packing=CapturePacking::Unknown;
    unsigned planes=1,chromaStride=0,chromaRowBytes=0;
    size_t chromaOffset=0,secondChromaOffset=0;
};
// Walk the AM_MEDIA_TYPE's format block without assuming VideoInfo vs
// VideoInfo2; used to read/rewrite the DIB orientation flag in place.
inline BITMAPINFOHEADER* captureBitmapHeader(AM_MEDIA_TYPE& type){
    if(!type.pbFormat)return nullptr;
    if(type.formattype==FORMAT_VideoInfo&&type.cbFormat>=sizeof(VIDEOINFOHEADER))return &reinterpret_cast<VIDEOINFOHEADER*>(type.pbFormat)->bmiHeader;
    if(type.formattype==FORMAT_VideoInfo2&&type.cbFormat>=sizeof(VIDEOINFOHEADER2))return &reinterpret_cast<VIDEOINFOHEADER2*>(type.pbFormat)->bmiHeader;
    return nullptr;
}
inline std::wstring captureFormatIdentity(const AM_MEDIA_TYPE& type){
    if(!type.pbFormat)return {};
    const BITMAPINFOHEADER* bitmap=nullptr;REFERENCE_TIME duration=0;
    if(type.formattype==FORMAT_VideoInfo&&type.cbFormat>=sizeof(VIDEOINFOHEADER)){
        const auto& v=*reinterpret_cast<const VIDEOINFOHEADER*>(type.pbFormat);bitmap=&v.bmiHeader;duration=v.AvgTimePerFrame;
    }else if(type.formattype==FORMAT_VideoInfo2&&type.cbFormat>=sizeof(VIDEOINFOHEADER2)){
        const auto& v=*reinterpret_cast<const VIDEOINFOHEADER2*>(type.pbFormat);bitmap=&v.bmiHeader;duration=v.AvgTimePerFrame;
    }
    if(!bitmap)return {};
    wchar_t subtype[40]{},formatType[40]{};StringFromGUID2(type.subtype,subtype,40);StringFromGUID2(type.formattype,formatType,40);
    auto key=std::format(L"{}:{}:{}:{}:{}:{}:{}",bitmap->biWidth,bitmap->biHeight,duration,subtype,formatType,bitmap->biBitCount,bitmap->biCompression);
    // 1080p30 and 1080i60 can otherwise have identical identities. Retain all
    // interlace flags, including field order, rather than silently selecting
    // the first same-size/rate entry. Legacy keys migrate only if unambiguous.
    if(type.formattype==FORMAT_VideoInfo2)key+=std::format(L":scan={}",reinterpret_cast<const VIDEOINFOHEADER2*>(type.pbFormat)->dwInterlaceFlags);
    return key;
}
// RGB DIB packings are the only ones whose biHeight sign describes storage
// order; YUV formats are always top-down (see captureMediaLayout).
constexpr bool captureIsRgbDib(CapturePacking packing){
    return packing==CapturePacking::Bgr32||packing==CapturePacking::Bgra32||packing==CapturePacking::Bgr24||
        packing==CapturePacking::Rgb555||packing==CapturePacking::Rgb565;
}
// Parse negotiated memory layout, not the UI's requested dimensions. YUV is
// top-down for either sign of biHeight; only RGB DIBs use bottom-up storage.
inline bool captureMediaLayout(const AM_MEDIA_TYPE& type,CaptureMediaLayout& out){
    out={};if(type.majortype!=MEDIATYPE_Video||!type.pbFormat)return false;
    BITMAPINFOHEADER bm{};DWORD flags=0;
    if(type.formattype==FORMAT_VideoInfo&&type.cbFormat>=sizeof(VIDEOINFOHEADER)){
        const auto& v=*reinterpret_cast<const VIDEOINFOHEADER*>(type.pbFormat);bm=v.bmiHeader;out.duration=v.AvgTimePerFrame;
    }else if(type.formattype==FORMAT_VideoInfo2&&type.cbFormat>=sizeof(VIDEOINFOHEADER2)){
        const auto& v=*reinterpret_cast<const VIDEOINFOHEADER2*>(type.pbFormat);bm=v.bmiHeader;out.duration=v.AvgTimePerFrame;flags=v.dwControlFlags;
    }else return false;
    const auto height=std::abs(int64_t(bm.biHeight));
    if(bm.biWidth<=0||bm.biWidth>3840||height<1||height>2160)return false;
    out.width=unsigned(bm.biWidth);out.height=unsigned(height);
    out.packing=capturePacking(type.subtype);
    switch(out.packing){
    case CapturePacking::Yuy2:case CapturePacking::Uyvy:case CapturePacking::Yvyu:
        if(out.width%2)return false;
        // N1: keep the driver's true packing in the frame contract and unpack
        // on the GPU; only the legacy CPU-unpack diagnostic maps to YUY2.
        out.format=out.packing==CapturePacking::Uyvy?AV_PIX_FMT_UYVY422:out.packing==CapturePacking::Yvyu?AV_PIX_FMT_YVYU422:AV_PIX_FMT_YUYV422;
        out.rowBytes=out.width*2;break;
    case CapturePacking::Nv12:case CapturePacking::Nv21:case CapturePacking::I420:case CapturePacking::Yv12:
    case CapturePacking::P010:case CapturePacking::P016:
        if(out.width%2||out.height%2)return false;
        out.planes=(out.packing==CapturePacking::I420||out.packing==CapturePacking::Yv12)?3:2;
        out.format=out.planes==3?AV_PIX_FMT_YUV420P:out.packing==CapturePacking::P010?AV_PIX_FMT_P010:out.packing==CapturePacking::P016?AV_PIX_FMT_P016:AV_PIX_FMT_NV12;
        out.rowBytes=out.width*((out.format==AV_PIX_FMT_P010||out.format==AV_PIX_FMT_P016)?2:1);break;
    case CapturePacking::Bgr32:case CapturePacking::Bgra32:case CapturePacking::Bgr24:case CapturePacking::Rgb555:case CapturePacking::Rgb565:
        out.format=out.packing==CapturePacking::Bgr24?AV_PIX_FMT_BGR24:out.packing==CapturePacking::Rgb555?AV_PIX_FMT_RGB555LE:out.packing==CapturePacking::Rgb565?AV_PIX_FMT_RGB565LE:AV_PIX_FMT_BGR0;
        out.bottomUp=bm.biHeight>0;
        out.rowBytes=out.width*(out.packing==CapturePacking::Bgr24?3:(out.packing==CapturePacking::Rgb555||out.packing==CapturePacking::Rgb565)?2:4);break;
    default:return false;
    }
    const unsigned rows=out.planes>1?out.height*3/2:out.height;
    const bool rgbDib=out.format==AV_PIX_FMT_BGR0||out.format==AV_PIX_FMT_BGR24||out.format==AV_PIX_FMT_RGB555LE||out.format==AV_PIX_FMT_RGB565LE;
    out.stride=out.rowBytes;
    if(rgbDib)out.stride=(out.stride+3)&~3u;
    if(bm.biSizeImage){
        // Fixed uncompressed allocation may include per-row padding. Reject
        // ambiguous/incomplete layouts instead of reading subsequent rows wrong.
        if(bm.biSizeImage%rows||bm.biSizeImage/rows<out.stride)return false;
        out.stride=bm.biSizeImage/rows;
    }
    if(rgbDib&&out.stride%4)return false;
    if((out.planes==3||out.format==AV_PIX_FMT_P010||out.format==AV_PIX_FMT_P016)&&out.stride%2)return false;
    out.sampleBytes=size_t(out.stride)*rows;
    if(out.planes>1){
        out.chromaStride=out.planes==3?out.stride/2:out.stride;
        out.chromaRowBytes=out.planes==3?out.width/2:out.rowBytes;
        out.chromaOffset=size_t(out.stride)*out.height;
        out.secondChromaOffset=out.chromaOffset+size_t(out.chromaStride)*(out.height/2);
        if(out.packing==CapturePacking::Yv12)std::swap(out.chromaOffset,out.secondChromaOffset);
    }
    if(out.sampleBytes>size_t(std::numeric_limits<LONG>::max()))return false;
    AVFrame frame{};frame.format=out.format;frame.width=int(out.width);frame.height=int(out.height);
    out.color=pipeline::resolveFrameColor(frame);
    // HDYC is UYVY with BT.709, even at SD sizes. Explicit VideoInfo2 tags
    // below, and the user's color override, retain precedence.
    if(captureIsFourcc(type.subtype,captureFourcc('H','D','Y','C'))){
        out.color.matrix=pipeline::YuvMatrix::BT709;out.color.matrixAssumed=false;
        out.color.primaries=pipeline::ColorPrimaries::BT709;out.color.primariesAssumed=false;
    }
    // SDR capture is already display-referred R'G'B' after the YUV matrix.
    // Preserve those code values through the sRGB working round trip, like
    // the old RGB ingress, instead of applying a camera OETF a second time.
    out.color.transfer=pipeline::TransferFunction::SRGB;out.color.transferAssumed=true;
    out.color.preserveSdrCodeValues=true;
    out.colorControlFlags=flags;
    if(flags&AMCONTROL_COLORINFO_PRESENT){
        DXVA2_ExtendedFormat ext{};ext.value=flags;
        if(ext.NominalRange==DXVA2_NominalRange_0_255){out.color.range=pipeline::ColorRange::Full;out.color.rangeAssumed=false;}
        else if(ext.NominalRange==DXVA2_NominalRange_16_235){out.color.range=pipeline::ColorRange::Limited;out.color.rangeAssumed=false;}
        else if(ext.NominalRange!=DXVA2_NominalRange_Unknown)return false;
        if(ext.VideoTransferMatrix==DXVA2_VideoTransferMatrix_BT601){out.color.matrix=pipeline::YuvMatrix::BT601;out.color.matrixAssumed=false;}
        else if(ext.VideoTransferMatrix==DXVA2_VideoTransferMatrix_BT709){out.color.matrix=pipeline::YuvMatrix::BT709;out.color.matrixAssumed=false;}
        // Extended Windows MF/DXVA color codes: BT2020_10/12=4/5,
        // BT2020 primaries=9, ST2084=15, HLG=16 (Windows SDK mfobjects.h).
        else if(ext.VideoTransferMatrix==4||ext.VideoTransferMatrix==5){out.color.matrix=pipeline::YuvMatrix::BT2020NCL;out.color.matrixAssumed=false;}
        else if(ext.VideoTransferMatrix!=DXVA2_VideoTransferMatrix_Unknown)return false;
        if(ext.VideoPrimaries==9){out.color.primaries=pipeline::ColorPrimaries::BT2020;out.color.primariesAssumed=false;}
        else if(ext.VideoPrimaries==DXVA2_VideoPrimaries_BT709){out.color.primaries=pipeline::ColorPrimaries::BT709;out.color.primariesAssumed=false;}
        else if(ext.VideoPrimaries==DXVA2_VideoPrimaries_BT470_2_SysBG){out.color.primaries=pipeline::ColorPrimaries::BT601_625;out.color.primariesAssumed=false;}
        else if(ext.VideoPrimaries==DXVA2_VideoPrimaries_SMPTE170M){out.color.primaries=pipeline::ColorPrimaries::BT601_525;out.color.primariesAssumed=false;}
        if(ext.VideoTransferFunction==DXVA2_VideoTransFunc_10){out.color.transfer=pipeline::TransferFunction::Linear;out.color.transferAssumed=false;}
        else if(ext.VideoTransferFunction==DXVA2_VideoTransFunc_sRGB){out.color.transferAssumed=false;}
        else if(ext.VideoTransferFunction==DXVA2_VideoTransFunc_709){out.color.transfer=pipeline::TransferFunction::BT709;out.color.transferAssumed=false;}
        else if(ext.VideoTransferFunction==15||ext.VideoTransferFunction==16){out.color.transfer=ext.VideoTransferFunction==15?pipeline::TransferFunction::PQ:pipeline::TransferFunction::HLG;out.color.transferAssumed=false;}
        else if(ext.VideoTransferFunction!=DXVA2_VideoTransFunc_Unknown&&ext.VideoTransferFunction!=DXVA2_VideoTransFunc_709&&ext.VideoTransferFunction!=DXVA2_VideoTransFunc_22)return false;
        // BT.2100 fallback needs an explicit HDR transfer, never just P010.
        // Keep missing fields marked assumed and preserve conflicting tags.
        if(out.color.isHdrPath()){
            if(ext.VideoTransferMatrix==DXVA2_VideoTransferMatrix_Unknown)out.color.matrix=pipeline::YuvMatrix::BT2020NCL;
            if(ext.VideoPrimaries==DXVA2_VideoPrimaries_Unknown)out.color.primaries=pipeline::ColorPrimaries::BT2020;
        }
        if(out.planes>1){
            const unsigned chroma=ext.VideoChromaSubsampling&~DXVA2_VideoChromaSubsampling_ProgressiveChroma;
            if(chroma==DXVA2_VideoChromaSubsampling_MPEG2)out.color.chromaLocation=pipeline::ChromaLocation::Left;
            else if(chroma==DXVA2_VideoChromaSubsampling_MPEG1)out.color.chromaLocation=pipeline::ChromaLocation::Center;
            else if(chroma==DXVA2_VideoChromaSubsampling_Cosited)out.color.chromaLocation=pipeline::ChromaLocation::TopLeft;
        }
    }
    return true;
}
// N1 rollback switch (diagnostic): map a packed capture layout back to the
// legacy BGR0 / YUY2 target the old per-pixel CPU conversion produced. The
// device still delivers the packed bytes; copyCaptureSample then converts on
// the callback thread exactly as it did before N1.
inline bool captureLegacyCpuLayout(CaptureMediaLayout& layout){
    switch(layout.packing){
    case CapturePacking::Uyvy:case CapturePacking::Yvyu:layout.format=AV_PIX_FMT_YUYV422;return true;
    case CapturePacking::Bgr24:case CapturePacking::Rgb555:case CapturePacking::Rgb565:layout.format=AV_PIX_FMT_BGR0;return true;
    default:return false;
    }
}
// flipVertical is the manual capture override: it inverts whatever the DIB
// header claims (and reverses chroma rows for planar YUV), so a device whose
// declared orientation does not match its samples can still be watched.
inline bool copyCaptureSample(const CaptureMediaLayout& layout,const uint8_t* src,size_t bytes,AVFrame& dst,bool flipVertical=false){
    if(!src||bytes<layout.sampleBytes||dst.format!=layout.format||dst.width!=int(layout.width)||dst.height!=int(layout.height)||!dst.data[0]||dst.linesize[0]<int(layout.rowBytes))return false;
    if(layout.format==AV_PIX_FMT_BGR0&&dst.linesize[0]<int(layout.width*4))return false;
    if(layout.planes>1&&(!dst.data[1]||dst.linesize[1]<int(layout.chromaRowBytes)))return false;
    if(layout.planes>2&&(!dst.data[2]||dst.linesize[2]<int(layout.chromaRowBytes)))return false;
    const bool flip=layout.bottomUp!=flipVertical;
    // Fast path: no row reorder and no per-pixel conversion, so the whole
    // plane is one contiguous copy (single-plane layouts only).
    const bool rowConvert=(layout.format==AV_PIX_FMT_BGR0&&(layout.packing==CapturePacking::Bgr24||layout.packing==CapturePacking::Rgb555||layout.packing==CapturePacking::Rgb565))||
        (layout.format==AV_PIX_FMT_YUYV422&&(layout.packing==CapturePacking::Uyvy||layout.packing==CapturePacking::Yvyu));
    if(!flip&&!rowConvert&&layout.planes==1&&size_t(dst.linesize[0])==size_t(layout.stride)){
        std::memcpy(dst.data[0],src,size_t(layout.stride)*layout.height);
        return true;
    }
    // When rows are flipped, read the source ascending and write the
    // destination descending: the read stream keeps the hardware prefetcher
    // (walking the source backwards cost ~2x on the 4K RGB24 case).
    for(unsigned sourceRow=0;sourceRow<layout.height;++sourceRow){
        const unsigned dstRow=flip?layout.height-1-sourceRow:sourceRow;
        const auto* s=src+size_t(sourceRow)*layout.stride;auto* d=dst.data[0]+ptrdiff_t(dstRow)*dst.linesize[0];
        if(layout.format==AV_PIX_FMT_BGR0&&layout.packing==CapturePacking::Bgr24){for(unsigned x=0;x<layout.width;++x){d[x*4]=s[x*3];d[x*4+1]=s[x*3+1];d[x*4+2]=s[x*3+2];d[x*4+3]=255;}}
        else if(layout.format==AV_PIX_FMT_BGR0&&(layout.packing==CapturePacking::Rgb555||layout.packing==CapturePacking::Rgb565)){
            const bool six=layout.packing==CapturePacking::Rgb565;
            for(unsigned x=0;x<layout.width;++x){const unsigned v=s[x*2]|(unsigned(s[x*2+1])<<8);const unsigned b=v&31,g=(v>>5)&(six?63:31),r=(v>>(six?11:10))&31;d[x*4]=uint8_t((b<<3)|(b>>2));d[x*4+1]=uint8_t(six?(g<<2)|(g>>4):(g<<3)|(g>>2));d[x*4+2]=uint8_t((r<<3)|(r>>2));d[x*4+3]=255;}
        }else if(layout.format==AV_PIX_FMT_YUYV422&&(layout.packing==CapturePacking::Uyvy||layout.packing==CapturePacking::Yvyu)){
            const bool uy=layout.packing==CapturePacking::Uyvy;for(unsigned x=0;x<layout.rowBytes;x+=4){d[x]=s[x+(uy?1:0)];d[x+1]=s[x+(uy?0:3)];d[x+2]=s[x+(uy?3:2)];d[x+3]=s[x+(uy?2:1)];}
        }else std::memcpy(d,s,layout.rowBytes);
    }
    for(unsigned p=1;p<layout.planes;++p)for(unsigned y=0;y<layout.height/2;++y){
        const auto* s=src+(p==1?layout.chromaOffset:layout.secondChromaOffset)+size_t(flip?layout.height/2-1-y:y)*layout.chromaStride;auto* d=dst.data[p]+ptrdiff_t(y)*dst.linesize[p];
        if(layout.packing==CapturePacking::Nv21){for(unsigned x=0;x<layout.chromaRowBytes;x+=2){d[x]=s[x+1];d[x+1]=s[x];}}
        else std::memcpy(d,s,layout.chromaRowBytes);
    }
    return true;
}
inline bool equivalentCaptureTypes(const AM_MEDIA_TYPE& a,const AM_MEDIA_TYPE& b){
    CaptureMediaLayout x,y;if(!captureMediaLayout(a,x)||!captureMediaLayout(b,y))return false;
    return x.packing==y.packing&&x.format==y.format&&x.width==y.width&&x.height==y.height&&x.stride==y.stride&&x.bottomUp==y.bottomUp&&x.duration==y.duration&&
        x.color.range==y.color.range&&x.color.matrix==y.color.matrix&&x.color.transfer==y.color.transfer&&
        x.color.primaries==y.color.primaries&&x.color.displayReferred709==y.color.displayReferred709&&x.color.preserveSdrCodeValues==y.color.preserveSdrCodeValues&&
        x.color.rangeAssumed==y.color.rangeAssumed&&x.color.matrixAssumed==y.color.matrixAssumed&&x.color.transferAssumed==y.color.transferAssumed&&
        x.color.primariesAssumed==y.color.primariesAssumed&&x.color.chromaLocation==y.color.chromaLocation;
}
}
