#include "veyra/engine/EngineController.h"
#include "veyra/engine/TiledImageProcessor.h"
#include "veyra/engine/VideoPresenter.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include <filesystem>
#include <format>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
namespace veyra::engine {
void EngineController::runLargeImage(HWND window,const sink::RgbaImage& original,PlayerOptions options,
    gfx::D3D12DeviceContext& ctx,gfx::CommandSlotRing& ring) {
    {std::lock_guard lock(mutex_);snapshot_.image=true;snapshot_.running=true;desired_.multiplier=1;snapshot_.desired=desired_;}
    // A bounded presentation texture is independent of the full-resolution
    // result retained for saving. Preview changes never rerun the NR tiles.
    RECT initialClient{};GetClientRect(window,&initialClient);
    const uint32_t width=uint32_t(std::clamp(initialClient.right,1L,2048L)),height=uint32_t(std::clamp(initialClient.bottom,1L,2048L));
    pipeline::EnhanceGraphDesc previewDesc;previewDesc.sourceWidth=previewDesc.workWidth=width;previewDesc.sourceHeight=previewDesc.workHeight=height;
    previewDesc.rgbInput=true;previewDesc.stillImage=true;previewDesc.noFeatures=true;previewDesc.enableNr=false;previewDesc.enableFg=false;
    pipeline::EnhanceGraph preview(ctx,ring);VideoPresenter presenter;
    struct Cleanup {
        gfx::CommandSlotRing& ring;VideoPresenter& presenter;pipeline::EnhanceGraph& graph;
        ~Cleanup(){ring.drainQueue();presenter.close();graph.shutdown();}
    } cleanup{ring,presenter,preview};
    bool captureCompatible=options.settings.captureCompatible;
    if(!preview.initialize(previewDesc)||!presenter.open(ctx,window,preview,captureCompatible)||!preview.createViews()){status(L"Large-image preview initialization failed",true);return;}
    const auto freeFrame=[](AVFrame* p){av_frame_free(&p);};std::unique_ptr<AVFrame,decltype(freeFrame)> frame(av_frame_alloc(),freeFrame);
    if(!frame){status(L"Out of memory for large-image preview",true);return;}
    frame->format=AV_PIX_FMT_RGBA;frame->width=width;frame->height=height;
    frame->color_range=AVCOL_RANGE_JPEG;frame->color_trc=AVCOL_TRC_IEC61966_2_1;frame->colorspace=AVCOL_SPC_RGB;
    if(av_frame_get_buffer(frame.get(),32)<0){status(L"Out of memory for large-image preview",true);return;}
    sink::RgbaImage enhanced;EnhancementSettings applied;applied.revision=0;
    pipeline::EnhanceGraph::FrameOutputs out;bool havePreview=false;
    int lastComparison=-1;float lastSplit=-1;uint64_t previewRevision=0;PreviewView lastView;RECT lastClient{};
    while(!stop_){
        EnhancementSettings requested;std::wstring save;
        {std::lock_guard lock(mutex_);requested=desired_;if(!enhanced.pixels.empty())save.swap(savePath_);}
        if(requested.captureCompatible!=captureCompatible){
            if(!ring.drainQueue()){status(L"Display-switch drain failed",true);break;}
            presenter.close();
            if(!presenter.open(ctx,window,preview,requested.captureCompatible)){
                presenter.close();
                if(!presenter.open(ctx,window,preview,captureCompatible)){status(L"Display-switch recovery failed",true);break;}
                std::lock_guard lock(mutex_);desired_.rejectVideoRequest(requested,applied);snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.backendWarning=L"Live-compatible mode could not be enabled; reverted";continue;
            }
            captureCompatible=requested.captureCompatible;
        }
        auto contentSettings=requested;contentSettings.captureCompatible=applied.captureCompatible;
        if(!enhanced.pixels.empty()&&contentSettings.sameVideoConfiguration(applied)){
            applied=requested;
            std::lock_guard lock(mutex_);snapshot_.applied=applied;snapshot_.applying=desired_!=applied;
        }
        if(!save.empty()){
            try {
            auto ext=std::filesystem::path(save).extension().wstring();for(auto& c:ext)c=towlower(c);
            if(!sink::saveImage(save,enhanced,ext==L".jpg"||ext==L".jpeg"))status(L"Saving image failed (target may already exist)",false);
            else {status(L"Image saved; the full result can still be previewed");veyra::log::info("image-save",std::format("tiled full-resolution saved extent={}x{} revision={}",enhanced.width,enhanced.height,applied.revision));}
            }catch(const std::exception& e){veyra::log::warn("image-save",std::format("save exception; retaining full result/session: {}",e.what()));status(L"Save exception; the enhanced result is kept and can be saved again",false);}
        }
        if(requested.revision==applied.revision&&requested!=applied){
            applied=requested;
            std::lock_guard lock(mutex_);snapshot_.applied=applied;snapshot_.applying=desired_!=applied;
        }
        if(requested.revision!=applied.revision){
            pipeline::EnhanceGraphDesc desc;desc.enableNr=requested.nr;desc.nrRuntime=requested.nrRuntime;desc.noFeatures=!requested.nr;
            desc.model=requested.model;desc.nrTemporal=requested.nrTemporal;desc.residual=requested.residual;desc.protection=requested.protection;desc.color=requested.color;desc.settingsRevision=requested.revision;
            desc.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
            sink::RgbaImage candidate;TiledImageProcessor::Stats stats;
            status(L"Enhancing large image in tiles, preserving the full output size...");
            bool processed=false;
            try{processed=TiledImageProcessor::process(ctx,ring,original,candidate,desc,stop_,stats,[this](uint32_t n,uint32_t total){status(std::format(L"Large-image tile enhancement {}/{} · independent context per tile",n,total));});}
            catch(const std::exception& e){veyra::log::warn("image-tiles",std::format("settings exception; retaining previous result: {}",e.what()));}
            if(!processed){
                if(stop_)break;
                if(enhanced.pixels.empty()){status(L"Large-image tile enhancement failed; see diagnostics",true);break;}
                std::lock_guard lock(mutex_);desired_.rejectVideoRequest(requested,applied);
                snapshot_.desired=desired_;snapshot_.rejectedRevision=requested.revision;snapshot_.applying=desired_!=applied;
                snapshot_.status=L"Applying large-image parameters failed; the previous result is kept";continue;
            }
            enhanced=std::move(candidate);applied=requested;lastComparison=-1;
            {std::lock_guard lock(mutex_);snapshot_.applied=applied;snapshot_.desired=desired_;snapshot_.applying=desired_!=applied;
                snapshot_.nrEvaluated=stats.nrEvaluations;snapshot_.frames=1;snapshot_.transport=TransportState::Playing;
                auto& plan=snapshot_.metrics.resolution;plan.source=plan.base=plan.output={original.width,original.height};plan.nr={std::min(1280u,original.width)+256,std::min(1280u,original.height)+256};plan.flow=plan.fg={};plan.srApplied=false;plan.settingsRevision=applied.revision;
                snapshot_.status=std::format(L"{}×{} · tiled NR (not a whole-image context) · save preserves full size",original.width,original.height);}
        }
        const int comparison=comparisonMode_.load();const float split=comparisonSplit_.load();
        const auto view=previewView();RECT client{};GetClientRect(window,&client);
        if(client.right<1||client.bottom<1){std::this_thread::sleep_for(std::chrono::milliseconds(16));continue;}
        if(comparison!=lastComparison||split!=lastSplit||view!=lastView||client.right!=lastClient.right||client.bottom!=lastClient.bottom){
            // Source and enhanced use exactly the same coordinates. The full
            // image buffers, rather than the preview, remain the save source.
            for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){
                const double fit=std::min(double(client.right)/original.width,double(client.bottom)/original.height)*view.zoom;
                const double u=((x+.5)/width-.5)*client.right/(original.width*fit)+view.centerX;
                const double v=((y+.5)/height-.5)*client.bottom/(original.height*fit)+view.centerY;
                auto* dst=frame->data[0]+size_t(y)*frame->linesize[0]+size_t(x)*4;
                if(u<0||u>=1||v<0||v>=1){dst[0]=dst[1]=dst[2]=0;dst[3]=255;continue;}
                const uint32_t sx=std::min(original.width-1,uint32_t(u*original.width)),sy=std::min(original.height-1,uint32_t(v*original.height));
                const bool reference=comparison==1||(comparison==2&&u<split);
                const auto& image=reference?original:enhanced;
                memcpy(dst,image.pixels.data()+(size_t(sy)*image.width+sx)*4,4);
            }
            out={};if(!preview.process(frame.get(),0,true,out,++previewRevision)){status(L"Large-image preview upload failed",true);break;}
            havePreview=true;lastComparison=comparison;lastSplit=split;lastView=view;lastClient=client;
        }
        if(havePreview&&!presenter.present(ctx,ring,preview,out.videoSlot,false,false,0,false,.5f,{},PreviewView{0,.5f,.5f})){status(L"Large-image presentation failed",true);break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    out={}; // Cleanup drains before either presenter or graph releases GPU objects.
}
}
