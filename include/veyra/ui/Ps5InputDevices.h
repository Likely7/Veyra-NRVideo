#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QWindow>
#include <windows.h>
#include <vector>

namespace veyra::ui {

// Observe the devices sending input to the local practice window.
class Ps5InputDevices final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(QString keyboard READ keyboard NOTIFY devicesChanged)
    Q_PROPERTY(QString mouse READ mouse NOTIFY devicesChanged)
public:
    using ReadHeader = RAWINPUTHEADER (*)(HRAWINPUT);
    using DeviceName = QString (*)(HANDLE);
    explicit Ps5InputDevices(ReadHeader read = nullptr, DeviceName name = nullptr);
    ~Ps5InputDevices() override;
    QString keyboard() const { return keyboard_; }
    QString mouse() const { return mouse_; }
    Q_INVOKABLE void start(QWindow* window);
    Q_INVOKABLE void stop();
signals:
    void devicesChanged();
private:
    ReadHeader readHeader_;
    DeviceName deviceName_;
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;
    HWND window_ = nullptr;
    std::vector<RAWINPUTDEVICE> added_;
    HANDLE lastKeyboard_ = nullptr, lastMouse_ = nullptr;
    QString keyboard_, mouse_;
};
}
