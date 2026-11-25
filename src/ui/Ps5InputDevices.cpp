#include "veyra/ui/Ps5InputDevices.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <algorithm>
#include <array>

namespace veyra::ui {
namespace {
bool registrations(std::vector<RAWINPUTDEVICE>& devices) {
    UINT size = 0;
    if (GetRegisteredRawInputDevices(nullptr, &size, sizeof(RAWINPUTDEVICE)) == UINT(-1)) return false;
    devices.resize(size);
    return !size || GetRegisteredRawInputDevices(devices.data(), &size, sizeof(RAWINPUTDEVICE)) != UINT(-1);
}

QString deviceName(HANDLE device) {
    UINT length = 0;
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &length) == UINT(-1) || !length) return {};
    std::wstring path(length + 1, L'\0');
    if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, path.data(), &length) == UINT(-1)) return {};
    std::array<wchar_t, 512> value{};
    DEVPROPTYPE type = 0;
    ULONG bytes = sizeof(value);
    DEVINST node;
    if (CM_Get_Device_Interface_PropertyW(path.c_str(), &DEVPKEY_Device_InstanceId, &type,
            reinterpret_cast<PBYTE>(value.data()), &bytes, 0) == CR_SUCCESS &&
        CM_Locate_DevNodeW(&node, value.data(), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
        for (int depth = 0; depth < 3; ++depth) {
            for (const auto* property : {&DEVPKEY_Device_BusReportedDeviceDesc, &DEVPKEY_Device_FriendlyName}) {
                bytes = sizeof(value);
                if (CM_Get_DevNode_PropertyW(node, property, &type, reinterpret_cast<PBYTE>(value.data()),
                        &bytes, 0) == CR_SUCCESS && type == DEVPROP_TYPE_STRING && value[0])
                    return QString::fromWCharArray(value.data());
            }
            DEVINST parent;
            if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS) break;
            node = parent;
        }
    }
    return QString::fromWCharArray(path.c_str()).section('#', 1, 2);
}

RAWINPUTHEADER deviceHeader(HRAWINPUT input) {
    RAWINPUTHEADER header{};
    UINT bytes = sizeof(header);
    if (GetRawInputData(input, RID_HEADER, &header, &bytes, sizeof(header)) != sizeof(header)) return {};
    return header;
}
}

Ps5InputDevices::Ps5InputDevices(ReadHeader read, DeviceName name)
    : readHeader_(read ? read : deviceHeader), deviceName_(name ? name : deviceName) {}

Ps5InputDevices::~Ps5InputDevices() { stop(); }

void Ps5InputDevices::start(QWindow* window) {
    stop();
    if (!window) return;
    window_ = reinterpret_cast<HWND>(window->winId());
    keyboard_.clear();
    mouse_.clear();
    lastKeyboard_ = lastMouse_ = nullptr;
    emit devicesChanged();
    std::vector<RAWINPUTDEVICE> current;
    if (!registrations(current)) return;
    for (const USHORT usage : {0x02, 0x06}) {
        if (std::any_of(current.begin(), current.end(), [usage](const auto& d) {
                return d.usUsagePage == 1 && d.usUsage == usage;
            })) continue;
        RAWINPUTDEVICE device{1, usage, 0, window_};
        if (RegisterRawInputDevices(&device, 1, sizeof(device))) added_.push_back(device);
    }
    QCoreApplication::instance()->installNativeEventFilter(this);
}

void Ps5InputDevices::stop() {
    if (!window_) return;
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
    std::vector<RAWINPUTDEVICE> current;
    if (registrations(current)) {
        for (auto device : added_) {
            const bool stillOurs = std::any_of(current.begin(), current.end(), [&](const auto& d) {
                return d.usUsagePage == device.usUsagePage && d.usUsage == device.usUsage &&
                       d.dwFlags == device.dwFlags && d.hwndTarget == window_;
            });
            if (stillOurs) {
                device.dwFlags = RIDEV_REMOVE;
                device.hwndTarget = nullptr;
                RegisterRawInputDevices(&device, 1, sizeof(device));
            }
        }
    }
    added_.clear();
    window_ = nullptr;
}

bool Ps5InputDevices::nativeEventFilter(const QByteArray& type, void* message, qintptr*) {
    if (type != "windows_generic_MSG") return false;
    const auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_INPUT || GET_RAWINPUT_CODE_WPARAM(msg->wParam) != RIM_INPUT) return false;
    // An existing raw-input registration may target another HWND on Veyra's
    // UI thread. Observe its device identity only while practice has focus.
    auto* focus = QGuiApplication::focusWindow();
    if (!focus || !focus->isActive() || reinterpret_cast<HWND>(focus->winId()) != window_) return false;
    const auto header = readHeader_(reinterpret_cast<HRAWINPUT>(msg->lParam));
    if (!header.hDevice) return false;
    if (header.dwType == RIM_TYPEKEYBOARD && header.hDevice != lastKeyboard_) {
        lastKeyboard_ = header.hDevice;
        keyboard_ = deviceName_(lastKeyboard_);
        emit devicesChanged();
    } else if (header.dwType == RIM_TYPEMOUSE && header.hDevice != lastMouse_) {
        lastMouse_ = header.hDevice;
        mouse_ = deviceName_(lastMouse_);
        emit devicesChanged();
    }
    return false; // Qt retains the foreground message and its normal cleanup.
}
}
