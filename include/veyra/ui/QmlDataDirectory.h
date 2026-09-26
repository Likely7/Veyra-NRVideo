#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace veyra::ui {

struct QmlDataMigrationResult {
    bool ok = false;
    std::size_t copied = 0;
    std::string error;
};

// Copies known user settings once. The legacy directory is always read-only.
QmlDataMigrationResult prepareQmlDataDirectory(const std::filesystem::path& legacy,
                                               const std::filesystem::path& current);

} // namespace veyra::ui
