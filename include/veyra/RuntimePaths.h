#pragma once

#include <filesystem>
#include <string>

namespace veyra::runtime {

std::filesystem::path applicationRoot();
std::filesystem::path shaderDirectory();
std::filesystem::path localRuntimeDirectory();
std::filesystem::path localDataDirectory();
// Writable presets and LUTs are shared by a managed ZIP's installed versions.
std::filesystem::path userDataDirectory();
std::filesystem::path logsDirectory();
// UTF-8 text of a path for logs and UTF-8 APIs. Never throws, unlike
// path::string(), which converts through the ANSI code page and throws for
// characters it cannot represent (a CJK folder on a Polish system, say).
std::string pathUtf8(const std::filesystem::path& path);

} // namespace veyra::runtime
