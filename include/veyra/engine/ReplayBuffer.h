#pragma once
// Instant replay (#103): the encoded packets of the last seconds or minutes,
// in memory. The recorder's writer thread appends; a save takes a clip (the
// packet bytes are shared, nothing is copied) on its own thread while the
// buffer goes on filling. Whole GOPs are dropped from the front (the live
// encoders use a 2 s GOP), so a clip starts on a keyframe and holds the chosen
// length plus at most one GOP. A memory cap may keep less; coverage() says so.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct AVCodecParameters;

namespace veyra::engine {

class ReplayBuffer {
public:
    // Video pts in µs, audio pts in 48 kHz samples, both from the segment's
    // first frame (the recording timeline). Audio may start before zero
    // (encoder priming) and carries its duration.
    struct Packet {
        std::shared_ptr<const std::vector<uint8_t>> data;
        int64_t pts=0;
        int64_t duration=0;
        bool key=false;
    };
    struct VideoFormat {
        uint32_t width=0,height=0,hdrMode=0,fps=60;
        bool hevc=false;
        std::vector<uint8_t> extradata;   // Annex B parameter sets
    };
    struct AudioFormat {
        std::shared_ptr<const AVCodecParameters> parameters;
        std::string name;                 // MP4 handler name
    };
    struct Clip {
        VideoFormat video;
        std::vector<AudioFormat> audio;
        std::vector<Packet> videoPackets;              // from a keyframe
        std::vector<std::vector<Packet>> audioPackets; // per track, from two frames before startUs
        int64_t startUs=0,endUs=0;                     // on the recording timeline
        int64_t originUs=0;                            // recording clock of the timeline's zero
        size_t bytes=0;
    };
    struct Coverage {
        double seconds=0;   // what a save now would hold, at most the chosen length
        size_t bytes=0;     // memory the buffer holds
        bool limited=false; // the memory cap, not the length, decided the last trim
        bool picture=false;
    };

    ReplayBuffer(int64_t keepUs,size_t capacityBytes);
    void setFormat(VideoFormat video,std::vector<AudioFormat> audio);
    // Recording clock (µs) of the timeline's zero; a clip asked for at a clock
    // time ends there.
    void setOrigin(int64_t originUs);
    void pushVideo(Packet packet);
    void pushAudio(size_t track,Packet packet);
    // The segment ended at `endUs` (timeline); nothing more arrives.
    void finish(int64_t endUs);

    bool finished() const;
    // Every track reached `clockUs` (recording clock), or the segment ended.
    bool covers(int64_t clockUs) const;
    // What a save asked for at `clockUs` holds: from the latest keyframe at
    // or before (end - length), else the oldest one, up to the request.
    Clip clip(int64_t clockUs) const;
    Coverage coverage() const;
    int64_t keepUs() const {return keep_;}

private:
    size_t clipStart(int64_t endUs) const;   // index into video_, locked
    int64_t videoEnd() const;                 // locked
    void trim();                              // locked

    mutable std::mutex m_;
    const int64_t keep_;
    const size_t capacity_;
    VideoFormat video_;
    std::vector<AudioFormat> audio_;
    std::deque<Packet> videoPackets_;
    std::deque<size_t> keys_;                 // sequence numbers of the keyframes in videoPackets_
    size_t frontSequence_=0,nextSequence_=0;
    std::vector<std::deque<Packet>> audioPackets_;
    std::vector<int64_t> audioEnd_;           // samples, per track
    size_t bytes_=0;
    int64_t origin_=0,end_=-1,frameUs_=16667;
    bool limited_=false;
};

} // namespace veyra::engine
