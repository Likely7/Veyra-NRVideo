#pragma once
#include "veyra/source/IFrameSource.h"
#include "veyra/source/CaptureColorOverride.h"
#include "veyra/source/CaptureFormatSelection.h"
#include <memory>
#include <string_view>
#include <vector>
#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/source/AudioInputRecovery.h"
namespace veyra::source {
struct CaptureDevice {
    std::wstring name;
    // DirectShow moniker DevicePath/display name. This is stable across a
    // fresh enumeration, unlike the ordinal exposed by ICreateDevEnum.
    std::wstring path;
    // Only populated for video devices: the selected video filter exposes an
    // audio output pin that can be used without a second audio filter.
    bool hasEmbeddedAudio=false;
    bool wasapi=false; // Audio only: path is a Windows recording endpoint ID.
};
constexpr int kCaptureAudioDisabled=-1;
constexpr int kCaptureAudioFromVideoDevice=-2;
constexpr int kCaptureAudioWasapi=-3; // capture2 only; requires explicit endpoint ID
struct CaptureMetrics {
    uint64_t received=0, delivered=0, dropped=0;
    double callbackFps=0, readAgeMs=0, frameAgeMs=0;
    bool sampledBlack=false; // Sparse source pixels, not input signal detection.
};
class CaptureCardSource final:public IFrameSource {
public:
    CaptureCardSource();~CaptureCardSource()override;
    static std::vector<CaptureDevice> deviceDetails(bool audio=false);
    static std::vector<std::wstring> devices(bool audio=false);
    static std::wstring makeCapturePath(unsigned videoIndex,const CaptureDevice& video,
        int format,int audioMode,const CaptureDevice* audio,unsigned colorOverride=0,double requestedFps=0,std::wstring_view formatKey={});
    static std::vector<CaptureFormat> formats(unsigned device);
    static std::vector<CaptureFormat> formatsByPath(std::wstring_view devicePath);
    // Explicit user action on the UI COM apartment; no Run or auto-routing.
    static bool showDeviceProperties(std::wstring_view devicePath,uintptr_t owner,bool inputSelector,std::wstring& error,std::wstring* changedFormatKey=nullptr);
    bool open(const SourceOpenDesc&)override;
    // Negotiate/allocate before GPU initialization, but do not queue frames
    // or start audio until the presenter and enhancement graph are ready.
    bool configure(const SourceOpenDesc&);
    bool start();
    bool reconnect(float gain,unsigned syncMode,int offsetMs);
    // Owner-thread recovery of DirectShow audio pins; same video filter retained.
    void recoverAudio(float gain,unsigned syncMode,int offsetMs);
    bool setAudioGain(float); // call on the graph owner thread; never system volume
    // 0 automatic, 1 PCM only, 2 bitstream preferred. Takes effect on the next
    // connect/reconnect because the audio media type is negotiated there.
    void setAudioIngress(unsigned mode);
    // 0 auto, 1 minimum, 2 driver default (see CaptureBuffer.h). Applied on the
    // next connect: the allocator is created while the graph is built.
    void setBufferMode(unsigned mode);
    // Manual ingest flip for devices whose declared DIB orientation does not
    // match the samples (RGB24 upside-down reports). Applies to the next
    // sample; works for RGB and YUV without touching the device.
    void setVerticalFlip(bool enabled);
    // N1 diagnostic: keep the legacy per-pixel CPU unpack (BGR0/YUY2 targets)
    // instead of the GPU unpack path. Applied on the next connect.
    void setCpuUnpack(bool enabled);
    CaptureMetrics metrics()const;
    void videoPresented(double ptsMs,int64_t host100ns,int64_t arrival100ns);
    void videoReset(bool resetAudio=true);
    void setAudioSync(unsigned mode,int offsetMs);
    sink::CaptureAudioState audioState()const;
    const SourceInfo& info()const override;
    const std::wstring& errorMessage()const{return error_;}
    SourceReadStatus read(pipeline::FramePacket&,const AVFrame**)override;
    SourceReadStatus tryRead(pipeline::FramePacket&,const AVFrame**);
    // Auto-reset event signalled after each delivered sample; lets the graph
    // owner wait on "frame or deadline" instead of a fixed 1 ms timer slice.
    // Null until configure(); the handle stays valid until close().
    HANDLE frameEvent()const;
    bool seek(const pipeline::Rational&)override{return false;}
    void close()noexcept override;
private:
    std::wstring error_;
    bool connectDirectShowAudio(const SourceOpenDesc&);
    // AVerMedia capture cards need their installed vendor component to arm
    // non-PCM (Dolby/DTS) passthrough before the audio pin is negotiated.
    bool applyVendorAudioSwitch(const std::wstring& audioName,const std::wstring& audioPath);
    SourceReadStatus readWithWait(pipeline::FramePacket&,const AVFrame**,unsigned milliseconds);
    struct Impl;std::unique_ptr<Impl> p_;
    SourceOpenDesc reconnectDesc_;
    std::wstring reconnectFormat_;
    SourceInfo reconnectInfo_;
    uint64_t epoch_=1,receivedOffset_=0,deliveredOffset_=0,droppedOffset_=0;
};
}
