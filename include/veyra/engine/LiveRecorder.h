#pragma once

// LiveRecorder: records the processed picture the player presents, with the
// sound it plays.
//
// Picture: VideoPresenter calls record() inside its presentation command list
// (PresentCapture), so turning the frame into NV12/P010 costs one compute pass
// on the GPU and nothing on the CPU; the engine thread never waits. A frame
// that finds every slot still encoding, or the disk behind, is skipped and
// counted instead of stalling playback. NVENC (NVIDIA) waits for the
// conversion on the GPU; the Media Foundation path (AMD, Intel) reads it back
// on the recorder's own thread.
// Sound: the PCM every renderer plays (sink/RecordingAudioTap.h), before
// volume and mute, timed by when it reaches the endpoint; AAC 48 kHz stereo.
// Gaps become silence and clock drift is absorbed by resampling, so the track
// stays continuous and in sync with the picture.
// File: fragmented MP4, so a crash keeps everything written so far. Settings
// changes keep the file; a change of the output size or HDR mode continues in
// a new file (-2, -3, ...). Files are finished on the recorder's threads.
#include <cstdint>
#include <memory>
#include <string>

#include "veyra/engine/PresentCapture.h"

namespace veyra::engine {

struct RecordingSettings {
    std::wstring directory;      // output folder (created when missing)
    uint32_t bitrateMbps=0;      // 0: automatic from the picture size and frame rate
    bool hevc=false;             // H.264 unless chosen; HDR always records HEVC 10-bit
    uint32_t fpsLimit=0;         // 0: every presented frame
    bool audio=true;             // record the sound the player plays
};

struct RecordingStatus {
    bool active=false;           // recording, or waiting for the first frame
    bool failed=false;           // the last recording stopped on an error
    bool finishing=false;        // a file is still being finished in the background
    std::wstring path;           // the file being written, or the last one finished
    std::wstring error;          // why the last recording failed
    std::wstring encoder;        // e.g. "NVIDIA NVENC H.264"
    std::wstring note;           // stop reason or a new-file note, for the interface
    uint64_t frames=0;           // frames encoded since start
    uint64_t dropped=0;          // presented frames skipped because the encoder or disk was busy
    double seconds=0;            // time since the first recorded frame
    uint32_t files=0;            // files this recording wrote (more than one after a size change)
    bool audio=false;            // the current file has a sound track
    uint64_t started=0;          // +1 per start
    uint64_t saved=0;            // +1 per finished file; path is that file while not active
};

class LiveRecorder final : public PresentCapture {
public:
    LiveRecorder();
    ~LiveRecorder() override;
    LiveRecorder(const LiveRecorder&)=delete;
    LiveRecorder& operator=(const LiveRecorder&)=delete;

    // Any thread. Checks the folder; the file opens with the next presented frame.
    bool start(const RecordingSettings& settings,std::wstring& error);
    // Any thread. The file is finished in the background.
    void stop(const std::wstring& note={});
    bool active() const;
    RecordingStatus status() const;
    // Engine thread: the presented frame rate, used for rate control when
    // every presented frame is recorded.
    void setPresentRateHint(double fps);
    // Engine thread: the session that owns the device ended; so does the recording.
    void sessionEnded(const std::wstring& note);
    // Stops and waits until every file is finished, at most timeoutMs.
    void waitIdle(uint32_t timeoutMs);

    // PresentCapture: engine thread, inside VideoPresenter::present().
    bool record(ID3D12GraphicsCommandList* list,const Frame& frame) override;
    void submitted(ID3D12Fence* fence,uint64_t value) override;
    void discarded() override;

    struct Segment;
private:
    struct State;
    std::unique_ptr<State> s_;
};

} // namespace veyra::engine
