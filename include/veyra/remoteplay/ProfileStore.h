#pragma once
#include "ChiakiBackend.h"
#include <filesystem>
#include <optional>
namespace veyra::remoteplay {
// Independent of the executable/runtime directory. Throws if Windows cannot
// resolve the current user's data folder; never silently falls back to cwd.
// An absolute VEYRA_REMOTEPLAY_PROFILE_DIR provides an isolated test/portable profile.
std::filesystem::path profileDirectory();
struct ProfileMigration { unsigned imported=0, failed=0; };
ProfileMigration migrateProfiles(const std::filesystem::path& legacyRoot);
ProfileMigration migrateProfilesTo(const std::filesystem::path& legacyRoot,const std::filesystem::path& destination);
// Entire profile is protected for the current Windows user. Never serialize
// credentials to INI/JSON, command lines or diagnostic output.
bool saveProfile(const std::filesystem::path&, const NativeConnectRequest&);
enum class ProfileLoadError { None, Missing, Read, Decrypt, Format };
std::optional<NativeConnectRequest> loadProfile(const std::filesystem::path&,ProfileLoadError* error=nullptr);
}
