#include "veyra/engine/VideoExportJob.h"
#include "veyra/source/MediaFileSource.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/pipeline/ResolutionPlan.h"
#include "veyra/sink/VideoEncoder.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/RuntimePaths.h"
#include <filesystem>
#include <format>
#include <thread>
#include <chrono>
#include <cmath>
#include <deque>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
#include <libavutil/mastering_display_metadata.h>
}
namespace veyra::engine {
namespace { std::string utf8(const std::wstring& s){const int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),r.data(),n,nullptr,nullptr);return r;} }
bool exportVideo(const std::wstring& input,const std::wstring& output,PlayerOptions options,bool hevc,std::atomic<bool>& cancel,const std::function<void(double,const std::wstring&)>& progress,unsigned maxFrames,const std::function<bool()>& frameBoundary,const std::function<void(const ExportCounts&)>& counts){
    if(std::filesystem::exists(output)||std::filesystem::exists(output+L".partial")){progress(0,L"The target or partial file already exists; please use a different name");return false;}
    // XeSS-FG and AMD FSR-FG interpolate inside the present swapchain: the
    // provider presents the extra frames itself, so no output texture ever
    // reaches the application (XeSS-FG 3.0.2 exports only xefgSwapChain*, and
    // the FSR-FG context owns the proxy swapchain). Refusing the job left those
    // users with no file at all, so export now runs the in-graph DLSS path and
    // states the substitution. A rejected requested multiplier fails the job
    // explicitly; a successful export must retain the requested cadence.
    std::wstring fgNote;
    if(options.fg&&presentSinkFrameGeneration(options.settings.frameGenerationBackend)){
        const auto requested=options.settings.frameGenerationBackend;
        fgNote=std::format(L"{} frame generation is produced directly by the display swapchain, so the export cannot capture its frames; this export uses DLSS frame generation {}X instead",
            requested==FrameGenerationBackend::XeSS?L"XeSS":L"AMD FSR",options.fgMultiplier);
        veyra::log::warn("export",std::format("present-sink frame generation cannot feed the encoder requested={} multiplier={}; substituting the in-graph DLSS path",frameGenerationBackendName(requested),options.fgMultiplier));
        options.settings.frameGenerationBackend=FrameGenerationBackend::Dlss;
    }
    gfx::D3D12DeviceContext ctx;gfx::CommandSlotRing ring;source::MediaFileSource source;pipeline::EnhanceGraph graph(ctx,ring);
    std::unique_ptr<sink::VideoEncoder> encoder;
    AVFormatContext *mux=nullptr,*audioInput=nullptr;AVStream* videoStream=nullptr;AVStream* audioStream=nullptr;AVPacket* audioPacket=av_packet_alloc();
    int audioIndex=-1;bool audioPending=false,audioEof=false,ok=false,headerWritten=false;int64_t written=0;double audioEndSeconds=0,videoOriginSeconds=0;
    uint64_t repairedTimestamps=0;
    std::wstring encoderName;
    std::wstring failureReason;
    auto failAv=[&](const wchar_t* stage,int code){
        char error[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,error,sizeof(error));
        failureReason=std::format(L"{} failed (error {})",stage,code);
        veyra::log::error("export",std::format("stage={} code={} detail={}",utf8(stage),code,error));
        return false;
    };
    const auto partial=output+L".partial";
    try { do {
        Status st=Status::Ok;gfx::DeviceContextDesc dd;dd.commandSlotCount=6;
        if(!ctx.initialize(dd,st)||!ring.initialize(ctx.device(),ctx.directQueue(),ctx.fence(),ctx.fenceEvent(),6,st))break;
        // Export runs on every adapter now: NVIDIA uses NVENC (D3D12,
        // zero-copy) and everything else uses the driver's Media Foundation
        // hardware encoder. Features that need NGX/NVOF stay NVIDIA-only and
        // are gated below instead of failing the job.
        const bool nvidiaAdapter=ctx.adapter().isNvidia;
        if(!nvidiaAdapter)veyra::log::info("export",std::format("adapter={} vendor={}; DLSS NR/SR/FG and NVOF are unavailable, encoder falls back to the system Media Foundation hardware MFT",utf8(ctx.adapter().description),ctx.adapter().vendorIdHex));
        source::SourceOpenDesc od;od.path=input;od.preferHardwareDecode=false;if(!source.open(od)){failureReason=source.errorMessage();break;}
        auto info=source.info();if(!pipeline::Extent{info.width,info.height}.valid()){progress(0,L"Input size exceeds a single GPU texture's capacity");break;}
        // The first decoded frame is authoritative when container headers omit
        // transfer/range metadata. Retain it instead of scanning and reopening.
        pipeline::FramePacket firstPacket;const AVFrame* firstFrame=nullptr;
        if(source.read(firstPacket,&firstFrame)!=source::SourceReadStatus::Frame||!firstFrame){failureReason=L"Pre-reading the first frame for export failed";break;}
        info=source.info(); // retain this frame for the export, without decoding it again
        int rateNum=info.nominalRateNum,rateDen=info.nominalRateDen;
        if(rateNum<=0||rateDen<=0){rateNum=30;rateDen=1;veyra::log::warn("export-timeline","missing nominal rate; encoder configured at 30 fps, source timestamps retained");}
        const double sourceInterval=double(rateDen)/rateNum;
        const auto resolution=pipeline::ResolutionPlan::make({info.width,info.height},options.sr,pipeline::NrSizePolicy::Native,true,options.settings.revision,options.settings.srTarget);
        pipeline::EnhanceGraphDesc gd;gd.hdrInput=gd.hdrOutput=info.color.isHdrPath();
        gd.videoHdr=options.settings.videoHdr;
        gd.videoHdr.enabled=gd.videoHdr.enabled&&nvidiaAdapter;
        gd.hdrOutput=gd.hdrInput||gd.videoHdr.enabled;
        // Plan v5.3: the grade changes the peak and the content light distribution.
        // Recomputing MaxCLL/MaxFALL needs a full pre-pass before the header is
        // written, which this exporter does not do yet, so say so instead of
        // letting the user assume the static metadata tracks the graded output.
        if(gd.hdrInput&&gd.hdrOutput&&options.settings.color.enabled&&!options.settings.color.neutral())
            veyra::log::warn("color-export","HDR export with the colour grade active: MaxCLL/MaxFALL are carried over from the source and NOT recomputed for the graded output (marked as not updated)");
        gd.captureBitDepth=info.color.pixelFormat==pipeline::SourcePixelFormat::P010?10:info.color.pixelFormat==pipeline::SourcePixelFormat::P016?16:8;
        // Adapter gate mirrors the preview rules (EngineController): DLSS NR,
        // DLSS SR and the NVOF guidance need an NVIDIA device; AMD FSR
        // upscaling is the only vendor-neutral video SR we ship. Requesting an
        // unavailable feature must degrade the export, never fail it.
        const bool nvidiaFeatures=nvidiaAdapter;
        const bool srAvailable=resolution.srApplied&&(nvidiaFeatures||options.settings.videoSrQuality==kVideoSrFsr);
        if(options.nr&&!nvidiaFeatures)fgNote+=fgNote.empty()?L"This GPU cannot use DLSS NR; NR is turned off for this export":L"; This GPU cannot use DLSS NR; NR is turned off for this export";
        if(options.sr&&!srAvailable)fgNote+=fgNote.empty()?L"This GPU cannot use the selected super resolution; super resolution is turned off for this export":L"; This GPU cannot use the selected super resolution; super resolution is turned off for this export";
        if(!nvidiaFeatures&&srAvailable)fgNote+=fgNote.empty()?L"This export uses AMD FSR super resolution":L"; This export uses AMD FSR super resolution";
        gd.sourceWidth=info.width;gd.sourceHeight=info.height;gd.workWidth=resolution.base.width;gd.workHeight=resolution.base.height;gd.nrWidth=resolution.nr.width;gd.nrHeight=resolution.nr.height;gd.flowWidth=resolution.flow.width;gd.flowHeight=resolution.flow.height;gd.enableSr=srAvailable;gd.videoSrQuality=options.settings.videoSrQuality;gd.enableNr=options.nr&&nvidiaFeatures;gd.nrRuntime=options.settings.nrRuntime;gd.nrTemporal=options.settings.nrTemporal;gd.enableFg=options.fg&&nvidiaFeatures;gd.fgMultiplier=options.fgMultiplier;gd.frameGenerationBackend=options.settings.frameGenerationBackend;gd.enableNvofStandalone=options.nr&&nvidiaFeatures;gd.model=options.settings.model;gd.residual=options.settings.residual;gd.protection=options.settings.protection;gd.color=options.settings.color;gd.settingsRevision=options.settings.revision;gd.flowQuality=options.settings.flow;gd.opticalFlowBackend=options.settings.opticalFlowBackend;gd.amdFlowHalfResolution=options.settings.amdFlowHalfResolution;gd.contentRate=options.settings.content;gd.runtimeAbsPath=runtime::localRuntimeDirectory().wstring();
        // Keep the requested multiplier. Changing it after initialization fails
        // would produce a successful-looking file with different settings.
        bool graphReady=false,fgFailure=false;
        auto startGraph=[&](bool fg,uint32_t multiplier){
            gd.enableFg=fg;gd.fgMultiplier=fg?multiplier:1;
            if(graph.initialize(gd)&&graph.createViews()){graphReady=true;return;}
            if(graph.failedBackend()==FailedBackend::VideoHdr||(gd.convertVideoHdr()&&!gd.enableNr&&!gd.enableSr&&!gd.enableFg&&graph.failedBackend()==FailedBackend::NgxCore)){
                graph.shutdown();
                gd.videoHdr.enabled=false;gd.hdrOutput=gd.hdrInput;
                fgNote+=L"; RTX Video HDR initialization failed; keeping SDR this time (error codes in the log)";
                if(graph.initialize(gd)&&graph.createViews()){graphReady=true;return;}
            }
            fgFailure=graph.failedBackend()==FailedBackend::Fg;
            graph.shutdown();
        };
        if(options.fg&&!nvidiaFeatures){failureReason=L"This export device cannot perform the requested DLSS frame generation; the multiplier was not reduced";break;}
        if(options.fg){
            startGraph(true,options.fgMultiplier);
            if(!graphReady&&fgFailure){
                failureReason=std::format(L"DLSS {}X frame generation initialization failed; the multiplier was not reduced; see the task diagnostic log",options.fgMultiplier);
            }
        } else startGraph(false,1);
        if(!graphReady){if(failureReason.empty())failureReason=L"Enhancement pipeline initialization failed; see diagnostics";break;}
        if(gd.hdrOutput&&!hevc){hevc=true;fgNote+=fgNote.empty()?L"HDR video automatically uses HEVC Main10 encoding":L"; HDR video automatically uses HEVC Main10 encoding";}
        if(!fgNote.empty())progress(0,fgNote);
        const AVRational rate=av_mul_q({rateNum,rateDen},{int(options.fg?options.fgMultiplier:1),1});
        constexpr AVRational mediaTimeBase{1,1000000};
        veyra::log::info("export-timeline",std::format("source-timed export nominal={}/{} encoder={}/{} fg={} backend={} note={} (no CFR preflight or output decode verification)",rateNum,rateDen,rate.num,rate.den,options.fg?options.fgMultiplier:1,frameGenerationBackendName(options.settings.frameGenerationBackend),utf8(fgNote)));
        if(avformat_alloc_output_context2(&mux,nullptr,"mp4",utf8(partial).c_str())<0||!mux)break;
        videoStream=avformat_new_stream(mux,nullptr);if(!videoStream)break;videoStream->time_base=mediaTimeBase;videoStream->avg_frame_rate=rate;
        auto* cp=videoStream->codecpar;cp->codec_type=AVMEDIA_TYPE_VIDEO;cp->codec_id=hevc?AV_CODEC_ID_HEVC:AV_CODEC_ID_H264;cp->width=gd.workWidth;cp->height=gd.workHeight;cp->format=AV_PIX_FMT_YUV420P;cp->color_range=AVCOL_RANGE_MPEG;cp->color_space=AVCOL_SPC_BT709;cp->color_primaries=AVCOL_PRI_BT709;cp->color_trc=AVCOL_TRC_IEC61966_2_1;
        if(gd.hdrOutput){cp->format=AV_PIX_FMT_YUV420P10LE;cp->color_space=AVCOL_SPC_BT2020_NCL;cp->color_primaries=AVCOL_PRI_BT2020;cp->color_trc=AVCOL_TRC_SMPTE2084;cp->profile=AV_PROFILE_HEVC_MAIN_10;}
        // Plan v5.3: this exporter cannot recompute content light levels (that
        // needs a full pre-pass before the header is written), so the source
        // declaration is carried into the 'clli' box verbatim and the graded case
        // is flagged as "not updated" instead of silently changing meaning.
        if(gd.hdrInput&&gd.hdrOutput&&info.color.hdrMaxCllNits>0.0f){
            if(auto* side=av_packet_side_data_new(&cp->coded_side_data,&cp->nb_coded_side_data,AV_PKT_DATA_CONTENT_LIGHT_LEVEL,sizeof(AVContentLightMetadata),0)){
                auto* cll=reinterpret_cast<AVContentLightMetadata*>(side->data);
                cll->MaxCLL=unsigned(info.color.hdrMaxCllNits);
                cll->MaxFALL=unsigned(info.color.hdrMaxFallNits);
            }else failureReason=L"Could not write content light-level metadata (MaxCLL/MaxFALL)";
        }
        auto inputUtf8=utf8(input);
        if(avformat_open_input(&audioInput,inputUtf8.c_str(),nullptr,nullptr)<0||avformat_find_stream_info(audioInput,nullptr)<0){progress(0,L"Could not read source audio-track information; export stopped");break;}
        {
            audioIndex=av_find_best_stream(audioInput,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);
            if(options.audioStreamIndex>=0){
                if(unsigned(options.audioStreamIndex)>=audioInput->nb_streams||audioInput->streams[options.audioStreamIndex]->codecpar->codec_type!=AVMEDIA_TYPE_AUDIO){progress(0,L"The selected audio track no longer exists; export stopped");break;}
                audioIndex=options.audioStreamIndex;
            }
            log::info("export-audio",std::format("selected stream={}",audioIndex));
            if(audioIndex>=0){auto* acp=audioInput->streams[audioIndex]->codecpar;
                audioStream=avformat_new_stream(mux,nullptr);if(!audioStream||avcodec_parameters_copy(audioStream->codecpar,acp)<0)break;audioStream->codecpar->codec_tag=0;audioStream->time_base=audioInput->streams[audioIndex]->time_base;
                av_dict_copy(&audioStream->metadata,audioInput->streams[audioIndex]->metadata,0);
            }
            av_dict_copy(&mux->metadata,audioInput->metadata,0);
        }
        auto writeAudioUntil=[&](double seconds){if(!audioStream)return true;
            for(;;){if(!audioPending){if(audioEof)return true;av_packet_unref(audioPacket);const int readResult=av_read_frame(audioInput,audioPacket);if(readResult==AVERROR_EOF){audioEof=true;return true;}if(readResult<0)return failAv(L"Reading audio track",readResult);if(audioPacket->stream_index!=audioIndex)continue;audioPending=true;}
                const auto tb=audioInput->streams[audioIndex]->time_base;const int64_t ts=audioPacket->pts!=AV_NOPTS_VALUE?audioPacket->pts:audioPacket->dts;const double time=ts==AV_NOPTS_VALUE?0:ts*av_q2d(tb)-videoOriginSeconds;if(time>seconds)return true;
                const int64_t origin=av_rescale_q(static_cast<int64_t>(videoOriginSeconds*1000000),{1,1000000},tb);
                if(audioPacket->pts!=AV_NOPTS_VALUE)audioPacket->pts-=origin;if(audioPacket->dts!=AV_NOPTS_VALUE)audioPacket->dts-=origin;
                av_packet_rescale_ts(audioPacket,tb,audioStream->time_base);audioPacket->stream_index=audioStream->index;audioPacket->pos=-1;audioPending=false;const int rc=av_interleaved_write_frame(mux,audioPacket);if(rc<0)return failAv(L"Writing audio track",rc);
            }};
        // Encoders retain ordinal timestamps; only the bounded in-flight queue
        // maps them to media time. Hold one compressed packet for its duration.
        std::deque<std::pair<int64_t,int64_t>> timestamps;
        auto freePacket=[](AVPacket* p){av_packet_free(&p);};
        std::unique_ptr<AVPacket,decltype(freePacket)> pendingVideo(av_packet_alloc(),freePacket);
        if(!pendingVideo)break;
        auto flushVideo=[&](int64_t endUs){
            auto* pkt=pendingVideo.get();if(!pkt->size)return true;
            pkt->duration=std::max<int64_t>(1,endUs-pkt->pts);
            audioEndSeconds=(pkt->pts+pkt->duration)/1000000.0;
            av_packet_rescale_ts(pkt,mediaTimeBase,videoStream->time_base);
            const int rc=av_interleaved_write_frame(mux,pkt);av_packet_unref(pkt);
            if(rc<0)return failAv(L"Writing video frame",rc);
            ++written;return writeAudioUntil(audioEndSeconds);
        };
        auto writer=[&](const uint8_t* bytes,size_t size,int64_t pts,bool key){
            if(!headerWritten)return false;
            if(timestamps.empty()||timestamps.front().first!=pts){failureReason=L"The encoder returned an unknown frame index";return false;}
            const auto timeUs=timestamps.front().second;timestamps.pop_front();
            if(!flushVideo(timeUs))return false;
            auto* pkt=pendingVideo.get();if(av_new_packet(pkt,int(size))<0)return false;
            memcpy(pkt->data,bytes,size);pkt->stream_index=videoStream->index;pkt->pts=pkt->dts=timeUs;
            if(key)pkt->flags|=AV_PKT_FLAG_KEY;return true;
        };
        // Must drain before rate/writeAudioUntil/writer leave scope, including cancel/error.
        struct EncoderCloser {std::unique_ptr<sink::VideoEncoder>& encoder;~EncoderCloser(){encoder.reset();}} closer{encoder};
        sink::EncoderConfig encoderConfig;encoderConfig.hevc=hevc;encoderConfig.fpsNum=unsigned(rate.num);encoderConfig.fpsDen=unsigned(rate.den);encoderConfig.bitrateMbps=options.settings.exportBitrateMbps;
        std::wstring encoderDetail;
        encoder=sink::openVideoEncoder(ctx,ring,graph,encoderConfig,writer,encoderDetail);
        if(!encoder){
            progress(0,encoderDetail.empty()?L"No usable video encoder":encoderDetail);
            if(failureReason.empty())failureReason=encoderDetail.empty()?L"No usable video encoder":encoderDetail;
            veyra::log::error("export","no usable video encoder for this adapter/codec");
            break;
        }
        encoderName=encoder->describe();
        veyra::log::info("export",std::format("encoder={} codec={} bitrateMbps={} rate={}/{}",std::string(sink::encoderBackendName(encoder->backend())),hevc?"HEVC":"H264",encoderConfig.bitrateMbps,rate.num,rate.den));
        auto headers=encoder->headers();cp->extradata=static_cast<uint8_t*>(av_mallocz(headers.size()+AV_INPUT_BUFFER_PADDING_SIZE));if(!cp->extradata)break;memcpy(cp->extradata,headers.data(),headers.size());cp->extradata_size=int(headers.size());
        int muxResult=avio_open(&mux->pb,utf8(partial).c_str(),AVIO_FLAG_WRITE);
        if(muxResult<0){failAv(L"Creating output file",muxResult);break;}
        muxResult=avformat_write_header(mux,nullptr);
        if(muxResult<0){failAv(L"Writing MP4 file header",muxResult);break;}headerWritten=true;
        uint64_t sourceCount=0,generatedCount=0,holdCount=0;int64_t outputIndex=0;bool error=false;std::shared_ptr<pipeline::FrameLease> lastReal;
        double previousPts=0;int64_t lastOutputUs=-1;
        const uint32_t multiplier=options.fg?options.fgMultiplier:1u;
        auto encodeAt=[&](unsigned slot,bool generated,double seconds){
            const int64_t timeUs=std::max(lastOutputUs+1,int64_t(std::llround(seconds*1000000)));
            timestamps.emplace_back(outputIndex,timeUs);lastOutputUs=timeUs;
            return encoder->encode(slot,generated,outputIndex++);
        };
        while(!cancel){if(frameBoundary&&!frameBoundary()){error=true;break;}pipeline::FramePacket packet;const AVFrame* frame=nullptr;
            source::SourceReadStatus rs;
            if(sourceCount==0){packet=firstPacket;frame=firstFrame;rs=source::SourceReadStatus::Frame;}
            else rs=source.read(packet,&frame);
            if(rs==source::SourceReadStatus::Eos)break;if(rs!=source::SourceReadStatus::Frame){failureReason=source.errorMessage();error=true;break;}
            double sourcePts=packet.pts.toDouble();
            if(sourceCount==0)videoOriginSeconds=!packet.pts.isUnknown()&&std::isfinite(sourcePts)?sourcePts:0;
            double pts=sourcePts-videoOriginSeconds;
            const bool repairPts=packet.pts.isUnknown()||!std::isfinite(pts)||(sourceCount&&pts<=previousPts);
            if(repairPts){
                pts=sourceCount?previousPts+sourceInterval:0;++repairedTimestamps;
                if(repairedTimestamps<=5)veyra::log::warn("export-timeline",std::format("timestamp repaired source={} raw={} output={}",sourceCount,sourcePts,pts));
            }
            pipeline::EnhanceGraph::FrameOutputs out;if(!graph.process(frame,(pts+videoOriginSeconds)*1000,sourceCount==0||repairPts||pipeline::breaksHistory(packet.flags),out,packet.sequence,&packet.colorInfo,&packet.hardwareSurface,false)){error=true;break;}
            const auto readyStart=std::chrono::steady_clock::now();
            while(!cancel&&!graph.resolveGeneration(out)){
                if(std::chrono::steady_clock::now()-readyStart>std::chrono::seconds(2)){error=true;failureReason=L"The GPU did not finish enhancing one frame within 2 seconds (the GPU may be busy or the driver may be malfunctioning)";veyra::log::error("export","frame GPU completion timed out after 2s");break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(error||cancel)break;
            if(sourceCount>0&&options.fg){
                for(uint32_t j=1;j<options.fgMultiplier;++j){
                    pipeline::BatchFrame* item=nullptr;
                    for(uint32_t k=0;k<out.batch.count;++k)if(out.batch.frames[k].subframe==j&&out.batch.frames[k].validity==pipeline::GenerationValidity::Valid)item=&out.batch.frames[k];
                    const double generatedPts=previousPts+(pts-previousPts)*j/multiplier;
                    if(item){if(!encodeAt(item->lease->slot,true,generatedPts)){error=true;break;}item->lease->consumerFence=ring.lastSignaledValue();++generatedCount;}
                    else {if(!lastReal||!encodeAt(lastReal->slot,false,generatedPts)){error=true;break;}lastReal->consumerFence=ring.lastSignaledValue();++holdCount;}
                }
            }
            if(error)break;
            auto& real=out.batch.frames[out.batch.count-1];
            if(!encodeAt(real.lease->slot,false,pts)){error=true;break;}
            real.lease->consumerFence=ring.lastSignaledValue();lastReal=real.lease;
            previousPts=pts;
            ++sourceCount;if(counts)counts({sourceCount,generatedCount,holdCount,uint64_t(written)});progress(info.duration.toDouble()>0?std::clamp(pts/info.duration.toDouble(),0.0,.99):0,std::format(L"Exporting: {} source frames / {} encoded frames ({})",sourceCount,outputIndex,encoderName));
            if(maxFrames&&sourceCount>=maxFrames)break;
        }
        if(error||cancel)break;
        if(options.fg&&lastReal)for(uint32_t j=1;j<multiplier;++j){if(!encodeAt(lastReal->slot,false,previousPts+sourceInterval*j/multiplier)){error=true;break;}++holdCount;}
        if(error)break;
        veyra::log::info("export-counts",std::format("source={} generated={} hold={} output={} multiplier={} repairedTimestamps={} backend={} encoder={} bitrateMbps={} note={} (holds are not DLSSG)",sourceCount,generatedCount,holdCount,outputIndex,multiplier,repairedTimestamps,frameGenerationBackendName(options.settings.frameGenerationBackend),std::string(sink::encoderBackendName(encoder->backend())),options.settings.exportBitrateMbps,utf8(fgNote)));
        progress(.99,L"Finishing: waiting for the encoder to output the remaining frames");
        if(!encoder->finish()){if(failureReason.empty())failureReason=L"Encoder finalization failed; see the encoder diagnostics";break;}
        if(!flushVideo(lastOutputUs+std::max<int64_t>(1,int64_t(std::llround(sourceInterval*1000000/multiplier)))))break;
        if(!writeAudioUntil(audioEndSeconds))break;
        progress(.995,L"Finishing: writing the MP4 index");
        muxResult=av_write_trailer(mux);
        if(muxResult<0){failAv(L"Writing MP4 index",muxResult);break;}
        ok=written>0;if(counts)counts({sourceCount,generatedCount,holdCount,uint64_t(written)});
    }while(false); }catch(const std::exception& e){veyra::log::error("export",std::format("exception: {}",e.what()));failureReason=L"Export exception; see diagnostics";ok=false;}
    encoder.reset();if(mux){if(mux->pb){const int rc=avio_closep(&mux->pb);if(rc<0){failAv(L"Flushing and closing output file",rc);ok=false;}}avformat_free_context(mux);}if(audioInput)avformat_close_input(&audioInput);av_packet_free(&audioPacket);
    ring.drainQueue();graph.shutdown();source.close();ring.shutdown();ctx.shutdown();
    if(ok){
        progress(.999,L"Saving the final file");
        ok=!cancel&&MoveFileExW(partial.c_str(),output.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
        if(!ok&&!cancel){const DWORD error=GetLastError();failureReason=std::format(L"The video was encoded, but saving the file name failed (Windows error {}); the partial file can be kept",error);veyra::log::error("export-rename",std::format("MoveFileExW failed error={} partial={}",error,utf8(partial)));}
        if(ok)veyra::log::info("export","encoder drained, mux closed, output saved; no post-export decoding");
    }
    if(ok){
        std::wstring done=encoderName.empty()?L"Video export complete":std::format(L"Video export complete ({})",encoderName);
        if(repairedTimestamps>0)done+=std::format(L" ({} frames with missing or out-of-order timestamps were repaired)",repairedTimestamps);
        progress(1,fgNote.empty()?done:fgNote+L"; "+done);
    }
    else {
        std::wstring message=cancel?L"Export cancelled":failureReason.empty()?L"Video export failed; see diagnostics":failureReason;
        std::error_code ec;
        message+=std::filesystem::exists(partial,ec)?L"; the partial file was kept":L"; no output file was produced";
        progress(0,message);
    }
    return ok;
}
}
