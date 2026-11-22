#pragma once

#include <windows.h>

namespace veyra::source {

struct CaptureGraphStart {
    HRESULT initial;
    HRESULT result;
    bool retried = false;
};

template<class Run, class DetachAudio>
CaptureGraphStart startCaptureGraph(bool hasDirectShowAudio, Run run, DetachAudio detachAudio) {
    const HRESULT initial = run();
    if (FAILED(initial) && hasDirectShowAudio && detachAudio()) return {initial, run(), true};
    return {initial, initial};
}

}
