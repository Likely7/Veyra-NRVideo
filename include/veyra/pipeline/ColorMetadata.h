#pragma once
#include "veyra/pipeline/FramePacket.h"
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/mastering_display_metadata.h>
}
namespace veyra::pipeline {
inline ColorDescription resolveFrameColor(const AVFrame& frame,ColorDescription c={}){
    if(const auto* data=av_frame_get_side_data(&frame,AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);data&&data->size>=sizeof(AVMasteringDisplayMetadata)){
        const auto* m=reinterpret_cast<const AVMasteringDisplayMetadata*>(data->data);
        if(m->has_luminance&&m->max_luminance.den>0){const double n=av_q2d(m->max_luminance);if(n>0&&n<=10000)c.hdrMasteringPeakNits=float(n);}
    }
    if(const auto* data=av_frame_get_side_data(&frame,AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);data&&data->size>=sizeof(AVContentLightMetadata)){
        const auto* m=reinterpret_cast<const AVContentLightMetadata*>(data->data);
        if(m->MaxCLL>0&&m->MaxCLL<=10000&&m->MaxFALL<=m->MaxCLL){c.hdrMaxCllNits=float(m->MaxCLL);c.hdrMaxFallNits=float(m->MaxFALL);}
    }
    switch(frame.chroma_location){
    case AVCHROMA_LOC_LEFT:c.chromaLocation=ChromaLocation::Left;break;
    case AVCHROMA_LOC_CENTER:c.chromaLocation=ChromaLocation::Center;break;
    case AVCHROMA_LOC_TOPLEFT:c.chromaLocation=ChromaLocation::TopLeft;break;
    case AVCHROMA_LOC_TOP:c.chromaLocation=ChromaLocation::Top;break;
    case AVCHROMA_LOC_BOTTOMLEFT:c.chromaLocation=ChromaLocation::BottomLeft;break;
    case AVCHROMA_LOC_BOTTOM:c.chromaLocation=ChromaLocation::Bottom;break;
    default:break;
    }
    switch(frame.format){
    case AV_PIX_FMT_NV12:c.pixelFormat=SourcePixelFormat::NV12;break;
    case AV_PIX_FMT_P016:c.pixelFormat=SourcePixelFormat::P016;break;
    case AV_PIX_FMT_YUV420P10LE:case AV_PIX_FMT_P010:c.pixelFormat=SourcePixelFormat::P010;break;
    case AV_PIX_FMT_YUV420P:case AV_PIX_FMT_YUVJ420P:c.pixelFormat=SourcePixelFormat::Yuv420P;break;
    case AV_PIX_FMT_YUYV422:c.pixelFormat=SourcePixelFormat::Yuy2;break;
    case AV_PIX_FMT_BGRA:case AV_PIX_FMT_BGR0:
        if(c.pixelFormat!=SourcePixelFormat::V210&&c.pixelFormat!=SourcePixelFormat::Rgb10xLE)c.pixelFormat=SourcePixelFormat::Bgra8;
        break;
    case AV_PIX_FMT_BGR24:c.pixelFormat=SourcePixelFormat::Bgr24;break;
    case AV_PIX_FMT_RGB555LE:c.pixelFormat=SourcePixelFormat::Rgb555;break;
    case AV_PIX_FMT_RGB565LE:c.pixelFormat=SourcePixelFormat::Rgb565;break;
    case AV_PIX_FMT_UYVY422:c.pixelFormat=SourcePixelFormat::Uyvy;break;
    case AV_PIX_FMT_YVYU422:c.pixelFormat=SourcePixelFormat::Yvyu;break;
    default:break;
    }
    const auto* format=av_pix_fmt_desc_get(AVPixelFormat(frame.format));
    const bool rgb=format&&(format->flags&AV_PIX_FMT_FLAG_RGB)!=0;
    switch(frame.color_range){
    case AVCOL_RANGE_JPEG:c.range=ColorRange::Full;c.rangeAssumed=false;break;
    case AVCOL_RANGE_MPEG:c.range=ColorRange::Limited;c.rangeAssumed=false;break;
    default:break;
    }
    switch(frame.colorspace){
    case AVCOL_SPC_BT470BG:case AVCOL_SPC_SMPTE170M:c.matrix=YuvMatrix::BT601;c.matrixAssumed=false;break;
    case AVCOL_SPC_BT709:c.matrix=YuvMatrix::BT709;c.matrixAssumed=false;break;
    case AVCOL_SPC_BT2020_NCL:c.matrix=YuvMatrix::BT2020NCL;c.matrixAssumed=false;break;
    case AVCOL_SPC_BT2020_CL:c.matrix=YuvMatrix::BT2020CL;c.matrixAssumed=false;break;
    default:break;
    }
    switch(frame.color_primaries){
    case AVCOL_PRI_BT709:c.primaries=ColorPrimaries::BT709;c.primariesAssumed=false;break;
    case AVCOL_PRI_BT2020:c.primaries=ColorPrimaries::BT2020;c.primariesAssumed=false;break;
    default:break;
    }
    if(c.primaries==ColorPrimaries::Unknown||c.primariesAssumed){
        c.primaries=c.matrix==YuvMatrix::BT2020NCL||c.matrix==YuvMatrix::BT2020CL?ColorPrimaries::BT2020:ColorPrimaries::BT709;
        c.primariesAssumed=true;
    }
    switch(frame.color_trc){
    case AVCOL_TRC_SMPTE2084:
        // Profile 8.4 declares an HLG base layer even when its VUI was tagged PQ.
        // Keep the same transfer when a downstream GPU upload resolves this frame again.
        c.transfer=c.dolbyVisionP84?TransferFunction::HLG:TransferFunction::PQ;
        c.transferAssumed=false;
        break;
    case AVCOL_TRC_ARIB_STD_B67:c.transfer=TransferFunction::HLG;c.transferAssumed=false;break;
    case AVCOL_TRC_BT709:case AVCOL_TRC_SMPTE170M:c.transfer=TransferFunction::BT709;c.transferAssumed=false;break;
    case AVCOL_TRC_IEC61966_2_1:c.transfer=TransferFunction::SRGB;c.transferAssumed=false;break;
    case AVCOL_TRC_LINEAR:c.transfer=TransferFunction::Linear;c.transferAssumed=false;break;
    case AVCOL_TRC_BT2020_10:case AVCOL_TRC_BT2020_12:c.transfer=TransferFunction::BT2020_10;c.transferAssumed=false;break;
    default:break;
    }
    // A source's documented fallback is still a resolved contract. Preserve
    // it (and its provenance) unless the frame supplies explicit metadata.
    if(c.range==ColorRange::Unknown){c.range=(rgb||frame.format==AV_PIX_FMT_YUVJ420P||frame.format==AV_PIX_FMT_YUVJ422P||frame.format==AV_PIX_FMT_YUVJ444P)?ColorRange::Full:ColorRange::Limited;c.rangeAssumed=true;}
    if(c.matrix==YuvMatrix::Unknown){c.matrix=rgb||frame.height>=700?YuvMatrix::BT709:YuvMatrix::BT601;c.matrixAssumed=true;}
    if(c.transfer==TransferFunction::Unknown){c.transfer=rgb?TransferFunction::SRGB:TransferFunction::BT709;c.transferAssumed=true;}
    return c;
}
// BT.2020 10/12-bit SDR uses the BT.709-compatible OETF. Keep it distinct in
// metadata, but feed the same shader transfer branch rather than sRGB.
inline uint32_t transferCode(TransferFunction t){return t==TransferFunction::Linear?0u:(t==TransferFunction::BT709||t==TransferFunction::BT2020_10)?2u:1u;}
inline uint32_t workingTransferCode(const ColorDescription& c){
    // Our SDR sink encodes sRGB. Decode that same curve for ordinary desktop
    // BT.709 playback, preserving code values through the no-effects graph.
    // A camera inverse OETF lifts midtones; BT.1886 -> sRGB darkens them.
    if(c.preserveSdrCodeValues&&c.transfer==TransferFunction::BT709)return 1u;
    return c.displayReferred709&&c.transfer==TransferFunction::BT709?3u:transferCode(c.transfer);
}

}
