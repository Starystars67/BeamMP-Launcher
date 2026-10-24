/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Games/GearBlocksManifest.h"

#include "Net/Base64.h"
#include "Net/Download.h"
#include "Net/Ed25519.h"
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Games {

namespace {
    constexpr size_t MaxReleases = 64;

    bool HexSha256(std::string& value) {
        if (value.size() != 64) {
            return false;
        }
        for (char& c : value) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                return false;
            }
        }
        return true;
    }

    std::vector<uint64_t> Groups(const std::string& text) {
        std::vector<uint64_t> groups;
        size_t start = 0;
        while (start <= text.size()) {
            size_t end = text.find('.', start);
            std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            groups.push_back(std::stoull(part));
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return groups;
    }

    // The member must be a string; the message names it.
    bool ReadString(const nlohmann::json& object, const char* name, std::string& value, std::string& error) {
        auto it = object.find(name);
        if (it == object.end() || !it->is_string()) {
            error = std::string("The manifest is incomplete (\"") + name + "\")";
            return false;
        }
        value = it->get<std::string>();
        return true;
    }

    bool ReadFile(const nlohmann::json& parent, const char* name, bool allowLoopbackHttp, bool allowBundle, ManifestFile& file, std::string& error) {
        auto it = parent.find(name);
        if (it == parent.end() || !it->is_object()) {
            error = std::string("The manifest is incomplete (\"") + name + "\")";
            return false;
        }
        if (!ReadString(*it, "url", file.url, error) || !ReadString(*it, "sha256", file.sha256, error)) {
            return false;
        }
        bool bundled = allowBundle && GearBlocksManifest::BundleName(file.url).has_value();
        if (!bundled && !Net::DownloadUrlAllowed(file.url, allowLoopbackHttp)) {
            error = std::string("The manifest's address for \"") + name + "\" is not https";
            return false;
        }
        if (!HexSha256(file.sha256)) {
            error = std::string("The manifest's checksum for \"") + name + "\" is not a SHA-256";
            return false;
        }
        return true;
    }
}

bool PlainVersion(const std::string& text) {
    if (text.empty() || text.size() > 32 || text.front() == '.' || text.back() == '.' || text.find("..") != std::string::npos) {
        return false;
    }
    if (!std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.'; })) {
        return false;
    }
    size_t groups = static_cast<size_t>(std::count(text.begin(), text.end(), '.')) + 1;
    if (groups > 6) {
        return false;
    }
    // every group fits a 64 bit number
    size_t run = 0;
    for (char c : text) {
        run = c == '.' ? 0 : run + 1;
        if (run > 12) {
            return false;
        }
    }
    return true;
}

bool VersionNewer(const std::string& a, const std::string& b) {
    if (!PlainVersion(a) || !PlainVersion(b)) {
        return false;
    }
    auto left = Groups(a);
    auto right = Groups(b);
    size_t count = std::max(left.size(), right.size());
    left.resize(count, 0);
    right.resize(count, 0);
    return left > right;
}

std::optional<std::string> GearBlocksManifest::BundleName(const std::string& url) {
    static const std::string Prefix = "bundle:";
    if (url.rfind(Prefix, 0) != 0) {
        return std::nullopt;
    }
    std::string name = url.substr(Prefix.size());
    if (name.empty() || name.size() > 128 || name == "." || name == ".." || name.find_first_of("/\\:\0") != std::string::npos
        || name.find('\0') != std::string::npos) {
        return std::nullopt;
    }
    return name;
}

std::optional<GearBlocksManifest> GearBlocksManifest::Parse(const std::string& text, bool allowLoopbackHttp, std::string& error, bool allowBundle) {
    nlohmann::json json = nlohmann::json::parse(text, nullptr, false);
    if (!json.is_object()) {
        error = "The manifest is not valid JSON";
        return std::nullopt;
    }
    GearBlocksManifest manifest;
    try {
        if (json.value("schema", 0) != 1) {
            error = "The manifest is of a kind this launcher does not know (update the launcher)";
            return std::nullopt;
        }
        if (json.value("game", std::string()) != "gearblocks") {
            error = "The manifest is not for GearBlocks";
            return std::nullopt;
        }
        auto sequence = json.find("sequence");
        if (sequence == json.end() || !sequence->is_number_unsigned()) {
            error = "The manifest is incomplete (\"sequence\")";
            return std::nullopt;
        }
        manifest.sequence = sequence->get<uint64_t>();

        auto loader = json.find("loader");
        if (loader == json.end() || !loader->is_object()) {
            error = "The manifest is incomplete (\"loader\")";
            return std::nullopt;
        }
        if (!ReadString(*loader, "version", manifest.loaderVersion, error) || manifest.loaderVersion.empty() || manifest.loaderVersion.size() > 64) {
            error = error.empty() ? "The manifest's loader version is not usable" : error;
            return std::nullopt;
        }
        auto wrapper = nlohmann::json { { "loader", *loader } };
        if (!ReadFile(wrapper, "loader", allowLoopbackHttp, allowBundle, manifest.loader, error)) {
            return std::nullopt;
        }

        auto releases = json.find("releases");
        if (releases == json.end() || !releases->is_array() || releases->empty() || releases->size() > MaxReleases) {
            error = "The manifest has no releases";
            return std::nullopt;
        }
        for (const auto& entry : *releases) {
            if (!entry.is_object()) {
                error = "The manifest has a release that is not an object";
                return std::nullopt;
            }
            ManifestRelease release;
            if (!ReadString(entry, "version", release.version, error)) {
                return std::nullopt;
            }
            if (!PlainVersion(release.version)) {
                error = "The manifest has a release version that is not a version";
                return std::nullopt;
            }
            auto protocol = entry.find("bridge_protocol");
            if (protocol == entry.end() || !protocol->is_number_integer() || protocol->get<int64_t>() < 1 || protocol->get<int64_t>() > 100000) {
                error = "The manifest is incomplete (\"bridge_protocol\")";
                return std::nullopt;
            }
            release.bridgeProtocol = static_cast<int>(protocol->get<int64_t>());
            auto builds = entry.find("game_builds");
            if (builds == entry.end() || !builds->is_array() || builds->empty() || builds->size() > 256) {
                error = "The manifest is incomplete (\"game_builds\")";
                return std::nullopt;
            }
            for (const auto& build : *builds) {
                if (!build.is_string() || !PlainVersion(build.get<std::string>())) {
                    error = "The manifest lists a game build that is not a version";
                    return std::nullopt;
                }
                release.gameBuilds.push_back(build.get<std::string>());
            }
            if (!ReadFile(entry, "plugin", allowLoopbackHttp, allowBundle, release.plugin, error) || !ReadFile(entry, "mod", allowLoopbackHttp, allowBundle, release.mod, error)) {
                return std::nullopt;
            }
            manifest.releases.push_back(std::move(release));
        }
    } catch (const nlohmann::json::exception& e) {
        error = std::string("The manifest is not usable: ") + e.what();
        return std::nullopt;
    }
    return manifest;
}

GearBlocksManifest::Pick GearBlocksManifest::Select(const std::string& gameBuild, const ManifestRelease*& release) const {
    release = nullptr;
    bool forBuild = false;
    for (const auto& candidate : releases) {
        bool buildMatches = gameBuild.empty() || std::find(candidate.gameBuilds.begin(), candidate.gameBuilds.end(), gameBuild) != candidate.gameBuilds.end();
        if (!buildMatches) {
            continue;
        }
        forBuild = true;
        if (candidate.bridgeProtocol != GearBlocksBridgeProtocol) {
            continue;
        }
        if (!release || VersionNewer(candidate.version, release->version)) {
            release = &candidate;
        }
    }
    if (release) {
        return Pick::Found;
    }
    return forBuild ? Pick::LauncherTooOld : Pick::UnsupportedBuild;
}

bool VerifyManifestSignature(const std::string& manifestText, const std::string& signatureText, const std::vector<std::string>& trustedKeys) {
    if (trustedKeys.empty()) {
        return false;
    }
    std::vector<std::string> signatures;
    size_t start = 0;
    while (start <= signatureText.size()) {
        size_t end = signatureText.find('\n', start);
        std::string line = signatureText.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? signatureText.size() + 1 : end + 1;
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
            line.pop_back();
        }
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        auto raw = Net::Base64Decode(line.substr(first));
        if (!raw || raw->size() != 64) {
            return false;
        }
        signatures.push_back(*raw);
        if (signatures.size() > 8) {
            return false;
        }
    }
    for (const auto& signature : signatures) {
        for (const auto& key : trustedKeys) {
            if (Net::Ed25519Verify(key, manifestText, signature)) {
                return true;
            }
        }
    }
    return false;
}

}
