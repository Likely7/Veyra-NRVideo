#pragma once
#include <atomic>
#include <chrono>
#include <cwctype>
#include <string>
#include <string_view>
#include <mutex>
#include "veyra/media/NetworkSettings.h"
extern "C" {
#include <libavformat/avformat.h>
}
namespace veyra::media {
// Network deadlines (#48). The old single 5 s limit applied to open, probe and every
// read alike, so a slow server or a Wi-Fi hiccup was reported as a failed source.
// Opening a connection and reading media now have separate limits; the owner's
// cancel flag (stop / source switch) still aborts every blocking call at once.
constexpr int kConnectTimeoutMs = 40000;     // avformat_open_input as a whole (a dead connection is cut at 15 s by rw_timeout)
constexpr int kProbeTimeoutMs = 30000;       // avformat_find_stream_info
constexpr int kReadTimeoutVodMs = 20000;     // one av_read_frame / seek on a seekable source
constexpr int kReadTimeoutLiveMs = 10000;    // one av_read_frame on a live source

inline bool networkMediaInput(std::wstring_view path) {
    const auto colon=path.find(L':');
    if(colon==std::wstring_view::npos||colon>5)return false;
    std::wstring scheme(path.substr(0,colon));
    for(auto& ch:scheme)ch=std::towlower(ch);
    return scheme==L"http"||scheme==L"https"||scheme==L"rtsp";
}
// A file on a server: a UNC path (\\host\share\...) or a drive letter mapped to a
// network share (GetDriveType == DRIVE_REMOTE). Local extended paths (\\?\C:\...)
// are local. Implemented in DemuxSession.cpp.
bool remoteMediaPath(std::wstring_view path);
// VEYRA_TEST_FORCE_DEMUX_SESSION=1: send local files through the shared demux
// session too, to compare it against the direct path frame by frame.
bool forceDemuxSessionForTest();
// Playback sources that read through the shared demux session. Offline users
// (export, tools) pass playback=false and keep the direct file path for NAS files.
inline bool sharedDemuxInput(std::wstring_view path, bool playback) {
    return networkMediaInput(path) || (playback && remoteMediaPath(path)) || forceDemuxSessionForTest();
}

// Per-operation deadlines only on networks; immediate owner cancellation on all inputs.
class InputInterrupt {
public:
    void configure(bool network,const std::atomic<bool>* owner,const std::atomic<bool>* local=nullptr,bool live=false){network_=network;owner_=owner;local_=local;live_=live;deadline_=0;}
    // Read / seek deadline.
    void arm(){deadline_=network_?now()+int64_t(live_?kReadTimeoutLiveMs:kReadTimeoutVodMs)*1000000ll:0;}
    void armConnect(){deadline_=network_?now()+int64_t(kConnectTimeoutMs)*1000000ll:0;}
    void armProbe(){deadline_=network_?now()+int64_t(kProbeTimeoutMs)*1000000ll:0;}
    void disarm(){deadline_=0;}
    static int callback(void* opaque){
        const auto& self=*static_cast<InputInterrupt*>(opaque);const auto deadline=self.deadline_.load();
        return (self.owner_&&self.owner_->load())||(self.local_&&self.local_->load())||(deadline&&now()>=deadline);
    }
private:
    static int64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    const std::atomic<bool>* owner_=nullptr;const std::atomic<bool>* local_=nullptr;
    bool network_=false,live_=false;std::atomic<int64_t> deadline_{0};
};
inline void networkMediaOptions(AVDictionary** options){
    static std::once_flag initialized;std::call_once(initialized,[]{avformat_network_init();});
    av_dict_set(options,"rw_timeout","15000000",0);av_dict_set(options,"timeout","15000000",0);
    av_dict_set(options,"rtsp_transport","tcp",0);
}
}
