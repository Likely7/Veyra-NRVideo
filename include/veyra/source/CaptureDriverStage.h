#pragma once

namespace veyra::source {
// Graph operations run on the owner thread and may block inside a driver.
enum class CaptureDriverStage { None, Stop, Enumerate, Configure, Run, Release };
}
