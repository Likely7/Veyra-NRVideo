#pragma once
#include "veyra/source/CaptureCardSource.h"

namespace veyra::source {
// Native NTV2 capture stays behind the existing capture facade; the renderer
// receives the same AVFrame/FramePacket contract as DirectShow.
class AjaCaptureSource final {
public:
    AjaCaptureSource();
    ~AjaCaptureSource();
    static bool isDevice(std::wstring_view path);
    static std::vector<CaptureDevice> devices();
    static std::vector<CaptureFormat> formats(std::wstring_view path);
    bool configure(std::wstring_view path, int audioMode, double requestedFps);
    bool start();
    void close() noexcept;
    const SourceInfo& info() const;
    const std::wstring& error() const;
    SourceReadStatus read(pipeline::FramePacket&, const AVFrame**, unsigned waitMs);
    CaptureMetrics metrics() const;
    void setVerticalFlip(bool);
    bool setAudioGain(float);
    void setAudioSync(unsigned, int);
    void videoPresented(double, int64_t, int64_t);
    void videoReset(bool);
    sink::CaptureAudioState audioState() const;
private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
}
