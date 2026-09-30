// SPDX-License-Identifier: GPL-3.0-only
// Where the client identity lives on disk: one DPAPI-protected file in
// %LOCALAPPDATA%\Veyra\moonlight, written atomically (temp file + replace),
// the same approach as the PS5 pairing profiles. The private key never leaves
// this file and is never logged.
#pragma once
#include <filesystem>
#include <optional>

#include "veyra/moonlight/Crypto.h"

namespace veyra::moonlight {

std::filesystem::path dataDirectory();

// nullopt when the file is missing, unreadable (another Windows user, damaged)
// or holds an identity that does not validate.
std::optional<Identity> loadIdentity(const std::filesystem::path& file);
bool saveIdentity(const std::filesystem::path& file, const Identity& identity);

// The saved identity, or a freshly created one that is saved first. `created`
// (optional) reports which. Throws std::runtime_error if a new identity cannot be saved.
Identity loadOrCreateIdentity(const std::filesystem::path& file, bool* created = nullptr);

} // namespace veyra::moonlight
