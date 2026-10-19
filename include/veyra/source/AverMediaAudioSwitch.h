#pragma once

// AVerMedia multichannel-audio switch (GC553G2 / GC553PRO / GC575).
//
// Those cards forward HDMI audio as 2.0 PCM until something tells the internal
// audio path to pass the compressed (non-PCM) stream through. That "something"
// is AVerMedia's own component: the user installs it for OBS ("AVerMedia
// Multichannel Audio"), and OBS then gets the Dolby stream as 6 PCM channels.
// Nothing is redistributed here - this module loads the copy that already sits
// on the machine, calls the same entry points the OBS plugin calls, and leaves
// capture/decoding to Veyra's existing audio graph.
//
// Behaviour measured from avt_device_opener.dll 1.0.2 (see
// docs/AVERMEDIA_5P1_CAPTURE_FEASIBILITY_2026-09-17.md):
//   * VendorSdk forwards 1:1 to RTICE_SDK_x64!initialize / uninitialize /
//     setDevice / setPort / closePort and to AT_Audio_Get_HdmiRX_AudioFormat,
//     AT_Audio_CSR_Get_Send_Non_Pcm_Data_On_off, AT_Audio_Send_Non_Pcm_Data.
//     Each call is wrapped in the global mutex "Global\RTK_SDK_LOCK_NAME", so
//     only one process drives the card at a time (OBS and Veyra can therefore
//     not both hold it).
//   * AT_Audio_Get_HdmiRX_AudioFormat returns 20 (0x14) while the HDMI source
//     sends non-PCM; the enable path refuses to switch for any other value.
//   * DeviceOpener::StartChecking runs the vendor thread that re-applies the
//     switch when the console toggles between PCM and bitstream.
//
// The two helper DLLs are prebuilt and carry no source; they are only ever
// loaded from the user's own installation, by absolute path.

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace veyra::source {

enum class AverMediaSwitchState { Unavailable, Ready, Switched, Failed };

// One AVerMedia USB function as Windows sees it in the device tree.
//
// This exists because the DirectShow audio moniker of these cards does not
// always carry a DevicePath: on the machine that produced the 2026-09-17 field
// log the stored path was the class-manager display name
// ("@device:cm:{...}\wave:{...}"), which contains no vid_/pid_ at all, while
// the vendor component parses exactly those out of the path. The device tree
// still knows the truth.
struct AverMediaUsbFunction {
    std::wstring instanceId;     // USB\VID_07CA&PID_2553&MI_02\5&2f0a1c4&0&0002
    std::wstring friendlyName;   // driver friendly name, may be empty
    std::wstring interfacePath;  // \\?\usb#vid_07ca&pid_2553&mi_02#...#{guid}
};

struct AverMediaSwitchStatus {
    AverMediaSwitchState state = AverMediaSwitchState::Unavailable;
    bool componentFound = false;
    bool dllsLoaded = false;
    bool sdkReady = false;
    bool nonPcmActive = false;   // what the chip reports *now* (source format)
    bool passthroughEnabled = false;  // VendorSdk::getNonPcmOnOff (the armed mode)
    bool passthroughKnown = false;    // missing/failed query is not "off"
    int chipAudioFormat = -1;    // raw AT_Audio_Get_HdmiRX_AudioFormat value
    std::wstring componentRoot;  // folder that owns obs-plugins\64bit
    std::string detail;          // last result/error, for the log
};

class AverMediaAudioSwitch {
public:
    AverMediaAudioSwitch();
    ~AverMediaAudioSwitch();
    AverMediaAudioSwitch(const AverMediaAudioSwitch&) = delete;
    AverMediaAudioSwitch& operator=(const AverMediaAudioSwitch&) = delete;

    // Folder of the installed component (the one containing
    // obs-plugins\64bit\avt_device_opener.dll). Empty when not installed.
    static std::wstring locateComponentRoot();
    // Cheap test used to skip unrelated audio devices: the DirectShow device
    // path of an AVerMedia capture device carries vid_07ca.
    static bool isAverMediaDevicePath(std::wstring_view devicePath);
    // Every present USB function whose device instance id contains
    // `vendorToken` (production uses L"vid_07ca"). Enumerated from the device
    // tree, so it works when DirectShow gives us nothing usable. `vendorToken`
    // is a parameter so tests can prove the enumeration itself works on a
    // machine that has no AVerMedia hardware.
    static std::vector<AverMediaUsbFunction> findUsbFunctions(std::wstring_view vendorToken);
    // Picks the audio function out of a device-tree result: the UAC interface
    // is the one Windows exposes as an audio endpoint. Falls back to the only
    // entry when there is exactly one.
    static const AverMediaUsbFunction* pickAudioFunction(const std::vector<AverMediaUsbFunction>& functions);

    // Loads the component (once) and arms non-PCM passthrough for the card
    // behind `devicePath`. Never throws; failures are reported in status().
    // Vendor audio graph setup defers monitoring until after Start so its
    // conditional enable cannot race the component's own monitor thread.
    bool apply(const std::wstring& deviceName, const std::wstring& devicePath,bool monitor=true);
    // Starts the vendor monitor thread that re-applies the switch when the
    // console changes audio format. Called by apply(); idempotent.
    void startMonitoring();
    // Stops the vendor monitor thread and releases the SDK port. The DLLs stay
    // loaded until the process exits: their own worker must not be unloaded
    // underneath a still-running thread.
    void stop() noexcept;
    bool active() const;
    // Re-reads the HDMI format and the passthrough flag. The format lives in
    // the low byte (measured -236 = 0xFFFFFF14 for a bitstream).
    bool refreshChipState();
    // True when that low byte is 20, the non-PCM/Dolby source format.
    bool sourceIsBitstream();
    bool passthroughEnabled() const;
    // VendorSdk::setNonPcmOnOff. Callers arm only when the flag is off:
    // forcing the call while the chip already reports armed left the pin
    // silent (2026-10-01). False is rejected; this API never disarms.
    bool setPassthrough(bool enable);
    // One alignment after apply(), before the vendor graph starts. Arms
    // passthrough when the flag is off. The OBS plugin makes the same call
    // on every start; without it a Dolby source stays digital silence.
    void alignPassthroughOnce();
    const AverMediaSwitchStatus& status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::source
