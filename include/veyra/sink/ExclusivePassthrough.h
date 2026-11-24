#pragma once

namespace veyra::sink {
// Set while a bitstream passthrough stream holds a render endpoint exclusively (XCAP Dolby passthrough). The PCM
// capture session then waits for its release instead of failing with AUDCLNT_E_DEVICE_IN_USE twice a second.
void setExclusivePassthroughActive(bool active);
bool exclusivePassthroughActive();
} // namespace veyra::sink
