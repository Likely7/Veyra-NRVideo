#pragma once
// Live recording takes the PCM the player renders - every source that plays
// through AudioRenderer: files, capture-card monitoring, PS5/Moonlight/Xbox
// streaming - before volume and mute, stamped with the steady-clock time at
// which its first frame reaches the endpoint. While no recording takes audio
// the renderer only reads one atomic flag.
#include <cstddef>
#include <cstdint>
#include <memory>
#include "veyra/sink/AudioFormat.h"

namespace veyra::sink {

class RecordingAudioSink {
public:
    virtual ~RecordingAudioSink()=default;
    // Audio owner thread: copy and return, never block. `renderer`
    // identifies the endpoint, so a second renderer can be ignored.
    virtual void renderedPcm(const float* interleaved,size_t frames,const AudioFormat& format,int64_t playoutUs,const void* renderer)=0;
};

// nullptr detaches. Any thread.
void setRecordingAudioSink(std::shared_ptr<RecordingAudioSink> sink);
bool recordingAudioActive();
std::shared_ptr<RecordingAudioSink> recordingAudioSink();
// The clock the recorder stamps video and audio with (steady_clock, µs).
int64_t recordingClockUs();

} // namespace veyra::sink
