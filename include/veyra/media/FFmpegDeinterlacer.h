#pragma once
#include <memory>
#include <string>
struct AVFrame;
namespace veyra::media {
struct HardwareSurfaceView;
// Optional LGPL FFmpeg filter module. GPU surfaces stay on GPU; software
// decoded frames use bwdif. Sources own demux/decode/PTS and seek resets.
class FFmpegDeinterlacer {
public:
    static bool available();
    FFmpegDeinterlacer();
    ~FFmpegDeinterlacer();
    bool open(const AVFrame*,const HardwareSurfaceView*,int tbNum,int tbDen,int fpsNum,int fpsDen);
    int push(const AVFrame*,const HardwareSurfaceView* = nullptr);
    int receive(const AVFrame**);
    int timeBaseNum()const;
    double observedPictureRate()const;
    int timeBaseDen()const;
    bool active()const;
    const std::string& path()const;
    void close();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
