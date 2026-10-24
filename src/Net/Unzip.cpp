/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Net/Unzip.h"

#include "zip_file.h"
#include <fstream>

namespace Net {

namespace fs = std::filesystem;

bool SafeArchivePath(const std::string& name, std::string& cleaned) {
    cleaned.clear();
    if (name.empty() || name.find('\0') != std::string::npos) {
        return false;
    }
    std::string path = name;
    for (char& c : path) {
        if (c == '\\') {
            c = '/';
        }
    }
    if (path[0] == '/' || (path.size() >= 2 && path[1] == ':')) {
        return false; // root or drive
    }
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t end = path.find('/', start);
        std::string part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        bool last = end == std::string::npos;
        if (part == "..") {
            return false;
        }
        if (part.empty()) {
            if (!last) {
                return false; // "a//b"
            }
            // a trailing slash marks a directory
        } else if (part != ".") {
            parts.push_back(part);
        }
        if (last) {
            break;
        }
        start = end + 1;
    }
    if (parts.empty()) {
        return false;
    }
    for (const auto& part : parts) {
        if (!cleaned.empty()) {
            cleaned += '/';
        }
        cleaned += part;
    }
    return true;
}

bool CreateZip(const fs::path& archive, const std::vector<std::pair<std::string, std::string>>& entries) {
    try {
        miniz_cpp::zip_file zip;
        for (const auto& [name, bytes] : entries) {
            zip.writestr(name, bytes);
        }
        zip.save(archive.string());
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

UnzipResult Unzip(const fs::path& archive, const fs::path& destination, const UnzipLimits& limits,
    const std::function<void(size_t, size_t)>& progress) {
    UnzipResult result;
    std::vector<miniz_cpp::zip_info> entries;
    miniz_cpp::zip_file zip;
    try {
        zip.load(archive.string());
        entries = zip.infolist();
    } catch (const std::exception& e) {
        result.error = std::string("Not a usable archive: ") + e.what();
        return result;
    }
    if (entries.size() > limits.maxFiles) {
        result.error = "The archive has too many entries";
        return result;
    }
    uint64_t total = 0;
    for (const auto& entry : entries) {
        total += entry.file_size;
        if (entry.file_size > limits.maxFileBytes || total > limits.maxTotalBytes) {
            result.error = "The archive is larger than allowed when unpacked";
            return result;
        }
    }

    std::error_code ec;
    fs::create_directories(destination, ec);
    size_t done = 0;
    for (const auto& entry : entries) {
        std::string cleaned;
        if (!SafeArchivePath(entry.filename, cleaned)) {
            result.error = "Unsafe entry in the archive: " + entry.filename;
            return result;
        }
        // an entry made on a Unix system with the link type in its attributes
        uint32_t mode = entry.external_attr >> 16;
        if (entry.create_system == 3 && (mode & 0170000) == 0120000) {
            result.error = "The archive contains a link: " + entry.filename;
            return result;
        }
        fs::path target = destination / fs::path(cleaned);
        bool directory = entry.filename.back() == '/' || entry.filename.back() == '\\';
        if (directory) {
            fs::create_directories(target, ec);
        } else {
            fs::create_directories(target.parent_path(), ec);
            std::string bytes;
            try {
                bytes = zip.read(entry);
            } catch (const std::exception& e) {
                result.error = "Cannot unpack " + entry.filename + ": " + e.what();
                return result;
            }
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            out.close();
            if (!out) {
                result.error = "Cannot write " + target.string();
                return result;
            }
            result.files.push_back(cleaned);
        }
        if (progress) {
            progress(++done, entries.size());
        }
    }
    result.ok = true;
    return result;
}

}
