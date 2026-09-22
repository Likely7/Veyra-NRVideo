#pragma once
#include <windows.h>
#include <functional>
#include "veyra/source/RemotePlaySource.h"
#include "CaptureConfigBlock.h"
namespace veyra::ui {
struct RemotePlayPanelStatus {std::wstring message;bool active=false;};
// start launches a normal Remote Play session. startWithCapture launches the
// combined mode: a non-empty capturePath means "control the PS5 through the
// Remote Play link but render the capture card"; an empty path must fall back
// to start's behaviour. captureSettings carries the engine live-setting pairs
// the embedded capture configuration block drives (same contract as the
// standalone capture popup).
void showRemotePlayPanel(HWND,
    std::function<void(source::RemotePlayConnectDesc)> start,
    std::function<void(source::RemotePlayConnectDesc,const std::wstring& capturePath)> startWithCapture,
    std::function<void(std::string)>,std::function<RemotePlayPanelStatus()>,std::function<void()>,std::function<bool()>,
    CaptureConfigBlock::Callbacks captureSettings);
}
