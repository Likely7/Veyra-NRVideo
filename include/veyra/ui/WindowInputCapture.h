// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <windows.h>
#include <functional>

#include "veyra/moonlight/InputRouter.h"



namespace veyra::ui {

class WindowInputCapture : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    using MouseGrab = std::function<bool(HWND, bool)>;
    explicit WindowInputCapture(moonlight::InputSink& sink, bool releaseOnly, QObject* parent = nullptr,
                                MouseGrab mouseGrab = {});
    ~WindowInputCapture() override;

    void setWindow(HWND topLevel) { window_ = topLevel; }
    void listenForLocalKeys(bool on) { listenForLocalKeys_ = on; }
    bool captured() const { return captured_; }
    // Returns false when it cannot start (no window, or the window is not in the foreground).
    bool capture(bool on);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void capturedChanged(bool captured);
    void releaseRequested();   // Ctrl+Alt+Shift+Z was pressed (capture is already released)
    void quitRequested();      // Ctrl+Alt+Shift+Q
    void statsRequested();     // Ctrl+Alt+Shift+S

private:
    static void clipToWindow(HWND window);
    static bool grabNativeMouse(HWND window, bool on);
    MouseGrab mouseGrab_;

    bool releaseOnly_;
    moonlight::InputRouter router_;
    HWND window_ = nullptr;
    bool captured_ = false;
    bool listenForLocalKeys_ = false;
};

} // namespace veyra::ui
