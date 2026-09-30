#include "veyra/ui/QmlDataDirectory.h"

#include <array>
#include <fstream>
#include <string_view>

namespace veyra::ui {
namespace {

constexpr std::array<std::wstring_view, 9> kLegacyFiles{
    L"presets.v1", L"user-presets.v1", L"nr-presets.v1", L"last-applied.v1",
    L"ui-session.v1", L"ui-preferences.v1", L"capture-preferences.v1",
    L"color-looks.v1", L"veyra.ini"
};

bool copyOnce(const std::filesystem::path& source, const std::filesystem::path& target,
              bool& copied, std::string& error) {
    std::error_code ec;
    copied = false;
    if (!std::filesystem::exists(source, ec)) {
        if (ec) { error = "Cannot inspect legacy setting: " + ec.message(); return false; }
        return true;
    }
    if (!std::filesystem::is_regular_file(source, ec)) {
        error = ec ? "Cannot inspect legacy setting: " + ec.message() : "Legacy setting is not a regular file";
        return false;
    }
    if (std::filesystem::exists(target, ec)) return !ec;
    if (ec) { error = "Cannot inspect new setting: " + ec.message(); return false; }

    auto temporary = target;
    temporary += L".migrating";
    std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) { error = "Cannot copy legacy setting: " + ec.message(); return false; }
    std::filesystem::rename(temporary, target, ec);
    if (ec) { error = "Cannot install migrated setting: " + ec.message(); return false; }
    copied = true;
    return true;
}

} // namespace

QmlDataMigrationResult prepareQmlDataDirectory(const std::filesystem::path& legacy,
                                               const std::filesystem::path& current) {
    QmlDataMigrationResult result;
    std::error_code ec;
    std::filesystem::create_directories(current, ec);
    if (ec) { result.error = "Cannot create 2.0.0 data directory: " + ec.message(); return result; }
    if (std::filesystem::equivalent(legacy, current, ec) && !ec) {
        result.error = "The legacy and 2.0.0 data directories are identical";
        return result;
    }

    const auto marker = current / L"migration-from-1.4.4.v1";
    if (std::filesystem::exists(marker, ec)) { result.ok = !ec; return result; }
    if (ec) { result.error = "Cannot inspect migration marker: " + ec.message(); return result; }

    for (const auto name : kLegacyFiles) {
        const auto source = legacy / std::filesystem::path(std::wstring(name));
        const auto target = current / std::filesystem::path(std::wstring(name));
        bool copied = false;
        if (!copyOnce(source, target, copied, result.error)) return result;
        if (copied) ++result.copied;
    }

    auto temporary = marker;
    temporary += L".migrating";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << "VEYRA_QML_DATA_MIGRATION 1\n";
        if (!out) { result.error = "Cannot write migration marker"; return result; }
    }
    std::filesystem::rename(temporary, marker, ec);
    if (ec) { result.error = "Cannot install migration marker: " + ec.message(); return result; }
    result.ok = true;
    return result;
}

} // namespace veyra::ui
