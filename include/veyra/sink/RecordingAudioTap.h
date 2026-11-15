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

// Any thread. Several recorders listen at once (a recording and the instant
// replay buffer, #103): each adds itself and removes itself when it is done.
void addRecordingAudioSink(std::shared_ptr<RecordingAudioSink> sink);
void removeRecordingAudioSink(const RecordingAudioSink* sink);
bool recordingAudioActive();
// What the renderer calls: forwards to every sink added, nullptr while none is.
std::shared_ptr<RecordingAudioSink> recordingAudioSink();
// The clock the recorder stamps video and audio with (steady_clock, µs).
int64_t recordingClockUs();

} // namespace veyra::sink
