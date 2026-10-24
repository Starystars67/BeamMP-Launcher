/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Net {

struct UnzipLimits {
    uint64_t maxTotalBytes = 1024ull * 1024 * 1024;
    uint64_t maxFileBytes = 512ull * 1024 * 1024;
    size_t maxFiles = 5000;
};

struct UnzipResult {
    bool ok = false;
    std::string error;
    /// What was written, relative to the destination with forward slashes (files only).
    std::vector<std::string> files;
};

/// Whether an entry name may be written under a destination: relative, no `..`, no drive or root, no empty parts.
/// Backslashes count as separators (zips made on Windows). The cleaned name is returned in `cleaned`.
bool SafeArchivePath(const std::string& name, std::string& cleaned);

/// Extracts the whole archive below `destination`. An entry that would land elsewhere (`../x`, `/x`, `C:\x`), a
/// link, or more than the limits allow ends it with an error (files written so far stay, `files` lists them).
/// `progress(done, total)` counts entries.
UnzipResult Unzip(const std::filesystem::path& archive, const std::filesystem::path& destination, const UnzipLimits& limits = {},
    const std::function<void(size_t done, size_t total)>& progress = {});

/// Writes a zip of the given (name, bytes) entries; names are taken as they are (for tests and packaging).
bool CreateZip(const std::filesystem::path& archive, const std::vector<std::pair<std::string, std::string>>& entries);

}
