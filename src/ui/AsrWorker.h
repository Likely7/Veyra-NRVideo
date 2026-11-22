// SPDX-License-Identifier: GPL-3.0-only
#pragma once

namespace veyra::ui::asr {
int runWorker(unsigned testDecodeDelayMs = 0);
int runFile(int argc, wchar_t** argv);
}
