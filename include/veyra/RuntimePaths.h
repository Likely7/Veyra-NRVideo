#pragma once

#include <filesystem>

namespace veyra::runtime {

std::filesystem::path applicationRoot();
std::filesystem::path shaderDirectory();
std::filesystem::path localRuntimeDirectory();
std::filesystem::path localDataDirectory();
// Writable presets and LUTs are shared by a managed ZIP's installed versions.
std::filesystem::path userDataDirectory();
std::filesystem::path logsDirectory();

} // namespace veyra::runtime
