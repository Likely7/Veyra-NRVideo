#pragma once
// Microphone for live recording and the instant replay buffer (#101): one
// shared-mode WASAPI capture stream on its own thread, open while at least one
// recorder listens. Every packet is stamped with the recording clock
// (sink/RecordingAudioTap.h) at the moment it was captured, so the voice lines
// up with the picture the user saw, and is delivered as 48 kHz interleaved
// float stereo, the format of the rendered-sound tap. A mono microphone is on
// both channels at full level.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace veyra::engine {

class MicrophoneSink {
public:
    virtual ~MicrophoneSink()=default;
    // Microphone thread: copy and return, never block.
    virtual void microphonePcm(const float* stereo,size_t frames,int64_t capturedUs)=0;
};

struct MicrophoneDevice {std::wstring id,name;};

struct MicrophoneStatus {
    bool open=false;             // capturing now
    std::wstring device;         // friendly name of the endpoint in use (or last tried)
    std::wstring error;          // why the last attempt failed; empty while it works
    uint32_t rate=0,channels=0;  // the endpoint's own format
    uint64_t packets=0,failures=0,opens=0;
};

// Active recording endpoints (enumeration only, nothing is opened).
std::vector<MicrophoneDevice> microphoneDevices();
// Any thread, never waits for the device: the capture thread opens
// `endpointId` (empty: the default recording device) while a sink is attached.
// A different id reopens the stream for every sink.
void attachMicrophone(std::shared_ptr<MicrophoneSink> sink,const std::wstring& endpointId);
void detachMicrophone(const MicrophoneSink* sink);
MicrophoneStatus microphoneStatus();

} // namespace veyra::engine
