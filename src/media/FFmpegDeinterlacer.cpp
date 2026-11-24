#include "veyra/media/FFmpegDeinterlacer.h"
#include "veyra/media/FFmpegVideoDecoder.h"
#include "veyra/Log.h"
#include <windows.h>
#include <filesystem>
#include <format>
#include <algorithm>
#include <deque>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/error.h>
}
#if defined(VEYRA_ENABLE_DEINTERLACE)
#define VEYRA_HAS_AVFILTER_HEADERS 1
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d12va.h>
}
#endif

namespace veyra::media {
#ifdef VEYRA_HAS_AVFILTER_HEADERS
namespace {
struct FilterApi {
#define FILTER_API(name) decltype(&::name) name=nullptr
    FILTER_API(avfilter_graph_alloc);FILTER_API(avfilter_graph_free);
    FILTER_API(avfilter_get_by_name);FILTER_API(avfilter_graph_create_filter);
    FILTER_API(avfilter_graph_alloc_filter);FILTER_API(avfilter_init_str);
    FILTER_API(avfilter_link);FILTER_API(avfilter_graph_config);
    FILTER_API(av_buffersrc_add_frame_flags);FILTER_API(av_buffersink_get_frame);
    FILTER_API(av_buffersink_get_time_base);FILTER_API(av_buffersrc_parameters_alloc);
    FILTER_API(av_buffersrc_parameters_set);
#undef FILTER_API
    bool ready=false;
    FilterApi(){
        wchar_t executable[32768]{};
        if(!GetModuleFileNameW(nullptr,executable,32768))return;
        const auto path=std::filesystem::path(executable).parent_path()/L"avfilter-12.dll";
        auto module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!module){log::warn("deinterlace",std::format("optional avfilter module unavailable Win32={}",GetLastError()));return;}
#define FILTER_API(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(module,#name));if(!name)return
        FILTER_API(avfilter_graph_alloc);FILTER_API(avfilter_graph_free);
        FILTER_API(avfilter_get_by_name);FILTER_API(avfilter_graph_create_filter);
        FILTER_API(avfilter_graph_alloc_filter);FILTER_API(avfilter_init_str);
        FILTER_API(avfilter_link);FILTER_API(avfilter_graph_config);
        FILTER_API(av_buffersrc_add_frame_flags);FILTER_API(av_buffersink_get_frame);
        FILTER_API(av_buffersink_get_time_base);FILTER_API(av_buffersrc_parameters_alloc);
        FILTER_API(av_buffersrc_parameters_set);
#undef FILTER_API
        ready=true; // Module intentionally retained while filters can exist.
    }
};
FilterApi& api(){static FilterApi value;return value;}
}
#endif
struct FFmpegDeinterlacer::Impl {
    std::string path;
    int num=0,den=0;
#ifdef VEYRA_HAS_AVFILTER_HEADERS
    AVFilterGraph* graph=nullptr;AVFilterContext* source=nullptr;AVFilterContext* sink=nullptr;
    AVFrame* output=nullptr;AVBufferRef* bridgeFrames=nullptr;
    struct Clock {int64_t pts,duration;unsigned field=0,fields=1;};
    std::deque<Clock> clocks;
    AVRational inputTimeBase{},inputRate{};
    int64_t previousPicturePts=AV_NOPTS_VALUE;
    int64_t observedPictureTicks=0;
    AVFrame* input(const AVFrame* frame,const HardwareSurfaceView* surface){
        if(!frame)return nullptr;
        auto* copy=av_frame_clone(frame);
        if(copy&&copy->pts==AV_NOPTS_VALUE)copy->pts=copy->best_effort_timestamp!=AV_NOPTS_VALUE?copy->best_effort_timestamp:copy->pkt_dts;
        if(!copy||frame->format!=AV_PIX_FMT_D3D11)return copy;
        if(!surface||!surface->texture||!surface->waitFence){av_frame_free(&copy);return nullptr;}
        if(!bridgeFrames){
            auto* device=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D12VA);
            if(!device){av_frame_free(&copy);return nullptr;}
            auto* context=reinterpret_cast<AVHWDeviceContext*>(device->data);
            auto* hardware=reinterpret_cast<AVD3D12VADeviceContext*>(context->hwctx);
            if(FAILED(surface->texture->GetDevice(IID_PPV_ARGS(&hardware->device)))||av_hwdevice_ctx_init(device)<0){
                av_buffer_unref(&device);av_frame_free(&copy);return nullptr;
            }
            bridgeFrames=av_hwframe_ctx_alloc(device);av_buffer_unref(&device);
            if(!bridgeFrames){av_frame_free(&copy);return nullptr;}
            auto* frames=reinterpret_cast<AVHWFramesContext*>(bridgeFrames->data);
            frames->format=AV_PIX_FMT_D3D12;
            frames->sw_format=surface->texture->GetDesc().Format==DXGI_FORMAT_P010?AV_PIX_FMT_P010:AV_PIX_FMT_NV12;
            frames->width=surface->textureWidth;frames->height=surface->textureHeight;
            if(av_hwframe_ctx_init(bridgeFrames)<0){av_buffer_unref(&bridgeFrames);av_frame_free(&copy);return nullptr;}
        }
        auto* descriptor=static_cast<AVD3D12VAFrame*>(av_mallocz(sizeof(AVD3D12VAFrame)));
        if(!descriptor){av_frame_free(&copy);return nullptr;}
        descriptor->texture=surface->texture;descriptor->texture->AddRef();
        descriptor->subresource_index=int(surface->subresourceIndex);
        descriptor->sync_ctx.fence=surface->waitFence;surface->waitFence->AddRef();
        descriptor->sync_ctx.fence_value=surface->waitValue;
        auto* reference=av_buffer_create(reinterpret_cast<uint8_t*>(descriptor),sizeof(*descriptor),
            [](void*,uint8_t* data){auto* d=reinterpret_cast<AVD3D12VAFrame*>(data);d->texture->Release();d->sync_ctx.fence->Release();av_free(d);},nullptr,0);
        if(!reference){descriptor->texture->Release();descriptor->sync_ctx.fence->Release();av_free(descriptor);av_frame_free(&copy);return nullptr;}
        // Keep the original D3D11 pool reference in buf[0]. This extra buffer
        // owns only the imported descriptor, texture and interop fence.
        int slot=0;while(slot<AV_NUM_DATA_POINTERS&&copy->buf[slot])++slot;
        if(slot==AV_NUM_DATA_POINTERS){av_buffer_unref(&reference);av_frame_free(&copy);return nullptr;}
        copy->buf[slot]=reference;copy->data[0]=reference->data;copy->data[1]=nullptr;copy->format=AV_PIX_FMT_D3D12;
        av_buffer_unref(&copy->hw_frames_ctx);copy->hw_frames_ctx=av_buffer_ref(bridgeFrames);
        return copy;
    }
#endif
};
bool FFmpegDeinterlacer::available(){
#ifdef VEYRA_HAS_AVFILTER_HEADERS
    auto& filters=api();return filters.ready&&filters.avfilter_get_by_name("deinterlace_d3d12")&&filters.avfilter_get_by_name("bwdif");
#else
    return false;
#endif
}
FFmpegDeinterlacer::FFmpegDeinterlacer():impl_(std::make_unique<Impl>()){}
FFmpegDeinterlacer::~FFmpegDeinterlacer(){close();}
bool FFmpegDeinterlacer::open(const AVFrame* frame,const HardwareSurfaceView* surface,int num,int den,int fpsNum,int fpsDen){
    close();
#ifdef VEYRA_HAS_AVFILTER_HEADERS
    auto& a=api();auto& i=*impl_;
    if(!a.ready||!frame||num<=0||den<=0)return false;
    auto* input=i.input(frame,surface);if(!input)return false;
    i.graph=a.avfilter_graph_alloc();
    if(!i.graph){av_frame_free(&input);return false;}
    const auto sar=input->sample_aspect_ratio.num>0&&input->sample_aspect_ratio.den>0?input->sample_aspect_ratio:AVRational{1,1};
    i.source=a.avfilter_graph_alloc_filter(i.graph,a.avfilter_get_by_name("buffer"),"in");
    int result=i.source?0:AVERROR(ENOMEM);
    if(result>=0){
        auto* parameters=a.av_buffersrc_parameters_alloc();
        if(!parameters)result=AVERROR(ENOMEM);
        else{
            parameters->format=input->format;parameters->width=input->width;parameters->height=input->height;
            parameters->time_base={num,den};parameters->sample_aspect_ratio=sar;parameters->frame_rate={std::max(1,fpsNum),std::max(1,fpsDen)};
            if(input->hw_frames_ctx)parameters->hw_frames_ctx=av_buffer_ref(input->hw_frames_ctx);
            parameters->color_space=input->colorspace;parameters->color_range=input->color_range;
            result=a.av_buffersrc_parameters_set(i.source,parameters);av_buffer_unref(&parameters->hw_frames_ctx);av_free(parameters);
        }
    }
    if(result>=0)result=a.avfilter_init_str(i.source,nullptr);
    const bool hardware=input->format==AV_PIX_FMT_D3D12;
    const char* name=hardware?"deinterlace_d3d12":"bwdif";
    const char* options=hardware?"method=default:mode=field:deint=interlaced":"mode=send_field:parity=auto:deint=interlaced";
    AVFilterContext* filter=nullptr;
    if(result>=0)result=a.avfilter_graph_create_filter(&filter,a.avfilter_get_by_name(name),"deinterlace",options,nullptr,i.graph);
    if(result>=0)result=a.avfilter_graph_create_filter(&i.sink,a.avfilter_get_by_name("buffersink"),"out",nullptr,nullptr,i.graph);
    if(result>=0)result=a.avfilter_link(i.source,0,filter,0);
    if(result>=0)result=a.avfilter_link(filter,0,i.sink,0);
    if(result>=0)result=a.avfilter_graph_config(i.graph,nullptr);
    av_frame_free(&input);
    if(result<0){log::error("deinterlace",std::format("filter configuration failed method={} code={}",name,result));close();return false;}
    i.output=av_frame_alloc();if(!i.output){close();return false;}
    const auto tb=a.av_buffersink_get_time_base(i.sink);i.num=tb.num;i.den=tb.den;i.path=hardware?"d3d12-video-processor":"bwdif-software";
    i.inputTimeBase={num,den};i.inputRate={fpsNum,fpsDen};
    log::info("deinterlace",std::format("method={} fieldRate=true inputFps={}/{} outputTimeBase={}/{} gpuPixelReadbacks=0",i.path,fpsNum,fpsDen,i.num,i.den));
    return true;
#else
    return false;
#endif
}
int FFmpegDeinterlacer::push(const AVFrame* frame,const HardwareSurfaceView* surface){
#ifdef VEYRA_HAS_AVFILTER_HEADERS
    auto& i=*impl_;if(!i.source)return AVERROR(EINVAL);
    auto* input=i.input(frame,surface);if(frame&&!input)return AVERROR(ENOMEM);
    if(input&&i.clocks.size()>=32){av_frame_free(&input);return AVERROR(ENOBUFS);}
    Impl::Clock clock{};
    if(input){
        clock.pts=input->pts;clock.duration=input->duration;clock.fields=(input->flags&AV_FRAME_FLAG_INTERLACED)?2:1;
        if(clock.duration<=0&&i.inputRate.num>0&&i.inputRate.den>0)
            clock.duration=av_rescale_q(1,av_inv_q(i.inputRate),i.inputTimeBase);
    }
    const int result=api().av_buffersrc_add_frame_flags(i.source,input,AV_BUFFERSRC_FLAG_KEEP_REF);
    if(result>=0&&input)i.clocks.push_back(clock);
    av_frame_free(&input);return result;
#else
    return AVERROR(ENOSYS);
#endif
}
int FFmpegDeinterlacer::receive(const AVFrame** frame){
    *frame=nullptr;
#ifdef VEYRA_HAS_AVFILTER_HEADERS
    auto& i=*impl_;if(!i.output)return AVERROR(EINVAL);
    av_frame_unref(i.output);const int result=api().av_buffersink_get_frame(i.sink,i.output);
    if(result>=0){
        if(i.clocks.empty())return AVERROR_INVALIDDATA;
        auto& clock=i.clocks.front();const AVRational outputBase{i.num,i.den};
        if(clock.field==0&&clock.pts!=AV_NOPTS_VALUE){
            // A container can label each interlaced picture with one field's
            // duration. Adjacent decoded picture PTS determine the real spacing.
            const int64_t next=i.clocks.size()>1?i.clocks[1].pts:AV_NOPTS_VALUE;
            const auto delta=[&](int64_t a,int64_t b)->int64_t{
                if(a==AV_NOPTS_VALUE||b==AV_NOPTS_VALUE||b<=a)return 0;
                const long double ticks=static_cast<long double>(b)-a;
                const double seconds=double(ticks)*av_q2d(i.inputTimeBase);
                return seconds>=1.0/240.0&&seconds<=0.25?int64_t(ticks):0;
            };
            const auto following=delta(clock.pts,next);
            const auto preceding=delta(i.previousPicturePts,clock.pts);
            i.previousPicturePts=clock.pts;
            if(const auto spacing=following?following:preceding;spacing>0&&clock.fields==2){
                clock.duration=spacing;
                if(!i.observedPictureTicks)i.observedPictureTicks=spacing;
            }
        }
        if(clock.pts!=AV_NOPTS_VALUE){
            i.output->pts=av_rescale_q(clock.pts,i.inputTimeBase,outputBase);
            const int64_t duration=clock.duration>0?av_rescale_q(clock.duration,i.inputTimeBase,outputBase)/clock.fields:0;
            if(clock.field&&duration>0)i.output->pts+=duration*clock.field;
            i.output->duration=duration;
        }
        if(++clock.field==clock.fields)i.clocks.pop_front();
        *frame=i.output;
    }
    if(result==AVERROR_EOF&&!i.clocks.empty())return AVERROR_INVALIDDATA;
    return result;
#else
    return AVERROR(ENOSYS);
#endif
}
int FFmpegDeinterlacer::timeBaseNum()const{return impl_->num;}
int FFmpegDeinterlacer::timeBaseDen()const{return impl_->den;}
double FFmpegDeinterlacer::observedPictureRate()const{
#ifdef VEYRA_HAS_AVFILTER_HEADERS
    const auto& i=*impl_;
    if(i.observedPictureTicks>0)return 1.0/(double(i.observedPictureTicks)*av_q2d(i.inputTimeBase));
#endif
    return 0;
}
bool FFmpegDeinterlacer::active()const{
#ifdef VEYRA_HAS_AVFILTER_HEADERS
    return impl_->output!=nullptr;
#else
    return false;
#endif
}
const std::string& FFmpegDeinterlacer::path()const{return impl_->path;}
void FFmpegDeinterlacer::close(){
#ifdef VEYRA_HAS_AVFILTER_HEADERS
    auto& i=*impl_;av_frame_free(&i.output);
    if(i.graph)api().avfilter_graph_free(&i.graph);
    av_buffer_unref(&i.bridgeFrames);i.source=i.sink=nullptr;
    i.clocks.clear();i.inputTimeBase={};i.inputRate={};
    i.previousPicturePts=AV_NOPTS_VALUE;i.observedPictureTicks=0;
#endif
    impl_->path.clear();impl_->num=impl_->den=0;
}
}
