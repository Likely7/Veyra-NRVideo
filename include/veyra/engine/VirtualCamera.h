#pragma once
#include "veyra/engine/PresentCapture.h"
#include <memory>
#include <string>

namespace veyra::engine {
struct VirtualCameraStatus {
    bool active=false,installed=false,failed=false;
    bool systemInstalled=false,installing=false;
    uint32_t width=0,height=0,fpsLimit=0; // fpsLimit 0: follow the consumer
    uint32_t consumerFps=0;               // what the camera's consumer negotiated, 0 unknown
    uint32_t presentedFps=0;              // the presented rhythm the camera picks from, 0 unknown
    bool systemOutdated=false;            // the system-wide modules are not this package's
    uint32_t processingWidth=0,processingHeight=0;
    uint64_t frames=0,dropped=0;
    std::wstring error;
    std::wstring setupError;
};
// Optional compatibility output: GPU conversion + bounded asynchronous NV12
// readback. It never waits on a camera consumer or a CPU fence in record().
class VirtualCamera final : public PresentCapture {
public:
    VirtualCamera();
    ~VirtualCamera() override;
    // fps: 0 follows the rate the consumer negotiated (60 until one does); 30/60/120 cap it.
    bool start(uint32_t fps,std::wstring& error);
    void stop();
    bool uninstall(std::wstring& error);
    bool installForAllUsers(std::wstring& error);
    void sessionEnded(); // Remains enabled; old pictures are invalidated.
    VirtualCameraStatus status() const;
    bool needsFrame()const override;
    bool record(ID3D12GraphicsCommandList*,const Frame&) override;
    void submitted(ID3D12Fence*,uint64_t) override;
    void discarded() override;
private:
    struct State;
    struct Worker;
    std::shared_ptr<State> state_;
    std::unique_ptr<Worker> worker_;
};
}
