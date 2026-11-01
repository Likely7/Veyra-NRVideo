#include "veyra/sink/RecordingAudioTap.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace veyra::sink {
namespace {
std::mutex gTapMutex;
std::shared_ptr<RecordingAudioSink> gTap;
std::atomic<bool> gTapActive{false};
}

void setRecordingAudioSink(std::shared_ptr<RecordingAudioSink> sink){
    std::lock_guard lock(gTapMutex);
    gTap=std::move(sink);
    gTapActive.store(gTap!=nullptr,std::memory_order_release);
}

bool recordingAudioActive(){return gTapActive.load(std::memory_order_acquire);}

std::shared_ptr<RecordingAudioSink> recordingAudioSink(){
    std::lock_guard lock(gTapMutex);
    return gTap;
}

int64_t recordingClockUs(){
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace veyra::sink
