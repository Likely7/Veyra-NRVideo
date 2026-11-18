#pragma once
#include "veyra/pipeline/FramePacket.h"
#include <dshow.h>
#include <wrl/client.h>
#include <array>
#include <string_view>

struct IKsControl;
namespace veyra::source {
struct ElgatoHdrPacket {
    bool valid=false;
    unsigned eotf=0;
    float peak=0, maxCll=0, maxFall=0;
};
bool isElgatoMk2(std::wstring_view name);
// Any Elgato capture filter: "Elgato ..." names and the "Game Capture ..." family.
bool isElgatoDevice(std::wstring_view name);
ElgatoHdrPacket parseElgatoHdrPacket(const std::array<unsigned char,32>& bytes);
// Elgato 4K X: reads the source HDR InfoFrame from the card's UVC extension
// unit command mailbox (topology node `node`). False leaves `packet` untouched.
bool readElgato4kxHdrPacket(IKsControl* control,ULONG node,std::array<unsigned char,32>& packet);

// Session-scoped device control; release it after Stop and before the filter.
class ElgatoHdrControl {
public:
    ~ElgatoHdrControl();
    ElgatoHdrControl()=default;
    ElgatoHdrControl(const ElgatoHdrControl&)=delete;
    ElgatoHdrControl& operator=(const ElgatoHdrControl&)=delete;
    // p010=true for the 10-bit HDR format, false for 8-bit formats (NV12, YUY2, RGB).
    bool configure(IUnknown* filter,std::wstring_view name,bool p010,
                   unsigned overrideValue,pipeline::ColorDescription& color);
private:
    // strict: a failed or contradicted write fails the connection (MK.2 P010).
    bool setTonemap(DWORD desired,bool strict);
    Microsoft::WRL::ComPtr<IKsPropertySet> properties_;
    DWORD original_=0;
    bool restore_=false;
};
}
