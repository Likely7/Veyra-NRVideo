#include "veyra/RuntimePaths.h"
#include "veyra/base/PortableLayout.h"

#include <windows.h>

namespace veyra::runtime {
namespace {
std::filesystem::path executableDirectory()
{
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    if (length == 0 || length >= std::size(path)) return std::filesystem::current_path();
    return std::filesystem::path(path).parent_path();
}

const std::filesystem::path& managedRoot()
{
    static const auto root = portable::rootFromVersionDirectory(executableDirectory());
    return root;
}
}

std::filesystem::path applicationRoot()
{
    auto candidate = executableDirectory();
    if (!managedRoot().empty()) return candidate;
    for (unsigned depth = 0; depth != 6; ++depth) {
        if (std::filesystem::exists(candidate / "runtime_local")) return candidate;
        const auto parent = candidate.parent_path();
        if (parent.empty() || parent == candidate) break;
        candidate = parent;
    }
    return executableDirectory();
}

std::filesystem::path shaderDirectory()
{
    const auto besideExecutable = executableDirectory() / "shaders";
    if (std::filesystem::exists(besideExecutable)) return besideExecutable;
    return applicationRoot() / "shaders";
}

std::filesystem::path localRuntimeDirectory() {
    const auto root=applicationRoot();
    return std::filesystem::exists(root/"runtime")?root/"runtime"/"experimental":root/"runtime_local"/"nvidia";
}
std::filesystem::path localDataDirectory() { return applicationRoot() / "runtime_local"; }
std::filesystem::path userDataDirectory() {
    const auto& root = managedRoot();
    return root.empty() ? localDataDirectory() : root / "runtime_local";
}
std::filesystem::path logsDirectory() {
    const auto& root = managedRoot();
    return (root.empty() ? applicationRoot() : root) / "logs";
}

} // namespace veyra::runtime
