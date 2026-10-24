/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace Net {

struct DownloadOptions {
    /// More than this ends the transfer.
    uint64_t maxBytes = 512ull * 1024 * 1024;
    /// Plain http is refused (a download that is then run must not be changeable on the way), except to this machine
    /// when this is set (tests).
    bool allowLoopbackHttp = false;
    /// Called while data arrives with what is there and the total (0 when unknown); false cancels.
    std::function<bool(uint64_t done, uint64_t total)> progress;
    int connectTimeoutSeconds = 15;
    /// A transfer that stays under a few KB/s this long is given up.
    int stalledSeconds = 30;
};

struct DownloadResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
};

/// Downloads to `destination` (written as `<destination>.part` and moved into place when complete, so a broken
/// transfer never leaves a file that looks finished). https only, redirects only to https.
DownloadResult DownloadFile(const std::string& url, const std::filesystem::path& destination, const DownloadOptions& options = {});

/// Whether the url is one DownloadFile accepts.
bool DownloadUrlAllowed(const std::string& url, bool allowLoopbackHttp);

}
