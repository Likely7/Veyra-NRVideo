// Routes the native-capture entry points of DeckLinkCapture.h to the device family
// that owns a path: the DeckLink SDK adapter or the XCAP card.
#include "veyra/source/DeckLinkCapture.h"
#include "veyra/source/XcapCapture.h"

namespace veyra::source::decklink {
// DeckLinkDriver.cpp
std::vector<Device> sdkDevices();
std::vector<Mode> sdkModes(std::wstring_view);
std::unique_ptr<Driver> makeSdkDriver();

namespace {
class Router final : public Driver {
    std::unique_ptr<Driver> active_;
public:
    bool open(std::wstring_view path, DriverSink& sink, std::wstring& error) override {
        stop();
        active_ = xcapture::isPath(path) ? xcapture::makeDriver() : makeSdkDriver();
        if (!active_) { error = L"原生采集接口不可用，请安装对应驱动或使用 DirectShow 入口"; return false; }
        return active_->open(path, sink, error);
    }
    std::vector<Mode> modes() override { return active_ ? active_->modes() : std::vector<Mode>{}; }
    bool autoDetection() const override { return active_ && active_->autoDetection(); }
    bool enable(const Mode& mode, Pixel pixel, bool automatic, bool audio, std::wstring& error) override {
        return active_ && active_->enable(mode, pixel, automatic, audio, error);
    }
    bool start(std::wstring& error) override { return active_ && active_->start(error); }
    bool pause(std::wstring& error) override { return active_ && active_->pause(error); }
    bool flush(std::wstring& error) override { return active_ && active_->flush(error); }
    unsigned audioChannels() override { return active_ ? active_->audioChannels() : 2; }
    unsigned audioBits() override { return active_ ? active_->audioBits() : 16; }
    void stop() noexcept override { if (active_) active_->stop(); }
};
}

std::vector<Device> devices() {
    auto out = xcapture::devices();
    for (auto& device : sdkDevices()) out.push_back(std::move(device));
    return out;
}
std::vector<Mode> modes(std::wstring_view path) { return xcapture::isPath(path) ? xcapture::modes(path) : sdkModes(path); }
std::unique_ptr<Driver> makeDriver() { return std::make_unique<Router>(); }
}
