#include "veyra/sink/RecordingAudioTap.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

namespace veyra::sink {
namespace {
std::mutex gTapMutex;
std::vector<std::shared_ptr<RecordingAudioSink>> gSinks;
std::atomic<bool> gTapActive{false};
// Each sink only copies (RecordingAudioSink contract), so the lock is held
// for a few copies; removal waits for a block in flight and then no further
// call reaches the sink.
struct Forwarder final : RecordingAudioSink {
    void renderedPcm(const float* pcm,size_t frames,const AudioFormat& format,int64_t playoutUs,const void* renderer) override {
        std::lock_guard lock(gTapMutex);
        for(const auto& sink:gSinks)sink->renderedPcm(pcm,frames,format,playoutUs,renderer);
    }
};
const std::shared_ptr<RecordingAudioSink>& forwarder(){static const std::shared_ptr<RecordingAudioSink> f=std::make_shared<Forwarder>();return f;}
}

void addRecordingAudioSink(std::shared_ptr<RecordingAudioSink> sink){
    if(!sink)return;
    std::lock_guard lock(gTapMutex);
    if(std::find(gSinks.begin(),gSinks.end(),sink)==gSinks.end())gSinks.push_back(std::move(sink));
    gTapActive.store(true,std::memory_order_release);
}

void removeRecordingAudioSink(const RecordingAudioSink* sink){
    std::lock_guard lock(gTapMutex);
    std::erase_if(gSinks,[sink](const auto& s){return s.get()==sink;});
    gTapActive.store(!gSinks.empty(),std::memory_order_release);
}

bool recordingAudioActive(){return gTapActive.load(std::memory_order_acquire);}

std::shared_ptr<RecordingAudioSink> recordingAudioSink(){
    const auto& f=forwarder();
    std::lock_guard lock(gTapMutex);
    return gSinks.empty()?nullptr:f;
}

int64_t recordingClockUs(){
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace veyra::sink
