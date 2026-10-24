/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Net/Download.h"

#include <algorithm>
#include <curl/curl.h>
#include <cstdio>

namespace Net {

namespace fs = std::filesystem;

namespace {
    struct Transfer {
        FILE* file = nullptr;
        uint64_t written = 0;
        uint64_t maxBytes = 0;
        bool tooBig = false;
        bool cancelled = false;
        const DownloadOptions* options = nullptr;
    };

    size_t Write(char* data, size_t size, size_t count, void* target) {
        auto* transfer = static_cast<Transfer*>(target);
        size_t bytes = size * count;
        if (transfer->written + bytes > transfer->maxBytes) {
            transfer->tooBig = true;
            return 0; // aborts
        }
        if (std::fwrite(data, 1, bytes, transfer->file) != bytes) {
            return 0;
        }
        transfer->written += bytes;
        return bytes;
    }

    int Progress(void* target, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
        auto* transfer = static_cast<Transfer*>(target);
        if (transfer->options->progress && !transfer->options->progress(static_cast<uint64_t>(std::max<curl_off_t>(now, 0)), static_cast<uint64_t>(std::max<curl_off_t>(total, 0)))) {
            transfer->cancelled = true;
            return 1;
        }
        return 0;
    }

    // localhost, ::1, or an IPv4 address of 127.0.0.0/8, written out in full (nothing after it).
    bool IsLoopback(const std::string& host) {
        if (host == "localhost" || host == "[::1]") {
            return true;
        }
        unsigned a = 0, b = 0, c = 0, d = 0;
        int used = 0;
        if (std::sscanf(host.c_str(), "%u.%u.%u.%u%n", &a, &b, &c, &d, &used) != 4 || static_cast<size_t>(used) != host.size()) {
            return false;
        }
        return a == 127 && b < 256 && c < 256 && d < 256;
    }
}

bool DownloadUrlAllowed(const std::string& url, bool allowLoopbackHttp) {
    if (url.rfind("https://", 0) == 0) {
        return url.size() > 8;
    }
    if (allowLoopbackHttp && url.rfind("http://", 0) == 0) {
        std::string rest = url.substr(7);
        std::string authority = rest.substr(0, rest.find_first_of("/?#"));
        if (authority.find('@') != std::string::npos || authority.find('\\') != std::string::npos) {
            return false; // "127.0.0.1@elsewhere" is elsewhere
        }
        std::string host = authority;
        if (!host.empty() && host[0] == '[') {
            host = host.substr(0, host.find(']') + 1);
        } else {
            host = host.substr(0, host.find(':'));
        }
        return IsLoopback(host);
    }
    return false;
}

DownloadResult DownloadFile(const std::string& url, const fs::path& destination, const DownloadOptions& options) {
    DownloadResult result;
    if (!DownloadUrlAllowed(url, options.allowLoopbackHttp)) {
        result.error = "Only https downloads are allowed";
        return result;
    }
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    fs::path partial = destination;
    partial += ".part";
    Transfer transfer;
    transfer.maxBytes = options.maxBytes;
    transfer.options = &options;
    transfer.file = std::fopen(partial.string().c_str(), "wb");
    if (!transfer.file) {
        result.error = "Cannot write " + partial.string();
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(transfer.file);
        fs::remove(partial, ec);
        result.error = "curl could not be initialised";
        return result;
    }
    char errorBuffer[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, options.allowLoopbackHttp ? "http,https" : "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, static_cast<long>(options.connectTimeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>(options.stalledSeconds));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "beammp-launcher");
    CURLcode code = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    std::fclose(transfer.file);

    if (code != CURLE_OK) {
        fs::remove(partial, ec);
        result.cancelled = transfer.cancelled;
        if (transfer.cancelled) {
            result.error = "Cancelled";
        } else if (transfer.tooBig) {
            result.error = "The download is larger than allowed";
        } else {
            result.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
        }
        return result;
    }
    fs::rename(partial, destination, ec);
    if (ec) {
        fs::remove(partial, ec);
        result.error = "Cannot move the download into place";
        return result;
    }
    result.ok = true;
    return result;
}

}
