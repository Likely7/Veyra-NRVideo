#pragma once
#include "veyra/pipeline/FramePacket.h"
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace veyra::source::decklink {
// Public ingress contract contains no SDK declarations. The SDK adapter and
// the injected test driver both exercise Capture's actual state machine.
enum class Pixel { Uyvy8, Bgra8, V210, Rgb10xLE };
struct Mode {
    uint32_t id=0, width=0, height=0;
    int64_t duration=0, scale=0;
    bool interlaced=false;
    std::wstring name;
    pipeline::ColorDescription color;
};
struct Device { std::wstring name,path; bool autoDetection=false; bool embeddedAudio=true; };
struct Frame {
    const uint8_t* data=nullptr; size_t bytes=0;
    uint32_t width=0,height=0,stride=0;
    Pixel pixel=Pixel::Uyvy8;
    int64_t pts100ns=0,duration100ns=0;
    bool noInput=false,bottomUp=false;
    pipeline::ColorDescription color;
    // Progressive delivery (XCAP): set when the frame is handed over while its rows still
    // arrive. Blocks until rows [0, n) are readable; n = height also means the whole frame is
    // valid. False: the frame was lost, do not publish it. Valid during the callback only.
    std::function<bool(uint32_t)> awaitRows;
    // Set by awaitRows(height): 1 the pixels equal the previous frame's, 0 they differ, -1 unknown.
    int repeat=-1;
};
struct Signal { Mode mode; bool rgb=false; unsigned bits=0; };
size_t rowBytes(Pixel,uint32_t width);
bool sameColor(const pipeline::ColorDescription&,const pipeline::ColorDescription&);
bool isPath(std::wstring_view);     // any native-capture endpoint
bool isXcapPath(std::wstring_view); // the XCAP card, not a Blackmagic device
std::wstring modeKey(uint32_t);
std::vector<Device> devices();
std::vector<Mode> modes(std::wstring_view);

class DriverSink {
public:
    virtual ~DriverSink()=default;
    virtual void formatChanged(const Signal&)=0;
    virtual void video(const Frame&)=0;
    virtual void audio(const void*,size_t,int64_t pts100ns)=0;
    virtual void failure(std::wstring)=0;
};
class Driver {
public:
    virtual ~Driver()=default;
    virtual bool open(std::wstring_view,DriverSink&,std::wstring&)=0;
    virtual std::vector<Mode> modes()=0;
    virtual bool autoDetection()const=0;
    // No control method may run from a driver callback. stop() detaches the
    // callback and waits for all in-flight callbacks before returning.
    virtual bool enable(const Mode&,Pixel,bool automatic,bool audio,std::wstring&)=0;
    virtual bool start(std::wstring&)=0;
    virtual bool pause(std::wstring&)=0;
    virtual bool flush(std::wstring&)=0;
    virtual void stop()noexcept=0;
    // Channels of the 48 kHz 16-bit PCM handed to DriverSink::audio: 2 or 8 (Windows 7.1 order).
    virtual unsigned audioChannels(){return 2;}
    // Container bits of that PCM: 16, or 32 holding 24 valid bits (left-justified, low byte zero).
    virtual unsigned audioBits(){return 16;}
};
std::unique_ptr<Driver> makeDriver();

class Capture final:private DriverSink {
public:
    explicit Capture(std::unique_ptr<Driver> driver=makeDriver());
    ~Capture();
    // Probe only: bounded wait for an actual frame, no pixels/audio retained.
    bool configure(std::wstring_view path,std::wstring_view key,bool audio,
                   unsigned timeoutMs=2500);
    bool start(std::function<void(const Frame&)> video,
               std::function<void(const void*,size_t,int64_t)> audio={});
    void stop()noexcept;
    bool needsReopen()const;
    std::wstring error()const;
    Frame contract()const; // data is always null; metadata from valid probe frame
    Mode mode()const;
    unsigned audioChannels()const;
    unsigned audioBits()const;
private:
    void formatChanged(const Signal&)override;
    void video(const Frame&)override;
    void audio(const void*,size_t,int64_t)override;
    void failure(std::wstring)override;
    struct Impl;std::unique_ptr<Impl> p_;
};
}
