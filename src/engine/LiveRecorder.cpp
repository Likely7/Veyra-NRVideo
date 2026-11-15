#include "veyra/engine/LiveRecorder.h"
#include "veyra/engine/MicrophoneCapture.h"
#include "veyra/engine/ReplayBuffer.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include "veyra/sink/MfLiveEncoder.h"
#include "veyra/sink/NvencLiveEncoder.h"
#include "veyra/sink/RecordingAudioTap.h"
#include "veyra/Log.h"

#include <dxgi1_6.h>
#include <objbase.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <format>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace veyra::engine {
using Microsoft::WRL::ComPtr;
namespace {

constexpr unsigned kSlots=6;
constexpr size_t kBacklogSkipBytes=size_t(64)<<20;   // the disk is behind: skip frames
constexpr size_t kBacklogFailBytes=size_t(512)<<20;  // far behind: stop with an error
constexpr int kAudioRate=48000;
constexpr int64_t kAudioHardGap=kAudioRate*60/1000;  // beyond 60 ms: insert silence or drop
constexpr double kAudioSoftGap=kAudioRate*5/1000.0;  // beyond 5 ms: resample it away
constexpr int kCompensationWindow=kAudioRate*4;      // one soft correction per 4 s
constexpr int kCompensationMax=kCompensationWindow/1000; // at most 0.1 % speed
constexpr size_t kAudioQueueFrames=size_t(kAudioRate)*4;
constexpr uint32_t kGpuWaitMs=3000;
constexpr int64_t kReplayRetryUs=5000000;            // a failed replay encoder is retried after 5 s
constexpr int64_t kReplaySaveWaitMs=600;             // a save waits this long for the tracks to reach the request

std::string utf8(const std::wstring& text){
    if(text.empty())return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),size,nullptr,nullptr);
    return out;
}
std::wstring wide(const std::string& text){
    if(text.empty())return {};
    const int size=MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),nullptr,0);
    std::wstring out(size_t(size),L'\0');
    MultiByteToWideChar(CP_UTF8,0,text.data(),int(text.size()),out.data(),size);
    return out;
}
std::wstring avError(int code){char buffer[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,buffer,sizeof(buffer));return wide(buffer);}
std::wstring fileStamp(){
    const std::time_t now=std::time(nullptr);std::tm local{};localtime_s(&local,&now);
    wchar_t buffer[32]{};
    swprintf_s(buffer,L"%04d-%02d-%02d-%02d%02d%02d",local.tm_year+1900,local.tm_mon+1,local.tm_mday,local.tm_hour,local.tm_min,local.tm_sec);
    return buffer;
}
// About 0.15 bit per pixel for H.264 at the recorded rate; HEVC needs ~2/3 of it.
uint32_t autoBitrateMbps(uint32_t width,uint32_t height,uint32_t fps,bool hevc){
    const double mbps=double(width)*height*fps*0.15*(hevc?0.65:1.0)/1e6;
    return uint32_t(std::clamp<long>(std::lround(mbps),hevc?5:6,hevc?120:150));
}
uint32_t adapterVendor(ID3D12Device* device){
    ComPtr<IDXGIFactory4> factory;
    if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))return 0;
    ComPtr<IDXGIAdapter1> adapter;
    if(FAILED(factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter))))return 0;
    DXGI_ADAPTER_DESC1 desc{};
    return SUCCEEDED(adapter->GetDesc1(&desc))?desc.VendorId:0;
}
// The replay buffer's memory cap when the settings leave it open: an eighth of
// the installed memory, 512 MB to 4 GB.
uint64_t defaultReplayMemory(){
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
    const uint64_t total=GlobalMemoryStatusEx(&memory)?memory.ullTotalPhys:(uint64_t(8)<<30);
    return std::clamp<uint64_t>(total/8,uint64_t(512)<<20,uint64_t(4)<<30);
}
std::wstring uniqueFile(const std::wstring& directory,const std::wstring& base){
    for(unsigned attempt=0;;++attempt){
        auto file=std::filesystem::path(directory)/(base+(attempt?std::format(L"-{}",attempt):L"")+L".mp4");
        std::error_code code;
        if(!std::filesystem::exists(file,code))return file.wstring();
    }
}

// The picture of a recording and of a saved replay.
bool setVideoParameters(AVCodecParameters* par,uint32_t width,uint32_t height,uint32_t hdrMode,bool hevc,const std::vector<uint8_t>& extradata){
    const bool hdr=hdrMode!=0;
    par->codec_type=AVMEDIA_TYPE_VIDEO;par->codec_id=hevc?AV_CODEC_ID_HEVC:AV_CODEC_ID_H264;
    par->width=int(width);par->height=int(height);
    par->format=hdr?AV_PIX_FMT_YUV420P10LE:AV_PIX_FMT_YUV420P;
    par->color_range=AVCOL_RANGE_MPEG;
    par->color_space=hdr?AVCOL_SPC_BT2020_NCL:AVCOL_SPC_BT709;
    par->color_primaries=hdr?AVCOL_PRI_BT2020:AVCOL_PRI_BT709;
    par->color_trc=hdr?AVCOL_TRC_SMPTE2084:AVCOL_TRC_BT709;
    if(hdr)par->profile=AV_PROFILE_HEVC_MAIN_10;
    par->extradata=static_cast<uint8_t*>(av_mallocz(extradata.size()+AV_INPUT_BUFFER_PADDING_SIZE));
    if(!par->extradata)return false;
    std::memcpy(par->extradata,extradata.data(),extradata.size());par->extradata_size=int(extradata.size());
    return true;
}

struct AudioBlock {std::vector<float> pcm;size_t frames=0;sink::AudioFormat format;int64_t atUs=0;};

// One sound source on the recording timeline: blocks stamped with the
// recording clock in (the played sound at the moment it reaches the
// endpoint, the microphone at the moment it was captured), 48 kHz stereo
// planar samples out, each at its timeline position: the fifo's front is the
// sample its track encodes next. Gaps become silence and clock drift is
// absorbed by resampling, so the track stays continuous and in sync.
struct AudioLane {
    // Producer thread (audio owner or microphone).
    std::mutex mutex;
    std::deque<AudioBlock> blocks;
    size_t queued=0;
    const void* source=nullptr;
    int64_t sourceUs=0;
    std::atomic<int64_t> arrivalUs{0};
    std::atomic<uint64_t> gaps{0},overflow{0};
    // Writer thread.
    SwrContext* swr=nullptr;
    sink::AudioFormat swrFormat;
    AVAudioFifo* fifo=nullptr;
    std::vector<float> planes[2];
    std::vector<float> silence;
    int64_t produced=0,compensationEnd=0;
    double driftEma=0;
    bool aligned=false;

    AudioLane()=default;
    AudioLane(const AudioLane&)=delete;
    AudioLane& operator=(const AudioLane&)=delete;
    ~AudioLane(){release();}
    void release(){
        if(swr)swr_free(&swr);
        if(fifo){av_audio_fifo_free(fifo);fifo=nullptr;}
    }
    bool open(){if(!fifo)fifo=av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP,2,kAudioRate);return fifo!=nullptr;}

    // Producer thread: copy and return. One source at a time; another takes
    // over only after the current one has been quiet for half a second
    // (endpoint rebuilds).
    void push(const float* pcm,size_t count,const sink::AudioFormat& format,int64_t atUs,const void* from){
        const int64_t now=sink::recordingClockUs();
        std::lock_guard lock(mutex);
        if(source&&source!=from&&now-sourceUs<500000)return;
        source=from;sourceUs=now;arrivalUs=now;
        AudioBlock block;block.pcm.assign(pcm,pcm+count*format.channels);block.frames=count;block.format=format;block.atUs=atUs;
        queued+=count;blocks.push_back(std::move(block));
        while(queued>kAudioQueueFrames&&!blocks.empty()){queued-=blocks.front().frames;blocks.pop_front();++overflow;}
    }
    bool configureSwr(const sink::AudioFormat& format){
        if(swr&&format==swrFormat)return true;
        if(swr){
            // Rare: the source layout changed. Keep what the old one buffered.
            if(!convertPcm(nullptr,0))return false;
            swr_free(&swr);
        }
        AVChannelLayout in{};
        if(av_channel_layout_from_mask(&in,format.mask)<0||in.nb_channels!=int(format.channels)){
            av_channel_layout_uninit(&in);av_channel_layout_default(&in,int(format.channels));
        }
        const AVChannelLayout out=AV_CHANNEL_LAYOUT_STEREO;
        int rc=swr_alloc_set_opts2(&swr,&out,AV_SAMPLE_FMT_FLTP,kAudioRate,&in,AV_SAMPLE_FMT_FLT,kAudioRate,0,nullptr);
        av_channel_layout_uninit(&in);
        if(rc<0||!swr)return false;
        // The resampler at an equal rate is what lets swr_set_compensation
        // absorb the source clock's drift against the recording clock.
        av_opt_set_int(swr,"flags",SWR_FLAG_RESAMPLE,0);
        if(swr_init(swr)<0){swr_free(&swr);return false;}
        swrFormat=format;compensationEnd=produced;
        silence.assign(size_t(kAudioRate/10)*format.channels,0.0f);
        return true;
    }
    bool convertPcm(const float* pcm,int count){
        const int capacity=swr_get_out_samples(swr,count)+32;
        if(capacity<=0)return true;
        for(auto& plane:planes)if(plane.size()<size_t(capacity))plane.resize(size_t(capacity));
        uint8_t* out[2]={reinterpret_cast<uint8_t*>(planes[0].data()),reinterpret_cast<uint8_t*>(planes[1].data())};
        const uint8_t* in[1]={reinterpret_cast<const uint8_t*>(pcm)};
        const int got=swr_convert(swr,out,capacity,pcm?in:nullptr,pcm?count:0);
        if(got<0)return false;
        if(got>0){
            void* data[2]={planes[0].data(),planes[1].data()};
            if(av_audio_fifo_write(fifo,data,got)<got)return false;
            produced+=got;
        }
        return true;
    }
    bool feedSilence(int64_t count){
        const int64_t chunk=int64_t(silence.size()/swrFormat.channels);
        while(count>0){
            const int n=int(std::min(count,chunk));
            if(!convertPcm(silence.data(),n))return false;
            count-=n;
        }
        return true;
    }
    int64_t outputPosition() const {return produced+(swr?swr_get_delay(swr,kAudioRate):0);}
    // Writer thread: the queued blocks into the fifo. final: up to the
    // picture's end (endTimeUs) and the resampler flushed.
    bool process(bool final,int64_t endTimeUs,int64_t t0){
        std::deque<AudioBlock> taken;
        {std::lock_guard lock(mutex);taken.swap(blocks);queued=0;}
        if(!t0)return true; // nothing recorded yet: earlier sound is not part of the file
        if(!swr&&!configureSwr(taken.empty()?sink::AudioFormat{}:taken.front().format))return false;
        for(auto& block:taken){
            if(!configureSwr(block.format))return false;
            int64_t start=(block.atUs-t0)*kAudioRate/1000000;
            size_t offset=0;
            if(start<0){const auto trim=size_t(-start);if(trim>=block.frames)continue;offset=trim;start=0;}
            int64_t drift=start-outputPosition();
            // The first block is placed exactly; later ones only beyond the
            // hard limit (gaps, rebuffering) - smaller drift is resampled away.
            const bool align=!aligned||drift>kAudioHardGap||drift< -kAudioHardGap;
            if(align&&drift>0){if(!feedSilence(drift))return false;if(aligned)++gaps;drift=0;}
            else if(align&&drift<0){const auto trim=size_t(-drift);if(trim>=block.frames-offset)continue;offset+=trim;drift=0;}
            aligned=true;
            driftEma=driftEma*0.9+double(drift)*0.1;
            if(std::abs(driftEma)>kAudioSoftGap&&produced>=compensationEnd){
                const int delta=int(std::clamp(driftEma,-double(kCompensationMax),double(kCompensationMax)));
                if(delta&&swr_set_compensation(swr,delta,kCompensationWindow)>=0)compensationEnd=produced+kCompensationWindow;
            }
            if(!convertPcm(block.pcm.data()+offset*block.format.channels,int(block.frames-offset)))return false;
        }
        if(final){
            const int64_t target=endTimeUs*kAudioRate/1000000;
            if(target>outputPosition()&&!feedSilence(target-outputPosition()))return false;
            return convertPcm(nullptr,0);
        }
        if(taken.empty()&&sink::recordingClockUs()-std::max(arrivalUs.load(),t0)>300000){
            // No sound arrives (paused, muted source, no audio monitoring, a
            // silent or missing microphone): keep the track continuous,
            // 200 ms behind real time.
            const int64_t target=(sink::recordingClockUs()-t0-200000)*kAudioRate/1000000;
            if(target>outputPosition()&&!feedSilence(target-outputPosition()))return false;
        }
        return true;
    }
};

// One AAC track: one lane, or the played sound with the microphone mixed in.
struct AudioTrack {
    AudioLane* lane=nullptr;
    AudioLane* mix=nullptr;
    std::string name;                 // MP4 handler name
    AVCodecContext* codec=nullptr;
    AVFrame* frame=nullptr;
    std::vector<float> extra[2];
    int64_t encodedSamples=0;
    AVStream* stream=nullptr;         // a recording's stream
    std::shared_ptr<const AVCodecParameters> parameters;

    AudioTrack()=default;
    AudioTrack(const AudioTrack&)=delete;
    AudioTrack& operator=(const AudioTrack&)=delete;
    ~AudioTrack(){release();}
    void release(){
        if(codec)avcodec_free_context(&codec);
        if(frame)av_frame_free(&frame);
    }
    int frameSize() const {return codec&&codec->frame_size>0?codec->frame_size:1024;}
    bool open(){
        const AVCodec* encoder=avcodec_find_encoder(AV_CODEC_ID_AAC);
        if(!encoder)return false;
        codec=avcodec_alloc_context3(encoder);
        if(!codec)return false;
        codec->sample_fmt=AV_SAMPLE_FMT_FLTP;codec->sample_rate=kAudioRate;
        const AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&codec->ch_layout,&stereo);
        codec->bit_rate=192000;codec->time_base=AVRational{1,kAudioRate};
        // MP4 (recordings and saved replays) keeps the decoder configuration
        // in the sample description.
        codec->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
        if(avcodec_open2(codec,encoder,nullptr)<0){avcodec_free_context(&codec);return false;}
        frame=av_frame_alloc();
        if(!frame)return false;
        frame->nb_samples=frameSize();
        frame->format=AV_SAMPLE_FMT_FLTP;frame->sample_rate=kAudioRate;
        av_channel_layout_copy(&frame->ch_layout,&stereo);
        if(av_frame_get_buffer(frame,0)<0)return false;
        AVCodecParameters* par=avcodec_parameters_alloc();
        if(!par||avcodec_parameters_from_context(par,codec)<0){avcodec_parameters_free(&par);return false;}
        parameters=std::shared_ptr<const AVCodecParameters>(par,[](const AVCodecParameters* p){auto* q=const_cast<AVCodecParameters*>(p);avcodec_parameters_free(&q);});
        return true;
    }
    template<class Out> bool send(AVFrame* input,Out& out){
        if(avcodec_send_frame(codec,input)<0)return false;
        AVPacket* packet=av_packet_alloc();
        if(!packet)return false;
        int rc=0;
        while((rc=avcodec_receive_packet(codec,packet))>=0){
            const bool ok=out(packet);
            av_packet_unref(packet);
            if(!ok){av_packet_free(&packet);return false;}
        }
        av_packet_free(&packet);
        return rc==AVERROR(EAGAIN)||rc==AVERROR_EOF;
    }
    // What every lane of the track holds; the final pass encodes up to
    // `limit` samples (the picture's end), discards the rest and flushes.
    template<class Out> bool encode(bool final,int64_t limit,Out&& out){
        const int size=frameSize();
        for(;;){
            int64_t available=av_audio_fifo_size(lane->fifo);
            if(mix)available=std::min<int64_t>(available,av_audio_fifo_size(mix->fifo));
            if(final)available=std::min(available,limit-encodedSamples);
            if(available<=0||(!final&&available<size))break;
            const int n=int(std::min<int64_t>(available,size));
            if(av_frame_make_writable(frame)<0)return false;
            frame->nb_samples=n;
            if(av_audio_fifo_read(lane->fifo,reinterpret_cast<void**>(frame->data),n)<n)return false;
            if(mix){
                for(auto& plane:extra)if(plane.size()<size_t(n))plane.resize(size_t(n));
                void* data[2]={extra[0].data(),extra[1].data()};
                if(av_audio_fifo_read(mix->fifo,data,n)<n)return false;
                for(int c=0;c<2;++c){
                    auto* samples=reinterpret_cast<float*>(frame->data[c]);
                    for(int i=0;i<n;++i)samples[i]=std::clamp(samples[i]+extra[c][size_t(i)],-1.0f,1.0f);
                }
            }
            frame->pts=encodedSamples;encodedSamples+=n;
            if(!send(frame,out))return false;
            frame->nb_samples=size;
        }
        if(!final)return true;
        for(auto* each:{lane,mix})
            if(each&&av_audio_fifo_size(each->fifo)>0)av_audio_fifo_drain(each->fifo,av_audio_fifo_size(each->fifo));
        return send(nullptr,out);
    }
};

// A saved replay: the clip's packets in time order into a new MP4. The
// timeline starts at the clip's first keyframe; sound before it (decoder
// priming) gets negative times, which the MP4 edit list cuts.
bool writeReplayFile(const std::wstring& path,const ReplayBuffer::Clip& clip,std::wstring& error){
    AVFormatContext* mux=nullptr;
    struct Close {AVFormatContext*& mux;~Close(){if(mux){if(mux->pb)avio_closep(&mux->pb);avformat_free_context(mux);}}} close{mux};
    auto fail=[&](std::wstring reason){
        error=std::move(reason);
        if(mux&&mux->pb)avio_closep(&mux->pb);
        std::error_code code;std::filesystem::remove(path,code);
        return false;
    };
    if(avformat_alloc_output_context2(&mux,nullptr,"mp4",nullptr)<0||!mux)return fail(L"无法创建回放文件封装器");
    AVStream* video=avformat_new_stream(mux,nullptr);
    if(!video)return fail(L"无法创建回放视频流");
    video->time_base=AVRational{1,1000000};
    video->avg_frame_rate=AVRational{int(std::max(1u,clip.video.fps)),1};
    if(!setVideoParameters(video->codecpar,clip.video.width,clip.video.height,clip.video.hdrMode,clip.video.hevc,clip.video.extradata))
        return fail(L"无法写入回放文件头");
    std::vector<AVStream*> audio;
    for(const auto& format:clip.audio){
        AVStream* stream=avformat_new_stream(mux,nullptr);
        if(!stream||!format.parameters||avcodec_parameters_copy(stream->codecpar,format.parameters.get())<0)return fail(L"无法创建回放音频流");
        stream->time_base=AVRational{1,kAudioRate};
        if(!format.name.empty())av_dict_set(&stream->metadata,"handler_name",format.name.c_str(),0);
        audio.push_back(stream);
    }
    int rc=avio_open(&mux->pb,utf8(path).c_str(),AVIO_FLAG_WRITE);
    if(rc<0)return fail(L"无法创建回放文件："+avError(rc));
    if((rc=avformat_write_header(mux,nullptr))<0)return fail(L"无法写入回放文件头："+avError(rc));
    const int64_t startSamples=av_rescale(clip.startUs,kAudioRate,1000000);
    auto write=[&](AVStream* stream,const ReplayBuffer::Packet& packet,int64_t pts,int64_t duration,AVRational base){
        AVPacket* out=av_packet_alloc();
        if(!out||av_new_packet(out,int(packet.data->size()))<0){av_packet_free(&out);return false;}
        std::memcpy(out->data,packet.data->data(),packet.data->size());
        out->stream_index=stream->index;out->pts=out->dts=pts;out->duration=std::max<int64_t>(1,duration);
        if(packet.key)out->flags|=AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(out,base,stream->time_base);
        const int written=av_interleaved_write_frame(mux,out);
        av_packet_free(&out);
        return written>=0;
    };
    const auto& pictures=clip.videoPackets;
    size_t v=0;std::vector<size_t> a(audio.size(),0);
    for(;;){
        // The earliest next packet, in µs from the clip start.
        int64_t best=INT64_MAX;int which=-1;
        if(v<pictures.size()){best=pictures[v].pts-clip.startUs;which=0;}
        for(size_t t=0;t<audio.size();++t){
            if(a[t]>=clip.audioPackets[t].size())continue;
            const int64_t at=av_rescale(clip.audioPackets[t][a[t]].pts-startSamples,1000000,kAudioRate);
            if(at<best){best=at;which=int(t)+1;}
        }
        if(which<0)break;
        bool ok=true;
        if(which==0){
            const int64_t next=v+1<pictures.size()?pictures[v+1].pts:clip.endUs;
            ok=write(video,pictures[v],pictures[v].pts-clip.startUs,next-pictures[v].pts,AVRational{1,1000000});++v;
        }else{
            const size_t t=size_t(which-1);const auto& packet=clip.audioPackets[t][a[t]++];
            ok=write(audio[t],packet,packet.pts-startSamples,packet.duration,AVRational{1,kAudioRate});
        }
        if(!ok)return fail(L"写入回放文件失败（磁盘已满或没有写入权限？）");
    }
    if((rc=av_write_trailer(mux))<0)return fail(L"回放文件收尾失败："+avError(rc));
    avio_closep(&mux->pb);
    return true;
}

} // namespace

// One output file, or one stretch of the instant replay buffer: the capture
// slots and conversion (engine thread), the encoder thread and the writer
// thread. Created by record() on the engine thread; LiveRecorder keeps it
// until both threads are done.
struct LiveRecorder::Segment final : sink::RecordingAudioSink, MicrophoneSink {
    // Fixed at creation.
    ComPtr<ID3D12Device> device;
    uint32_t width=0,height=0,hdrMode=0,fps=60,bitrateMbps=20,vendor=0;
    bool hevc=false,nvenc=false;
    bool sound=false,microphone=false,microphoneTrack=false; // what the sound tracks take
    std::atomic<bool> audio{false};             // the file (or buffer) has sound tracks
    std::wstring path;                          // a recording's file
    std::shared_ptr<ReplayBuffer> replay;       // the instant replay buffer instead of a file

    // Engine thread: conversion into the slots.
    pipeline::ComputePass convert;
    pipeline::DescriptorStager stager;
    pipeline::StateTracker states;
    ComPtr<ID3D12Resource> y,uv;
    struct Slot {
        ComPtr<ID3D12Resource> nv12;            // NVENC input
        ComPtr<ID3D12Resource> yRead,uvRead;    // Media Foundation readback
        UINT yPitch=0,uvPitch=0;
        std::atomic<int> state{0};              // 0 free, 1 recorded, 2 queued for the encoder
        ComPtr<ID3D12Fence> fence;              // the presenter's fence; kept alive past the session
        uint64_t value=0;
        int64_t ptsUs=0;
    };
    std::array<Slot,kSlots> slots;
    int pending=-1;
    std::atomic<int64_t> t0Us{0};               // recording clock of the first frame
    int64_t lastPtsUs=-1;
    std::atomic<uint64_t> frames{0},dropped{0};

    // Encoder thread.
    std::thread encoderThread;
    std::mutex jobMutex;
    std::condition_variable jobCv;
    std::deque<unsigned> jobs;
    bool jobsClosed=false;
    std::atomic<bool> encoderReady{false};

    // Writer thread.
    std::thread writerThread;
    struct VideoPacket {std::vector<uint8_t> data;int64_t ptsUs=0;bool key=false;};
    std::mutex outMutex;
    std::condition_variable outCv;
    std::deque<VideoPacket> videoPackets;
    std::vector<uint8_t> headers;
    bool headersReady=false,encoderDone=false;
    std::atomic<size_t> backlogBytes{0};
    std::atomic<int64_t> endUs{-1};
    std::atomic<uint64_t> videoWritten{0};
    std::atomic<int64_t> writtenUs{0};

    // Sound: the played sound (tap) and the microphone, each on its own lane;
    // up to two AAC tracks take them (sound / sound + microphone / microphone).
    AudioLane soundLane,micLane;
    std::array<AudioTrack,2> tracks;
    unsigned trackCount=0;

    // Writer-owned FFmpeg state of a recording.
    AVFormatContext* mux=nullptr;
    AVStream* videoStream=nullptr;
    int64_t lastVideoDts=INT64_MIN;
    bool headerWritten=false,originSet=false;

    // Shared state.
    std::atomic<bool> closing{false},failed{false},encoderFinished{false},writerFinished{false};
    mutable std::mutex textMutex;
    std::wstring error,encoderName,note;

    ~Segment() override {
        if(encoderThread.joinable())encoderThread.join();
        if(writerThread.joinable())writerThread.join();
        releaseMux();
    }

    bool done() const {
        if(!encoderFinished.load()||!writerFinished.load()||pending>=0)return false;
        // An encoder failure may leave submitted capture commands in flight.
        // Keep their textures alive without making the playback thread wait.
        for(const auto& slot:slots)
            if(slot.fence&&slot.fence->GetCompletedValue()<slot.value)return false;
        return true;
    }
    void join(){if(encoderThread.joinable())encoderThread.join();if(writerThread.joinable())writerThread.join();}
    std::wstring errorText() const {std::lock_guard lock(textMutex);return error;}
    std::wstring encoderText() const {std::lock_guard lock(textMutex);return encoderName;}
    std::wstring noteText() const {std::lock_guard lock(textMutex);return note;}
    void setNote(std::wstring text){std::lock_guard lock(textMutex);note=std::move(text);}
    std::string name() const {return replay?std::string("replay buffer"):utf8(path);}

    void fail(const std::wstring& reason){
        {std::lock_guard lock(textMutex);if(error.empty())error=reason;}
        if(!failed.exchange(true))veyra::log::error("live-recorder",std::format("segment failed path={} reason={}",name(),utf8(reason)));
        closeJobs();
    }
    void closeJobs(){
        closing=true;
        {std::lock_guard lock(jobMutex);jobsClosed=true;}
        jobCv.notify_all();
    }
    // Any thread: no more frames; the threads finish the file.
    void finish(int64_t endClockUs){
        const int64_t t0=t0Us.load();
        if(t0&&endUs.load()<0)endUs=std::max<int64_t>(0,endClockUs-t0);
        // UI stop can arrive between record() and submitted(). Keep the
        // encoder alive until that command list is submitted or discarded.
        closing=true;
        if(pending<0)closeJobs();
    }
    bool matches(const PresentCapture::Frame& frame) const {
        return frame.width==width&&frame.height==height&&frame.hdrMode==hdrMode;
    }

    // ---- creation (engine thread) -------------------------------------------
    bool create(std::wstring& reason){
        std::vector<uint8_t> shader;
        if(!convert.loadShader("RgbToNv12.dxil",shader)||!convert.create(device.Get(),shader,kSlots+2,1,2)){reason=L"录制转换着色器加载失败";return false;}
        if(!stager.initialize(device.Get(),kSlots)){reason=L"录制描述符分配失败";return false;}
        const bool hdr=hdrMode!=0;
        y=pipeline::makeTexture(device.Get(),width,height,hdr?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM,true);
        uv=pipeline::makeTexture(device.Get(),width/2,height/2,hdr?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM,true);
        if(!y||!uv){reason=L"录制显存分配失败";return false;}
        pipeline::makeUav(device.Get(),y.Get(),hdr?DXGI_FORMAT_R16_UNORM:DXGI_FORMAT_R8_UNORM,pipeline::cpuHandleOf(convert,kSlots));
        pipeline::makeUav(device.Get(),uv.Get(),hdr?DXGI_FORMAT_R16G16_UNORM:DXGI_FORMAT_R8G8_UNORM,pipeline::cpuHandleOf(convert,kSlots+1));
        for(auto& slot:slots){
            if(nvenc){
                slot.nv12=pipeline::makeTexture(device.Get(),width,height,hdr?DXGI_FORMAT_P010:DXGI_FORMAT_NV12,false);
                if(!slot.nv12){reason=L"录制显存分配失败";return false;}
                continue;
            }
            auto readback=[&](UINT rowBytes,UINT rows,ComPtr<ID3D12Resource>& out,UINT& pitch){
                pitch=(rowBytes+255)/256*256;
                D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=uint64_t(pitch)*rows;bd.Height=1;
                bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                return SUCCEEDED(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&out)));
            };
            if(!readback(width,height,slot.yRead,slot.yPitch)||!readback(width,height/2,slot.uvRead,slot.uvPitch)){reason=L"录制回读缓冲分配失败";return false;}
        }
        encoderThread=std::thread([this]{encoderMain();});
        writerThread=std::thread([this]{writerMain();});
        return true;
    }

    // ---- capture (engine thread) ---------------------------------------------
    bool capture(ID3D12GraphicsCommandList* list,const PresentCapture::Frame& frame,int64_t nowUs){
        if(closing.load()||!encoderReady.load())return false;
        if(backlogBytes.load()>=kBacklogFailBytes){fail(L"磁盘写入跟不上录制，已停止录制（请换更快的磁盘或降低码率）");return false;}
        if(backlogBytes.load()>=kBacklogSkipBytes){++dropped;return false;}
        int index=-1;
        for(unsigned i=0;i<kSlots;++i){int expected=0;if(slots[i].state.compare_exchange_strong(expected,1)){index=int(i);break;}}
        if(index<0){++dropped;return false;}
        auto& slot=slots[size_t(index)];
        if(!t0Us.load())t0Us=nowUs;
        int64_t pts=nowUs-t0Us.load();
        if(pts<=lastPtsUs)pts=lastPtsUs+1;
        lastPtsUs=pts;slot.ptsUs=pts;
        // Per-frame SRV through the staging heap (GpuPassUtils.h driver note:
        // a direct SRV write into a shader-visible heap can corrupt Present).
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=frame.format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
        stager.stageSrv(frame.texture,&view,convert.heap.Get(),UINT(index));
        D3D12_RESOURCE_BARRIER source{};source.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;source.Transition.pResource=frame.texture;
        source.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        source.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;source.Transition.StateAfter=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1,&source);
        states.transition(list,y.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        states.transition(list,uv.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        // reserved.x = HDR mode, reserved.y = output dither step (RgbToNv12.hlsl).
        const float constants[8]={std::bit_cast<float>(width),std::bit_cast<float>(height),std::bit_cast<float>(hdrMode),frame.ditherStep,0,0,0,0};
        convert.bind(list,constants,pipeline::gpuHandleOf(convert,UINT(index)).ptr,pipeline::gpuHandleOf(convert,kSlots).ptr);
        list->Dispatch((width+15)/16,(height+15)/16,1);
        states.uavBarrier(list,y.Get());states.uavBarrier(list,uv.Get());
        states.transition(list,y.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        states.transition(list,uv.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE);
        std::swap(source.Transition.StateBefore,source.Transition.StateAfter);
        list->ResourceBarrier(1,&source);
        if(nvenc){
            D3D12_RESOURCE_BARRIER target{};target.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;target.Transition.pResource=slot.nv12.Get();
            target.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            target.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;target.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
            list->ResourceBarrier(1,&target);
            for(UINT plane=0;plane<2;++plane){
                D3D12_TEXTURE_COPY_LOCATION from{},to{};
                from.pResource=plane?uv.Get():y.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;from.SubresourceIndex=0;
                to.pResource=slot.nv12.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;to.SubresourceIndex=plane;
                list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            }
            std::swap(target.Transition.StateBefore,target.Transition.StateAfter);
            list->ResourceBarrier(1,&target);
        }else{
            auto copyPlane=[&](ID3D12Resource* from,ID3D12Resource* to,DXGI_FORMAT format,UINT w,UINT h,UINT pitch){
                D3D12_TEXTURE_COPY_LOCATION a{},b{};
                a.pResource=from;a.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;a.SubresourceIndex=0;
                b.pResource=to;b.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                b.PlacedFootprint.Footprint.Format=format;b.PlacedFootprint.Footprint.Width=w;b.PlacedFootprint.Footprint.Height=h;
                b.PlacedFootprint.Footprint.Depth=1;b.PlacedFootprint.Footprint.RowPitch=pitch;
                list->CopyTextureRegion(&b,0,0,0,&a,nullptr);
            };
            copyPlane(y.Get(),slot.yRead.Get(),DXGI_FORMAT_R8_UNORM,width,height,slot.yPitch);
            copyPlane(uv.Get(),slot.uvRead.Get(),DXGI_FORMAT_R8G8_UNORM,width/2,height/2,slot.uvPitch);
        }
        pending=index;
        return true;
    }
    void submittedPending(ID3D12Fence* fence,uint64_t value){
        if(pending<0)return;
        auto& slot=slots[size_t(pending)];pending=-1;
        slot.fence=fence;slot.value=value;slot.state=2;
        {std::lock_guard lock(jobMutex);if(!encoderFinished.load())jobs.push_back(unsigned(&slot-slots.data()));}
        jobCv.notify_one();
        ++frames;
        if(closing.load())closeJobs();
    }
    void discardPending(){
        if(pending<0)return;
        slots[size_t(pending)].state=0;pending=-1;
        if(closing.load())closeJobs();
    }
    void releaseSlot(unsigned index){slots[index].fence.Reset();slots[index].state=0;}

    // ---- encoder thread -------------------------------------------------------
    void publishHeaders(std::vector<uint8_t> bytes,std::wstring name){
        {std::lock_guard lock(textMutex);encoderName=std::move(name);}
        {std::lock_guard lock(outMutex);headers=std::move(bytes);headersReady=true;}
        outCv.notify_all();
        encoderReady=true;
    }
    void pushVideo(std::vector<uint8_t> data,int64_t ptsUs,bool key){
        const size_t size=data.size();
        // Publish the size before the writer can take the packet. Otherwise
        // it can subtract first, underflow size_t and stop a healthy recording.
        {std::lock_guard lock(outMutex);videoPackets.push_back({std::move(data),ptsUs,key});backlogBytes+=size;}
        outCv.notify_one();
    }
    void encoderMain(){
        if(nvenc)encodeNvenc();else encodeMediaFoundation();
        encoderReady=false;closing=true;
        // Failed inputs may still be used by the presentation queue. Leave
        // their fence references for done() to check before releasing memory.
        {std::lock_guard lock(outMutex);encoderDone=true;}
        outCv.notify_all();
        encoderFinished=true;
    }
    void encodeNvenc(){
        sink::NvencLiveEncoder encoder;
        sink::NvencLiveEncoder::Config config;
        config.width=width;config.height=height;config.hevc=hevc;config.hdr=hdrMode!=0;config.fps=fps;config.bitrateMbps=bitrateMbps;
        std::vector<ID3D12Resource*> inputs;for(auto& slot:slots)inputs.push_back(slot.nv12.Get());
        if(!encoder.open(device.Get(),config,inputs)){fail(L"NVENC 无法开始录制："+encoder.lastError());return;}
        publishHeaders(encoder.headers(),encoder.describe());
        veyra::log::info("live-recorder",std::format("encoder ready {} path={}",utf8(encoder.describe()),name()));
        for(;;){
            std::vector<unsigned> batch;bool closed=false;
            {
                std::unique_lock lock(jobMutex);
                if(jobs.empty()&&!jobsClosed&&encoder.inFlight()==0)jobCv.wait(lock,[&]{return !jobs.empty()||jobsClosed;});
                while(!jobs.empty()&&batch.size()+encoder.inFlight()<encoder.capacity()){batch.push_back(jobs.front());jobs.pop_front();}
                closed=jobsClosed&&jobs.empty();
            }
            for(const auto index:batch){
                auto& slot=slots[index];
                if(!encoder.submit(index,slot.fence.Get(),slot.value,slot.ptsUs)){fail(L"NVENC 编码失败："+encoder.lastError());break;}
            }
            if(failed.load())break;
            // Hand finished frames to the writer; block on the oldest only
            // when nothing new can be submitted meanwhile.
            bool blocked=false;
            while(encoder.inFlight()){
                bool more=false;{std::lock_guard lock(jobMutex);more=!jobs.empty();}
                const bool wait=!(more&&encoder.inFlight()<encoder.capacity())&&!blocked;
                sink::NvencLiveEncoder::Packet packet;bool ready=false;
                if(!encoder.takeOldest(packet,wait,kGpuWaitMs,ready)){fail(L"NVENC 编码失败："+encoder.lastError());break;}
                if(!ready)break;
                blocked|=wait;
                releaseSlot(packet.input);
                pushVideo(std::move(packet.data),packet.ptsUs,packet.key);
            }
            if(failed.load())break;
            if(closed&&!encoder.inFlight()){std::lock_guard lock(jobMutex);if(jobs.empty())break;}
        }
        encoder.close();
    }
    void encodeMediaFoundation(){
        const bool com=SUCCEEDED(CoInitializeEx(nullptr,COINIT_MULTITHREADED));
        {
            auto encoder=sink::createMediaFoundationLiveEncoder();
            sink::EncoderConfig config;config.hevc=hevc;config.fpsNum=fps;config.fpsDen=1;config.bitrateMbps=bitrateMbps;
            config.rateControl=sink::ExportRateControl::Vbr;config.adapterVendorId=vendor;config.drainTimeoutMs=kGpuWaitMs;
            auto writer=[this](const uint8_t* bytes,size_t size,int64_t ptsUs,bool key){pushVideo(std::vector<uint8_t>(bytes,bytes+size),ptsUs,key);return true;};
            if(!encoder->openLive(width,height,config,writer)){fail(L"系统硬件编码器无法开始录制："+encoder->liveError());}
            else{
                publishHeaders(encoder->liveHeaders(),encoder->liveDescription());
                veyra::log::info("live-recorder",std::format("encoder ready {} path={}",utf8(encoder->liveDescription()),name()));
                HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
                for(;;){
                    unsigned index=0;
                    {
                        std::unique_lock lock(jobMutex);
                        jobCv.wait(lock,[&]{return !jobs.empty()||jobsClosed;});
                        if(jobs.empty())break;
                        index=jobs.front();jobs.pop_front();
                    }
                    auto& slot=slots[index];
                    if(slot.fence->GetCompletedValue()<slot.value&&(!event||FAILED(slot.fence->SetEventOnCompletion(slot.value,event))||WaitForSingleObject(event,kGpuWaitMs)!=WAIT_OBJECT_0)){
                        fail(L"录制画面回读超时");releaseSlot(index);break;
                    }
                    void* yData=nullptr;void* uvData=nullptr;
                    const D3D12_RANGE yRange{0,size_t(slot.yPitch)*height},uvRange{0,size_t(slot.uvPitch)*(height/2)};
                    bool ok=SUCCEEDED(slot.yRead->Map(0,&yRange,&yData))&&SUCCEEDED(slot.uvRead->Map(0,&uvRange,&uvData));
                    if(ok)ok=encoder->encodeNv12(static_cast<const uint8_t*>(yData),slot.yPitch,static_cast<const uint8_t*>(uvData),slot.uvPitch,slot.ptsUs);
                    const D3D12_RANGE none{0,0};
                    if(yData)slot.yRead->Unmap(0,&none);
                    if(uvData)slot.uvRead->Unmap(0,&none);
                    releaseSlot(index);
                    if(!ok){fail(L"系统硬件编码失败："+encoder->liveError());break;}
                }
                if(event)CloseHandle(event);
                if(!failed.load()&&!encoder->finishLive())fail(L"系统硬件编码器收尾失败："+encoder->liveError());
            }
        }
        if(com)CoUninitialize();
    }

    // ---- sound (audio owner and microphone threads) ----------------------------
    void renderedPcm(const float* pcm,size_t count,const sink::AudioFormat& format,int64_t playoutUs,const void* renderer) override {
        if(!sound||!audio||closing.load()||!format.valid()||!count)return;
        soundLane.push(pcm,count,format,playoutUs,renderer);
    }
    void microphonePcm(const float* stereo,size_t count,int64_t capturedUs) override {
        if(!microphone||!audio||closing.load()||!count)return;
        micLane.push(stereo,count,sink::AudioFormat{},capturedUs,this);
    }

    // ---- writer thread ---------------------------------------------------------
    void releaseMux(){
        if(mux){
            if(mux->pb)avio_closep(&mux->pb);
            avformat_free_context(mux);mux=nullptr;
        }
        for(auto& track:tracks)track.release();
        soundLane.release();micLane.release();
    }
    // The tracks the settings ask for: the played sound, the microphone
    // mixed into it, or the microphone on a second track.
    bool openAudio(){
        trackCount=0;
        if((sound&&!soundLane.open())||(microphone&&!micLane.open()))return false;
        auto add=[&](AudioLane* lane,AudioLane* mix,const char* label){
            auto& track=tracks[trackCount];
            track.lane=lane;track.mix=mix;track.name=label;
            if(!track.open())return false;
            ++trackCount;return true;
        };
        if(sound&&microphone&&!microphoneTrack)return add(&soundLane,&micLane,"Veyra sound + microphone");
        if(sound&&!add(&soundLane,nullptr,"Veyra sound"))return false;
        if(microphone&&!add(&micLane,nullptr,"Veyra microphone"))return false;
        return true;
    }
    void openSound(){
        if(!audio)return;
        if(!openAudio()){
            audio=false;trackCount=0;
            for(auto& track:tracks)track.release();
            setNote(L"没有可用的 AAC 编码器，本次录制不含声音");
            veyra::log::warn("live-recorder","AAC encoder unavailable; recording without sound");
        }
    }
    bool openFile(std::vector<uint8_t> extradata){
        if(avformat_alloc_output_context2(&mux,nullptr,"mp4",nullptr)<0||!mux){fail(L"无法创建录制封装器");return false;}
        videoStream=avformat_new_stream(mux,nullptr);
        if(!videoStream){fail(L"无法创建录制视频流");return false;}
        videoStream->time_base=AVRational{1,1000000};
        videoStream->avg_frame_rate=AVRational{int(fps),1};
        if(!setVideoParameters(videoStream->codecpar,width,height,hdrMode,hevc,extradata)){fail(L"无法写入录制文件头");return false;}
        openSound();
        for(unsigned i=0;i<trackCount;++i){
            auto& track=tracks[i];
            track.stream=avformat_new_stream(mux,nullptr);
            if(!track.stream||avcodec_parameters_copy(track.stream->codecpar,track.parameters.get())<0){fail(L"无法创建录制音频流");return false;}
            track.stream->time_base=AVRational{1,kAudioRate};
            av_dict_set(&track.stream->metadata,"handler_name",track.name.c_str(),0);
        }
        int rc=avio_open(&mux->pb,utf8(path).c_str(),AVIO_FLAG_WRITE);
        if(rc<0){fail(L"无法创建录制文件："+avError(rc));return false;}
        // Fragmented MP4: every fragment is playable on its own, so an
        // interrupted recording keeps what was written.
        AVDictionary* options=nullptr;
        av_dict_set(&options,"movflags","frag_keyframe+empty_moov+default_base_moof",0);
        rc=avformat_write_header(mux,&options);
        av_dict_free(&options);
        if(rc<0){fail(L"无法写入录制文件头："+avError(rc));return false;}
        headerWritten=true;
        veyra::log::info("live-recorder",std::format("file open path={} extent={}x{} codec={} hdr={} fps={} bitrateMbps={} audio={} sound={} microphone={} tracks={}",
            utf8(path),width,height,hevc?"HEVC":"H264",hdrMode,fps,bitrateMbps,audio.load(),sound,microphone?(microphoneTrack?"track":"mixed"):"off",trackCount));
        return true;
    }
    // The instant replay buffer takes what a file would: the encoder's
    // parameter sets and the sound tracks' decoder configuration.
    bool openReplay(std::vector<uint8_t> extradata){
        openSound();
        ReplayBuffer::VideoFormat format;
        format.width=width;format.height=height;format.hdrMode=hdrMode;format.fps=fps;format.hevc=hevc;format.extradata=std::move(extradata);
        std::vector<ReplayBuffer::AudioFormat> sounds;
        for(unsigned i=0;i<trackCount;++i)sounds.push_back({tracks[i].parameters,tracks[i].name});
        replay->setFormat(std::move(format),std::move(sounds));
        veyra::log::info("live-recorder",std::format("replay buffer open extent={}x{} codec={} hdr={} fps={} bitrateMbps={} keepSeconds={:.0f} sound={} microphone={} tracks={}",
            width,height,hevc?"HEVC":"H264",hdrMode,fps,bitrateMbps,double(replay->keepUs())/1e6,sound,microphone?(microphoneTrack?"track":"mixed"):"off",trackCount));
        return true;
    }
    bool writeVideo(const VideoPacket& packet,int64_t durationUs){
        AVPacket* out=av_packet_alloc();
        if(!out||av_new_packet(out,int(packet.data.size()))<0){av_packet_free(&out);return false;}
        std::memcpy(out->data,packet.data.data(),packet.data.size());
        out->stream_index=videoStream->index;out->pts=out->dts=packet.ptsUs;out->duration=std::max<int64_t>(1,durationUs);
        if(packet.key)out->flags|=AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(out,AVRational{1,1000000},videoStream->time_base);
        if(out->dts<=lastVideoDts)out->pts=out->dts=lastVideoDts+1;
        lastVideoDts=out->dts;
        const int rc=av_interleaved_write_frame(mux,out);
        av_packet_free(&out);
        if(rc<0){veyra::log::error("live-recorder",std::format("video write failed rc={}",rc));return false;}
        ++videoWritten;writtenUs=packet.ptsUs;
        return true;
    }
    bool processAudio(bool final,int64_t endTimeUs){
        const int64_t t0=t0Us.load();
        if(sound&&!soundLane.process(final,endTimeUs,t0))return false;
        if(microphone&&!micLane.process(final,endTimeUs,t0))return false;
        if(!t0)return true;
        const int64_t limit=final?std::max<int64_t>(endTimeUs*kAudioRate/1000000,0):INT64_MAX;
        for(unsigned i=0;i<trackCount;++i){
            auto& track=tracks[i];
            const bool ok=track.encode(final,limit,[&](AVPacket* packet){
                if(replay){
                    replay->pushAudio(i,{std::make_shared<const std::vector<uint8_t>>(packet->data,packet->data+packet->size),packet->pts,packet->duration,false});
                    return true;
                }
                packet->stream_index=track.stream->index;
                av_packet_rescale_ts(packet,AVRational{1,kAudioRate},track.stream->time_base);
                return av_interleaved_write_frame(mux,packet)>=0;
            });
            if(!ok)return false;
        }
        return true;
    }
    void writerMain(){
        std::vector<uint8_t> extradata;
        {
            std::unique_lock lock(outMutex);
            outCv.wait(lock,[&]{return headersReady||encoderDone;});
            if(headersReady)extradata=headers;
        }
        bool ok=!extradata.empty()&&(replay?openReplay(std::move(extradata)):openFile(std::move(extradata)));
        std::optional<VideoPacket> held;
        const int64_t frameUs=1000000/std::max(1u,fps);
        while(ok){
            std::deque<VideoPacket> batch;bool ended=false;
            {
                std::unique_lock lock(outMutex);
                outCv.wait_for(lock,std::chrono::milliseconds(20),[&]{return !videoPackets.empty()||encoderDone;});
                batch.swap(videoPackets);
                ended=encoderDone;
            }
            for(auto& packet:batch){
                backlogBytes-=packet.data.size();
                if(replay){
                    if(!originSet){replay->setOrigin(t0Us.load());originSet=true;}
                    ++videoWritten;writtenUs=packet.ptsUs;
                    replay->pushVideo({std::make_shared<const std::vector<uint8_t>>(std::move(packet.data)),packet.ptsUs,0,packet.key});
                    continue;
                }
                if(ok&&held)ok=writeVideo(*held,packet.ptsUs-held->ptsUs);
                held=std::move(packet);
            }
            if(ok&&audio)ok=processAudio(false,0);
            if(!ok){fail(replay?L"即时回放写入失败":L"写入录制文件失败（磁盘已满或没有写入权限？）");break;}
            if(ended)break;
        }
        // Drop what can no longer be written.
        {std::lock_guard lock(outMutex);for(auto& packet:videoPackets)backlogBytes-=packet.data.size();videoPackets.clear();}
        if(replay){
            int64_t end=endUs.load();
            if(videoWritten.load())end=std::max(end,writtenUs.load()+frameUs);
            if(ok&&audio&&videoWritten.load())ok=processAudio(true,end);
            replay->finish(std::max<int64_t>(end,0));
            veyra::log::info("live-recorder",std::format("replay buffer closed videoFrames={} seconds={:.2f} audioGaps={} audioOverflow={} microphoneGaps={} microphoneOverflow={} dropped={} ok={}",
                videoWritten.load(),double(std::max<int64_t>(end,0))/1e6,soundLane.gaps.load(),soundLane.overflow.load(),micLane.gaps.load(),micLane.overflow.load(),dropped.load(),ok));
        }else if(headerWritten){
            int64_t end=endUs.load();
            if(held)end=std::max(end,held->ptsUs+frameUs);
            if(ok&&held)ok=writeVideo(*held,end-held->ptsUs);
            if(ok&&audio&&videoWritten.load())ok=processAudio(true,end);
            const int rc=av_write_trailer(mux);
            if(rc<0)ok=false;
            if(!ok&&!failed.load())fail(L"录制文件收尾失败");
            avio_closep(&mux->pb);
            if(!videoWritten.load()){
                std::error_code code;std::filesystem::remove(path,code);
                setNote(L"没有录到画面，未保存文件");
            }
            veyra::log::info("live-recorder",std::format("file closed path={} videoFrames={} seconds={:.2f} originUs={} audio={} audioGaps={} audioOverflow={} microphoneGaps={} microphoneOverflow={} dropped={} ok={}",
                utf8(path),videoWritten.load(),double(end)/1e6,t0Us.load(),audio.load(),soundLane.gaps.load(),soundLane.overflow.load(),micLane.gaps.load(),micLane.overflow.load(),dropped.load(),ok));
        }
        releaseMux();
        writerFinished=true;
    }
};

// A replay save in progress: its own thread, so writing a long clip never
// holds up the buffer. Shared with that thread, which may outlive the
// recorder at exit (waitIdle detaches a stuck one).
struct ReplaySave {
    std::mutex m;
    bool done=false,ok=false;
    std::wstring path,error;
    double seconds=0;
    std::thread thread;
};

struct LiveRecorder::State {
    mutable std::mutex m;
    bool replayMode=false;
    RecordingSettings settings;
    bool armed=false;
    std::shared_ptr<Segment> current,pendingCapture;
    std::vector<std::shared_ptr<Segment>> finishing;
    std::wstring baseName;
    unsigned nextNumber=1;
    int64_t firstFrameUs=0,nextDueUs=0;
    double lastSeconds=0;
    std::atomic<double> presentFps{0};
    bool failed=false;
    std::wstring path,error,encoder,note;
    uint64_t framesBase=0,droppedBase=0;
    uint32_t files=0;
    uint64_t started=0,saved=0;
    bool audioTrack=false;
    // Instant replay recorder.
    std::shared_ptr<ReplayBuffer> frozen;   // the last stretch before a picture change or a new session
    std::shared_ptr<ReplaySave> save;
    int64_t retryAfterUs=0;

    void retire(const std::shared_ptr<Segment>& segment,const std::wstring& why){
        if(!segment)return;
        sink::removeRecordingAudioSink(segment.get());
        detachMicrophone(segment.get());
        if(!why.empty()&&segment->noteText().empty())segment->setNote(why);
        if(const auto name=segment->encoderText();!name.empty())encoder=name;
        segment->finish(sink::recordingClockUs());
        framesBase+=segment->frames.load();droppedBase+=segment->dropped.load();
        // What it buffered stays saveable until the next stretch is as long.
        if(segment->replay&&segment->replay->coverage().picture)frozen=segment->replay;
        finishing.push_back(segment);
    }
    void reap(){
        if(current&&current->failed.load()){
            failed=true;error=current->errorText();
            if(replayMode){
                // The buffer stays armed; the next picture tries again a little later.
                retryAfterUs=sink::recordingClockUs()+kReplayRetryUs;
                veyra::log::warn("live-recorder",std::format("replay buffer stopped on error, retrying in 5 s: {}",utf8(error)));
            }else{
                armed=false;
                if(firstFrameUs)lastSeconds=double(sink::recordingClockUs()-firstFrameUs)/1e6;
                veyra::log::warn("live-recorder",std::format("recording stopped on error: {}",utf8(error)));
            }
            retire(current,{});current.reset();
        }
        for(auto it=finishing.begin();it!=finishing.end();){
            auto& segment=*it;
            if(!segment->done()){++it;continue;}
            segment->join();
            if(!replayMode&&segment->videoWritten.load()){path=segment->path;++saved;}
            if(segment->failed.load()&&error.empty()){failed=true;error=segment->errorText();}
            if(const auto text=segment->noteText();!text.empty())note=text;
            it=finishing.erase(it);
        }
        if(frozen&&current&&current->replay&&current->replay->coverage().seconds*1e6>=double(current->replay->keepUs())-1e5)frozen.reset();
        if(save){
            std::unique_lock lock(save->m);
            if(save->done){
                if(save->ok){path=save->path;++saved;failed=false;error.clear();note.clear();}
                else{failed=true;error=save->error;}
                lock.unlock();
                if(save->thread.joinable())save->thread.join();
                save.reset();
            }
        }
    }
    std::shared_ptr<ReplayBuffer> replayToSave() const {
        const auto ownBuffer=current&&current->replay?current->replay:nullptr;
        if(ownBuffer&&ownBuffer->coverage().seconds>=1.0)return ownBuffer;
        if(frozen&&frozen->coverage().picture)return frozen;
        if(ownBuffer&&ownBuffer->coverage().picture)return ownBuffer;
        return nullptr;
    }
};

LiveRecorder::LiveRecorder(bool replay):s_(std::make_unique<State>()){s_->replayMode=replay;}
LiveRecorder::~LiveRecorder(){waitIdle(8000);}

bool LiveRecorder::start(const RecordingSettings& settings,std::wstring& error){
    error.clear();
    auto& s=*s_;
    std::lock_guard lock(s.m);
    s.reap();
    if(s.replayMode){
        // Armed again with other settings: the next picture starts a new
        // stretch with them; the current one stays saveable meanwhile.
        const auto& o=s.settings;
        const bool same=s.armed&&o.bitrateMbps==settings.bitrateMbps&&o.hevc==settings.hevc&&o.fpsLimit==settings.fpsLimit&&
            o.audio==settings.audio&&o.microphone==settings.microphone&&o.microphoneId==settings.microphoneId&&
            o.microphoneTrack==settings.microphoneTrack&&o.replaySeconds==settings.replaySeconds&&o.replayMemoryBytes==settings.replayMemoryBytes;
        s.settings=settings;
        if(s.settings.replaySeconds==0)s.settings.replaySeconds=60;
        if(same)return true;
        if(s.current){s.retire(s.current,{});s.current.reset();}
        if(!s.armed){s.failed=false;s.error.clear();s.note.clear();s.framesBase=s.droppedBase=0;++s.started;}
        s.armed=true;s.nextDueUs=0;s.retryAfterUs=0;
        veyra::log::info("live-recorder",std::format("replay buffer armed keepSeconds={} memoryCapMiB={} bitrateMbps={} hevc={} fpsLimit={} audio={} microphone={}",
            s.settings.replaySeconds,(s.settings.replayMemoryBytes?s.settings.replayMemoryBytes:defaultReplayMemory())>>20,settings.bitrateMbps,settings.hevc,settings.fpsLimit,settings.audio,
            settings.microphone?(settings.microphoneTrack?"track":"mixed"):"off"));
        return true;
    }
    if(s.armed){error=L"录制已在进行中";return false;}
    if(!s.finishing.empty()){error=L"上一段录制仍在保存，请稍后再试";return false;}
    if(settings.directory.empty()){error=L"未设置录制保存位置";return false;}
    std::error_code code;
    std::filesystem::create_directories(settings.directory,code);
    if(!std::filesystem::is_directory(settings.directory,code)){error=L"无法创建录制目录："+settings.directory;return false;}
    ULARGE_INTEGER available{};
    if(GetDiskFreeSpaceExW(settings.directory.c_str(),&available,nullptr,nullptr)&&available.QuadPart<(uint64_t(1)<<30)){
        error=L"录制目录所在磁盘剩余空间不足 1 GB";return false;
    }
    s.settings=settings;s.armed=true;s.failed=false;s.error.clear();s.note.clear();s.encoder.clear();
    s.framesBase=s.droppedBase=0;s.files=0;s.firstFrameUs=0;s.nextDueUs=0;s.lastSeconds=0;s.audioTrack=false;
    s.baseName=L"Veyra-"+fileStamp();s.nextNumber=1;++s.started;
    veyra::log::info("live-recorder",std::format("armed directory={} bitrateMbps={} hevc={} fpsLimit={} audio={} microphone={}",
        utf8(settings.directory),settings.bitrateMbps,settings.hevc,settings.fpsLimit,settings.audio,
        settings.microphone?(settings.microphoneTrack?"track":"mixed"):"off"));
    return true;
}

void LiveRecorder::stop(const std::wstring& note){
    auto& s=*s_;
    std::lock_guard lock(s.m);
    s.reap();
    if(!s.armed)return;
    s.armed=false;
    if(s.firstFrameUs)s.lastSeconds=double(sink::recordingClockUs()-s.firstFrameUs)/1e6;
    if(s.current){s.retire(s.current,note);s.current.reset();}
    else if(!note.empty())s.note=note;
    // A replay buffer turned off lets go of its memory (a save in progress keeps its clip).
    if(s.replayMode)s.frozen.reset();
    veyra::log::info("live-recorder",std::format("{} requested note={}",s.replayMode?"replay buffer stop":"stop",utf8(note)));
}

bool LiveRecorder::active() const{std::lock_guard lock(s_->m);return s_->armed;}

RecordingStatus LiveRecorder::status() const{
    auto& s=*s_;
    std::lock_guard lock(s.m);
    s.reap();
    RecordingStatus out;
    out.active=s.armed;out.failed=s.failed;out.finishing=!s.finishing.empty();
    out.error=s.error;out.note=s.note;out.files=s.files;out.started=s.started;out.saved=s.saved;
    out.frames=s.framesBase;out.dropped=s.droppedBase;out.path=s.path;out.encoder=s.encoder;out.audio=s.audioTrack;
    if(s.current){
        if(!s.replayMode)out.path=s.current->path;
        out.frames+=s.current->frames.load();out.dropped+=s.current->dropped.load();
        if(const auto name=s.current->encoderText();!name.empty())out.encoder=name;
        out.audio=s.current->audio;
        out.microphone=s.current->microphone&&s.current->audio;
        if(const auto text=s.current->noteText();!text.empty())out.note=text;
    }
    if(out.microphone){
        const auto mic=microphoneStatus();
        if(!mic.open)out.microphoneError=mic.error;
        out.microphoneName=mic.device;
    }
    if(s.replayMode){
        out.saving=s.save!=nullptr;
        if(const auto buffer=s.replayToSave()){
            const auto coverage=buffer->coverage();
            out.bufferedSeconds=coverage.seconds;out.memoryLimited=coverage.limited;
        }
        for(const auto& buffer:{s.current&&s.current->replay?s.current->replay:nullptr,s.frozen})
            if(buffer)out.bufferedBytes+=buffer->coverage().bytes;
        out.seconds=out.bufferedSeconds;
        out.finishing=out.saving;
        return out;
    }
    out.seconds=s.armed?(s.firstFrameUs?double(sink::recordingClockUs()-s.firstFrameUs)/1e6:0.0):s.lastSeconds;
    return out;
}

bool LiveRecorder::saveReplay(const std::wstring& directory,std::wstring& error){
    error.clear();
    auto& s=*s_;
    std::lock_guard lock(s.m);
    s.reap();
    if(!s.replayMode||!s.armed){error=L"即时回放未开启";return false;}
    if(s.save){error=L"上一段回放仍在保存，请稍后再试";return false;}
    const auto buffer=s.replayToSave();
    if(!buffer){error=L"回放里还没有画面";return false;}
    if(directory.empty()){error=L"未设置录制保存位置";return false;}
    std::error_code code;
    std::filesystem::create_directories(directory,code);
    if(!std::filesystem::is_directory(directory,code)){error=L"无法创建录制目录："+directory;return false;}
    ULARGE_INTEGER available{};
    if(GetDiskFreeSpaceExW(directory.c_str(),&available,nullptr,nullptr)&&available.QuadPart<buffer->coverage().bytes+(uint64_t(256)<<20)){
        error=L"录制目录所在磁盘剩余空间不足，无法保存回放";return false;
    }
    auto job=std::make_shared<ReplaySave>();
    job->path=uniqueFile(directory,L"Veyra-Replay-"+fileStamp());
    const int64_t request=sink::recordingClockUs();
    veyra::log::info("live-recorder",std::format("replay save requested path={} buffered={:.2f}s bytes={}",utf8(job->path),buffer->coverage().seconds,buffer->coverage().bytes));
    try{
        job->thread=std::thread([job,buffer,request]{
            // The sound reaches the buffer a little after the picture (the
            // endpoint latency): wait briefly so both end at the request.
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(kReplaySaveWaitMs);
            while(!buffer->covers(request)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const auto clip=buffer->clip(request);
            std::wstring reason;
            const auto began=std::chrono::steady_clock::now();
            const bool ok=writeReplayFile(job->path,clip,reason);
            const double seconds=double(clip.endUs-clip.startUs)/1e6;
            size_t sounds=0;for(const auto& track:clip.audioPackets)sounds+=track.size();
            veyra::log::info("live-recorder",std::format("replay saved ok={} path={} seconds={:.3f} originUs={} clipStartUs={} videoPackets={} audioPackets={} tracks={} bytes={} writeMs={} error={}",
                ok,utf8(job->path),seconds,clip.originUs,clip.startUs,clip.videoPackets.size(),sounds,clip.audio.size(),clip.bytes,
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began).count(),utf8(reason)));
            std::lock_guard lock(job->m);
            job->ok=ok;job->error=reason;job->seconds=seconds;job->done=true;
        });
    }catch(...){error=L"无法开始保存回放";return false;}
    s.save=std::move(job);
    return true;
}

void LiveRecorder::setPresentRateHint(double fps){if(std::isfinite(fps)&&fps>0)s_->presentFps=fps;}

void LiveRecorder::sessionEnded(const std::wstring& note){
    auto& s=*s_;
    if(!s.replayMode){stop(note);return;}
    // The buffer belongs to the device that ends; the next session starts a
    // new stretch, and this one stays saveable until then.
    std::lock_guard lock(s.m);
    s.reap();
    if(s.current){s.retire(s.current,{});s.current.reset();}
}

void LiveRecorder::waitIdle(uint32_t timeoutMs){
    stop();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);
    for(;;){
        {
            std::lock_guard lock(s_->m);
            s_->reap();
            if(s_->finishing.empty()&&!s_->save)return;
            if(std::chrono::steady_clock::now()>=deadline){
                // A stuck encoder must not hang the exit: leave its threads running.
                for(auto& segment:s_->finishing){
                    veyra::log::error("live-recorder",std::format("segment not finished at exit path={}",segment->name()));
                    if(segment->encoderThread.joinable())segment->encoderThread.detach();
                    if(segment->writerThread.joinable())segment->writerThread.detach();
                    // Intentionally process-lifetime storage: a static vector
                    // destructor could free a Segment under its detached worker.
                    static auto* abandoned=new std::vector<std::shared_ptr<Segment>>;
                    abandoned->push_back(segment);
                }
                s_->finishing.clear();
                if(s_->save){
                    // The save keeps its own state and buffer alive.
                    veyra::log::error("live-recorder","replay save not finished at exit");
                    if(s_->save->thread.joinable())s_->save->thread.detach();
                    s_->save.reset();
                }
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool LiveRecorder::record(ID3D12GraphicsCommandList* list,const Frame& frame){
    auto& s=*s_;
    std::lock_guard lock(s.m);
    if(!s.armed||!list||!frame.texture)return false;
    if(s.pendingCapture){s.pendingCapture->discardPending();s.pendingCapture.reset();}
    if(s.current&&s.current->failed.load())s.reap();
    if(!s.armed)return false;
    const int64_t now=sink::recordingClockUs();
    if(s.replayMode&&!s.current&&now<s.retryAfterUs)return false;
    if(s.settings.fpsLimit){
        const int64_t interval=1000000/int64_t(s.settings.fpsLimit);
        if(s.nextDueUs&&now+interval/4<s.nextDueUs)return false;
        s.nextDueUs=(s.nextDueUs&&now-s.nextDueUs<interval)?s.nextDueUs+interval:now+interval;
    }
    if(s.current&&!s.current->matches(frame)){
        s.reap();
        if(!s.replayMode&&s.finishing.size()>=2){
            s.armed=false;s.failed=true;s.error=L"录制文件仍在保存，已停止录制以避免积压";
            s.retire(s.current,{});s.current.reset();return false;
        }
        veyra::log::info("live-recorder",std::format("picture changed {}x{} hdr={} -> {}x{} hdr={}; continuing in a new {}",
            s.current->width,s.current->height,s.current->hdrMode,frame.width,frame.height,frame.hdrMode,s.replayMode?"replay buffer":"file"));
        s.retire(s.current,s.replayMode?std::wstring{}:L"画面尺寸或 HDR 模式变化，已接着写入新文件");s.current.reset();
    }
    if(!s.current){
        // Rapid source/settings changes must not create an unbounded number
        // of encoder workers while the previous segments are draining.
        s.reap();
        if(s.replayMode&&s.finishing.size()>=2)return false;
        auto fail=[&](const std::wstring& reason){
            s.failed=true;s.error=reason;
            if(s.replayMode)s.retryAfterUs=now+kReplayRetryUs;else s.armed=false;
            veyra::log::error("live-recorder",std::format("cannot {}: {}",s.replayMode?"buffer replay":"record",utf8(reason)));
            return false;
        };
        auto segment=std::make_shared<Segment>();
        if(FAILED(frame.texture->GetDevice(IID_PPV_ARGS(&segment->device))))return fail(L"无法获取当前显卡");
        segment->vendor=adapterVendor(segment->device.Get());
        segment->nvenc=segment->vendor==0x10DE&&GetEnvironmentVariableW(L"VEYRA_TEST_FORCE_MF_ENCODER",nullptr,0)==0;
        if(frame.hdrMode&&!segment->nvenc)return fail(L"HDR 画面录制需要 NVIDIA 显卡（HEVC 10bit）；请关闭 HDR 输出后再录制");
        if(frame.width<16||frame.height<16||(frame.width&1)||(frame.height&1))
            return fail(std::format(L"当前画面尺寸 {}×{} 无法录制",frame.width,frame.height));
        segment->width=frame.width;segment->height=frame.height;segment->hdrMode=frame.hdrMode;
        // H.264 hardware encoders stop at 4096 pixels per side.
        segment->hevc=s.settings.hevc||frame.hdrMode!=0||frame.width>4096||frame.height>4096;
        const double hint=s.presentFps.load();
        segment->fps=s.settings.fpsLimit?s.settings.fpsLimit:uint32_t(std::clamp<long>(std::lround(hint>1?hint:60.0),10,480));
        segment->bitrateMbps=s.settings.bitrateMbps?s.settings.bitrateMbps:autoBitrateMbps(frame.width,frame.height,segment->fps,segment->hevc);
        segment->sound=s.settings.audio;
        segment->microphone=s.settings.microphone;
        segment->microphoneTrack=s.settings.microphoneTrack;
        segment->audio=segment->sound||segment->microphone;
        if(s.replayMode){
            const uint64_t cap=s.settings.replayMemoryBytes?s.settings.replayMemoryBytes:defaultReplayMemory();
            segment->replay=std::make_shared<ReplayBuffer>(int64_t(s.settings.replaySeconds)*1000000,size_t(cap));
        }else{
            const auto base=s.baseName+(s.nextNumber>1?std::format(L"-{}",s.nextNumber):L"");
            segment->path=uniqueFile(s.settings.directory,base);
        }
        std::wstring reason;
        if(!segment->create(reason))return fail(reason);
        s.current=segment;++s.files;++s.nextNumber;s.audioTrack=segment->audio;
        if(segment->sound)sink::addRecordingAudioSink(segment);
        if(segment->microphone)attachMicrophone(segment,s.settings.microphoneId);
        veyra::log::info("live-recorder",std::format("{} {} path={} extent={}x{} hdr={} codec={} fps={} bitrateMbps={} encoder={} audio={} microphone={}",
            s.replayMode?"replay buffer":"file",s.files,segment->name(),segment->width,segment->height,segment->hdrMode,segment->hevc?"HEVC":"H264",segment->fps,segment->bitrateMbps,
            segment->nvenc?"NVENC":"MediaFoundation",segment->sound,segment->microphone?(segment->microphoneTrack?"track":"mixed"):"off"));
    }
    if(!s.current->capture(list,frame,now))return false;
    if(!s.firstFrameUs)s.firstFrameUs=now;
    s.pendingCapture=s.current;
    return true;
}

void LiveRecorder::submitted(ID3D12Fence* fence,uint64_t value){
    std::lock_guard lock(s_->m);
    if(s_->pendingCapture){s_->pendingCapture->submittedPending(fence,value);s_->pendingCapture.reset();}
}

void LiveRecorder::discarded(){
    std::lock_guard lock(s_->m);
    if(s_->pendingCapture){s_->pendingCapture->discardPending();s_->pendingCapture.reset();}
}

} // namespace veyra::engine
