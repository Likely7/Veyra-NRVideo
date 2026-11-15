// Instant replay buffer (#103): GOP trimming, the memory cap, clip selection,
// sound alongside the picture, coverage and the end of a segment; the
// recording audio tap with two listeners (a recording and the replay buffer).
#include "veyra/engine/ReplayBuffer.h"
#include "veyra/sink/RecordingAudioTap.h"

#include <cstdio>
#include <memory>
#include <vector>

using veyra::engine::ReplayBuffer;
namespace {
int failures=0,passes=0;
void check(bool ok,const char* what){std::printf("%s %s\n",ok?"PASS":"FAIL",what);ok?++passes:++failures;}
std::shared_ptr<const std::vector<uint8_t>> bytes(size_t n){return std::make_shared<const std::vector<uint8_t>>(n,uint8_t(0x42));}
constexpr int64_t kFrameUs=16667;          // 60 fps
constexpr int kGop=120;                    // 2 s
ReplayBuffer::Packet video(int frame,size_t size=1000){return {bytes(size),int64_t(frame)*kFrameUs,0,frame%kGop==0};}
ReplayBuffer::Packet sound(int64_t pts){return {bytes(400),pts,1024,false};}
// Feeds `seconds` of 60 fps video, and AAC frames up to the same time
// (priming first, as the encoder delivers them).
void feed(ReplayBuffer& buffer,int frames,int firstFrame=0,size_t size=1000,int64_t* audioPts=nullptr){
    int64_t local=-1024;int64_t& pts=audioPts?*audioPts:local;
    for(int f=firstFrame;f<firstFrame+frames;++f){
        buffer.pushVideo(video(f,size));
        const int64_t until=int64_t(f+1)*kFrameUs*48000/1000000;
        while(pts+1024<=until){buffer.pushAudio(0,sound(pts));pts+=1024;}
    }
}
ReplayBuffer::VideoFormat format(){ReplayBuffer::VideoFormat f;f.width=1920;f.height=1080;f.fps=60;f.extradata={0,0,0,1};return f;}
struct Collect final : veyra::sink::RecordingAudioSink {
    size_t frames=0;int calls=0;
    void renderedPcm(const float*,size_t n,const veyra::sink::AudioFormat&,int64_t,const void*) override {frames+=n;++calls;}
};
}

int main(){
    {
        ReplayBuffer empty(10'000'000,size_t(64)<<20);
        empty.setFormat(format(),{{nullptr,"Veyra sound"}});
        check(!empty.coverage().picture&&empty.coverage().seconds==0,"empty buffer holds nothing");
        empty.pushVideo({bytes(100),0,0,false});
        check(!empty.coverage().picture,"pictures before the first keyframe are not kept");
        check(empty.clip(1'000'000).videoPackets.empty(),"empty buffer gives an empty clip");
    }
    {
        // 30 s at 60 fps into a 10 s buffer.
        ReplayBuffer buffer(10'000'000,size_t(64)<<20);
        buffer.setFormat(format(),{{nullptr,"Veyra sound"}});
        buffer.setOrigin(5'000'000);
        int64_t audioPts=-1024;
        feed(buffer,1800,0,1000,&audioPts);
        const auto coverage=buffer.coverage();
        check(coverage.picture&&coverage.seconds==10.0,"full buffer reports the chosen length");
        check(!coverage.limited,"length, not memory, trims a buffer under its cap");
        // Kept: the chosen 10 s plus less than one GOP.
        const size_t kept=coverage.bytes;
        check(kept>=600*1000&&kept<=(720+1)*1000+size_t(48000*12/1024+8)*400,"trimmed to the length plus at most one GOP");
        const int64_t newest=1799*kFrameUs;
        const auto clip=buffer.clip(5'000'000+newest+kFrameUs);
        check(!clip.videoPackets.empty()&&clip.videoPackets.front().key,"clip starts on a keyframe");
        const double length=double(clip.endUs-clip.startUs)/1e6;
        check(length>=10.0&&length<12.0,"clip holds the chosen length and less than one more GOP");
        check(clip.startUs%(int64_t(kGop)*kFrameUs)==0&&clip.startUs<=clip.endUs-10'000'000,"clip starts at the latest keyframe early enough");
        check(clip.videoPackets.back().pts<clip.endUs,"clip ends at the request");
        bool ordered=true;for(size_t i=1;i<clip.videoPackets.size();++i)ordered&=clip.videoPackets[i].pts>clip.videoPackets[i-1].pts;
        check(ordered,"clip pictures are in order");
        const auto& audio=clip.audioPackets.at(0);
        const int64_t startSamples=clip.startUs*48000/1000000;
        check(!audio.empty()&&audio.front().pts<=startSamples-1024&&audio.front().pts>=startSamples-2048-1024,
              "clip sound starts two frames before the picture (decoder priming)");
        check(audio.back().pts<clip.endUs*48000/1000000,"clip sound ends at the request");
        // A request a second back keeps the start rule.
        const int64_t back=newest-1'000'000;
        const auto earlier=buffer.clip(5'000'000+back);
        check(earlier.videoPackets.front().key&&earlier.endUs-earlier.startUs>=10'000'000-kFrameUs&&earlier.videoPackets.back().pts<back,
              "an earlier request ends there and starts on the keyframe before");
        // Coverage of a request: the picture must reach it, and the sound too.
        check(buffer.covers(5'000'000+newest),"covers a request the picture and sound reached");
        check(!buffer.covers(5'000'000+newest+500'000),"does not cover a request half a second ahead");
        for(int f=1800;f<1830;++f)buffer.pushVideo(video(f));
        check(!buffer.covers(5'000'000+1829*kFrameUs),"a sound track behind the picture holds the request back");
        while(audioPts+1024<=int64_t(1830)*kFrameUs*48000/1000000){buffer.pushAudio(0,sound(audioPts));audioPts+=1024;}
        check(buffer.covers(5'000'000+1829*kFrameUs),"the request is covered once the sound arrives");
        // The end of the segment.
        buffer.finish(1830*kFrameUs);
        check(buffer.finished()&&buffer.covers(INT64_MAX/2),"a finished buffer covers any request");
        const size_t before=buffer.coverage().bytes;
        buffer.pushVideo(video(1830));
        check(buffer.coverage().bytes==before,"nothing is added after the end");
        const auto last=buffer.clip(INT64_MAX/2);
        check(last.endUs==1830*kFrameUs,"a finished buffer's clip ends where the segment ended");
        const int64_t requested=30'200'000;
        const auto requestedClip=buffer.clip(5'000'000+requested);
        check(requestedClip.endUs<=requested+kFrameUs&&requestedClip.videoPackets.back().pts<requested,
              "a segment ending during a save never extends the requested clip into the future");
        check(buffer.clip(5'000'000).videoPackets.empty(),"an expired request never returns a later picture");
    }
    {
        // A memory cap of about 4 s at this bitrate: the cap trims, the length does not.
        const size_t perFrame=4000;
        ReplayBuffer capped(30'000'000,size_t(perFrame)*240+size_t(perFrame)*10);
        capped.setFormat(format(),{});
        feed(capped,3600,0,perFrame);
        const auto coverage=capped.coverage();
        check(coverage.limited,"the memory cap is reported");
        check(coverage.seconds<30.0&&coverage.seconds>=2.0,"a capped buffer keeps less than the chosen length");
        check(coverage.bytes<=size_t(perFrame)*250,"a capped buffer stays at its cap");
        const auto clip=capped.clip(INT64_MAX/4);
        check(clip.videoPackets.front().key,"a capped clip still starts on a keyframe");
    }
    {
        // Short buffer: less than the chosen length available, the clip takes all of it.
        ReplayBuffer young(60'000'000,size_t(64)<<20);
        young.setFormat(format(),{});
        feed(young,300);
        const auto clip=young.clip(INT64_MAX/4);
        check(young.coverage().seconds>4.9&&young.coverage().seconds<5.1,"a young buffer reports what it has");
        check(clip.startUs==0&&clip.videoPackets.size()==300,"a young buffer's clip starts at its first frame");
    }
    {
        ReplayBuffer audioHeavy(60'000'000,8192);
        audioHeavy.setFormat(format(),{{nullptr,"Veyra sound"}});
        audioHeavy.pushVideo(video(0));
        for(int i=0;i<100;++i)audioHeavy.pushAudio(0,sound(i*1024));
        check(audioHeavy.coverage().bytes<=8192&&audioHeavy.coverage().limited,
              "audio without new pictures cannot grow beyond the replay memory cap");
        ReplayBuffer oversized(60'000'000,8192);
        oversized.setFormat(format(),{});
        oversized.pushVideo(video(0,9000));
        check(oversized.coverage().bytes<=8192&&!oversized.coverage().picture,
              "a single GOP larger than the cap is dropped whole");
        oversized.pushVideo(video(1));
        check(!oversized.coverage().picture,"an oversized GOP is not resumed on a dependent frame");
        oversized.pushVideo(video(120));
        check(oversized.coverage().picture&&oversized.clip(INT64_MAX/4).videoPackets.front().key,
              "buffering recovers at the next keyframe after a capacity drop");
    }
    {
        // Two listeners on the rendered-sound tap; removing one leaves the other.
        auto first=std::make_shared<Collect>(),second=std::make_shared<Collect>();
        check(!veyra::sink::recordingAudioActive()&&!veyra::sink::recordingAudioSink(),"tap is idle without listeners");
        veyra::sink::addRecordingAudioSink(first);veyra::sink::addRecordingAudioSink(second);veyra::sink::addRecordingAudioSink(first);
        const float pcm[4]{};
        veyra::sink::recordingAudioSink()->renderedPcm(pcm,2,{},0,nullptr);
        check(first->calls==1&&second->calls==1,"both listeners get each block once");
        veyra::sink::removeRecordingAudioSink(first.get());
        veyra::sink::recordingAudioSink()->renderedPcm(pcm,2,{},0,nullptr);
        check(first->calls==1&&second->calls==2&&veyra::sink::recordingAudioActive(),"a removed listener gets nothing more");
        veyra::sink::removeRecordingAudioSink(second.get());
        check(!veyra::sink::recordingAudioActive()&&!veyra::sink::recordingAudioSink(),"tap is idle again");
    }
    std::printf("Totals: %d passed, %d failed\n",passes,failures);
    return failures?1:0;
}
