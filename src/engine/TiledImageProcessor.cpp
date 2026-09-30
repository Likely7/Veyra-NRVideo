#include "veyra/engine/TiledImageProcessor.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::engine {
bool TiledImageProcessor::process(gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring,
    const sink::RgbaImage& input,sink::RgbaImage& output,pipeline::EnhanceGraphDesc desc,
    const std::atomic<bool>& cancel,Stats& stats,const std::function<void(uint32_t,uint32_t)>& progress) {
    stats={};output={};
    const uint64_t pixels=uint64_t(input.width)*input.height;
    if(!input.width||!input.height||pixels>std::numeric_limits<size_t>::max()/4||input.pixels.size()!=pixels*4||cancel)return false;
    // This path preserves input resolution; SR is not an implicit tile resize.
    if(desc.enableSr){veyra::log::error("image-tiles","SR requires a separate full-image output plan");return false;}
    constexpr uint32_t coreLimit=1280,halo=128,overlap=64;
    // The NR exclusion feather ramps inside the padding; a halo smaller than
    // the accepted feather range (0-64 px) would clip the ramp at tile edges.
    static_assert(halo>=64,"tile halo must cover the maximum protection feather");
    const uint32_t coreW=std::min(coreLimit,input.width),coreH=std::min(coreLimit,input.height);
    const uint32_t tileW=coreW+2*halo,tileH=coreH+2*halo;
    const uint32_t countX=(input.width-1)/coreW+1,countY=(input.height-1)/coreH+1;
    if(uint64_t(countX)*countY>UINT32_MAX)return false;
    const uint32_t total=countX*countY;
    desc.sourceWidth=desc.workWidth=desc.nrWidth=tileW;
    desc.sourceHeight=desc.workHeight=desc.nrHeight=tileH;
    desc.rgbInput=true;desc.stillImage=true;desc.enableFg=false;desc.enableNvofStandalone=false;
    pipeline::EnhanceGraph graph(ctx,ring);
    if(!graph.initialize(desc)||!graph.createViews())return false;
    AVFrame* raw=av_frame_alloc();
    const auto freeFrame=[](AVFrame* p){av_frame_free(&p);};
    std::unique_ptr<AVFrame,decltype(freeFrame)> frame(raw,freeFrame);
    if(!frame)return false;
    frame->format=AV_PIX_FMT_RGBA;frame->width=tileW;frame->height=tileH;
    frame->color_range=AVCOL_RANGE_JPEG;frame->color_trc=AVCOL_TRC_IEC61966_2_1;frame->colorspace=AVCOL_SPC_RGB;
    if(av_frame_get_buffer(frame.get(),32)<0)return false;
    sink::RgbaImage result;result.width=input.width;result.height=input.height;
    std::vector<float> weights;
    try{result.pixels.resize(size_t(pixels)*4);weights.resize(size_t(pixels));}
    catch(const std::bad_alloc&){veyra::log::error("image-tiles","insufficient host memory for full-resolution image");return false;}
    for(uint32_t row=0;row<countY;++row)for(uint32_t col=0;col<countX;++col){
        if(cancel)return false;
        const int64_t originX=int64_t(col)*coreW,originY=int64_t(row)*coreH;
        for(uint32_t y=0;y<tileH;++y){
            const auto sy=uint32_t(std::clamp(originY+int64_t(y)-halo,int64_t(0),int64_t(input.height)-1));
            auto* dst=frame->data[0]+size_t(y)*frame->linesize[0];
            for(uint32_t x=0;x<tileW;++x){
                const auto sx=uint32_t(std::clamp(originX+int64_t(x)-halo,int64_t(0),int64_t(input.width)-1));
                memcpy(dst+size_t(x)*4,input.pixels.data()+(size_t(sy)*input.width+sx)*4,4);
            }
        }
        // Source-normalized regions are mapped to this padded tile. Halo exceeds
        // the maximum feather, so clipping at tile edges cannot alter core pixels.
        engine::EnhancementSettings tileSettings;tileSettings.nr=desc.enableNr;tileSettings.model=desc.model;tileSettings.residual=desc.residual;tileSettings.protection=desc.protection;
        // Protection is tile-local; grading remains the same ordered chain as
        // the full image. Default colour settings would reject an enabled grade
        // (or a nonempty additional chain) before the first tile is processed.
        tileSettings.color=desc.color;tileSettings.additionalColors=desc.additionalColors;
        tileSettings.additionalColorCount=desc.additionalColorCount;
        for(auto& q:tileSettings.protection.regions){
            // A rectangle clipped to the tile is the same rectangle; an ellipse
            // clipped would be a different ellipse, so it keeps its full box.
            const float low=q.ellipse?-64.0f:0.0f,high=q.ellipse?65.0f:1.0f;
            q.left=std::clamp(float(q.left*input.width-originX+halo)/tileW,low,high);
            q.right=std::clamp(float(q.right*input.width-originX+halo)/tileW,low,high);
            q.top=std::clamp(float(q.top*input.height-originY+halo)/tileH,low,high);
            q.bottom=std::clamp(float(q.bottom*input.height-originY+halo)/tileH,low,high);
        }
        tileSettings.revision=desc.settingsRevision;if(!graph.applySettings(tileSettings))return false;
        pipeline::EnhanceGraph::FrameOutputs out;sink::RgbaImage tile;
        // Every tile is a new spatial image, never a temporal neighbour.
        if(!graph.process(frame.get(),0,true,out,uint64_t(stats.tiles)+1,nullptr,nullptr,false)||
            !sink::readRgba8(ctx,ring,graph.videoFrameResource(out.videoSlot),tile))return false;
        if(tile.width!=tileW||tile.height!=tileH)return false;
        out={};
        const int64_t x0=std::max(int64_t(0),originX-overlap),y0=std::max(int64_t(0),originY-overlap);
        const int64_t x1=std::min(int64_t(input.width),originX+coreW+overlap),y1=std::min(int64_t(input.height),originY+coreH+overlap);
        for(int64_t y=y0;y<y1;++y)for(int64_t x=x0;x<x1;++x){
            const auto ramp=[](int64_t p,int64_t origin,uint32_t core){
                return std::min({1.0f,float(p-origin+overlap+1)/overlap,float(origin+core+overlap-p)/overlap});
            };
            const float weight=ramp(x,originX,coreW)*ramp(y,originY,coreH);
            const size_t index=size_t(y)*input.width+size_t(x);
            const size_t tileIndex=(size_t(y-originY+halo)*tileW+size_t(x-originX+halo))*4;
            const float sum=weights[index]+weight;
            for(unsigned c=0;c<4;++c)result.pixels[index*4+c]=uint8_t(std::clamp(std::lround((result.pixels[index*4+c]*weights[index]+tile.pixels[tileIndex+c]*weight)/sum),0l,255l));
            weights[index]=sum;
        }
        ++stats.tiles;stats.nrEvaluations=graph.metrics().nrEvaluateCount;
        if(progress)progress(stats.tiles,total);
    }
    if(cancel||!ring.drainQueue())return false;
    veyra::log::info("image-tiles",std::format("full={}x{} gpuTile={}x{} tiles={} NR={} contextHalo={} overlap={} resetEachTile=1",input.width,input.height,tileW,tileH,stats.tiles,stats.nrEvaluations,halo,overlap));
    output=std::move(result);return true;
}
}
