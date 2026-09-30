#include "veyra/ui/QmlDataDirectory.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
void write(const std::filesystem::path& path, const char* contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "usage: veyra_qml_data_tests <new scratch directory>\n"; return 2; }
    const std::filesystem::path root(argv[1]);
    if (std::filesystem::exists(root)) { std::cerr << "scratch directory already exists\n"; return 2; }
    const auto old = root / "legacy";
    const auto current = root / "current";
    std::filesystem::create_directories(old);
    write(old / "user-presets.v1", "original preset");
    write(old / "ui-session.v1", "original session");
    write(old / "private-key.txt", "do not copy");

    const auto first = veyra::ui::prepareQmlDataDirectory(old, current);
    if (!first.ok) std::cerr << "migration error: " << first.error << '\n';
    bool ok = check(first.ok && first.copied == 2, "first migration copies two allowlisted files");
    ok &= check(read(current / "user-presets.v1") == "original preset", "preset copied");
    ok &= check(!std::filesystem::exists(current / "private-key.txt"), "unknown file excluded");
    write(current / "user-presets.v1", "edited in 2.0.0");
    write(old / "ui-session.v1", "edited in 1.4.4");
    const auto second = veyra::ui::prepareQmlDataDirectory(old, current);
    if (!second.ok) std::cerr << "second migration error: " << second.error << '\n';
    ok &= check(second.ok && second.copied == 0, "second launch does not migrate again");
    ok &= check(read(current / "user-presets.v1") == "edited in 2.0.0", "new preset preserved");
    ok &= check(read(current / "ui-session.v1") == "original session", "old edit cannot rewrite 2.0.0");
    ok &= check(read(old / "user-presets.v1") == "original preset", "1.4.4 file untouched");
    std::cout << (ok ? "PASS" : "FAIL") << " qml data migration\n";
    return ok ? 0 : 1;
}
