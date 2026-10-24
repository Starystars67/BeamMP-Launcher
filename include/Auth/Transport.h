/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Auth {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    long timeoutSeconds = 30;
};

struct HttpResponse {
    /// False when no HTTP answer arrived at all (DNS, connection, TLS, timeout).
    bool completed = false;
    long status = 0;
    std::string body;
    std::string error;
};

/// Sends one HTTP request. Swapped for a fake in tests.
class Transport {
public:
    virtual ~Transport() = default;
    virtual HttpResponse Send(const HttpRequest& request) = 0;
};

/// libcurl with certificate verification on, no redirects, answers capped at 1 MiB.
std::unique_ptr<Transport> MakeCurlTransport();

}
