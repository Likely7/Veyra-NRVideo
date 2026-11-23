// SPDX-License-Identifier: GPL-3.0-only
// Interface of the Xbox transport engine: the reference Chromium player inside
// WebView2 exports its encoded frames to XboxWebSource. The engine, UI and
// enhancement chain only ever see this interface, so the transport cannot
// affect super resolution / frame generation.
#pragma once
#include <cstdint>

#include "veyra/remoteplay/Types.h"
#include "veyra/sink/CaptureAudioSession.h"
#include "veyra/source/IFrameSource.h"
#include "veyra/source/XboxStats.h"

namespace veyra::source {

class IXboxSource : public IFrameSource {
public:
    virtual bool connect(XboxConnectDesc desc) = 0;
    virtual void cancel() = 0;
    virtual XboxStats stats() const = 0;
    virtual uint64_t skipped() const = 0;
    virtual uint64_t unitsReceived() const = 0;
    virtual sink::CaptureAudioState audioState() const = 0;
    virtual void setAudioGain(float value) = 0;
    virtual void setAudioSync(unsigned mode, int offset) = 0;
    virtual void videoPresented(double ptsMs, int64_t host100ns, bool sourceFrame = true) = 0;
    virtual void videoReset(bool resetAudio = true) = 0;
    virtual void controller(const remoteplay::ControllerState& state) = 0;
    virtual remoteplay::ControllerFeedback takeFeedback() = 0;
};

} // namespace veyra::source