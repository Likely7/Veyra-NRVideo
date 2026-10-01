#pragma once
#include "veyra/engine/ExportOptions.h"
#include <atomic>
#include <memory>
#include <vector>
struct AVFormatContext;

namespace veyra::engine {
struct ExportTrackInfo {
    int index=-1,channels=0;
    bool audio=false,subtitle=false,selected=false,compatible=true,converted=false;
    bool defaultTrack=false,forced=false;
    std::string codec,language,title;
    std::wstring action;
};
struct ExportMediaInfo {
    double duration=0;
    std::vector<ExportTrackInfo> tracks;
    std::wstring error;
    // Tracks an "all tracks" policy could not carry into this container. They
    // are reported before start and in the result, never dropped silently.
    std::wstring skipped;
};
// Packet-only companion to the existing GPU video path. One demuxer, bounded
// lookahead, common video origin; text conversion uses libavcodec.
class ExportStreams {
public:
    ExportStreams();~ExportStreams();
    bool prepare(const std::wstring& input,AVFormatContext* output,const ExportMediaOptions& options,
                 int defaultAudio,double start,double end,std::atomic<bool>& cancel,std::wstring& error);
    bool writeUntil(double seconds,double videoOrigin,bool final,std::wstring& error);
    const ExportMediaInfo& info()const;
    static ExportMediaInfo probe(const std::wstring& input,const ExportMediaOptions& options,
                                 double start,double end,std::atomic<bool>& cancel);
    static void enableWorkerLogging(bool detailed=false);
private:
    struct Impl;std::unique_ptr<Impl> p_;
};
}
