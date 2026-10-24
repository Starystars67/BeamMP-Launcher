/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Auth/Transport.h"

#include <algorithm>
#include <curl/curl.h>

namespace Auth {

namespace {
    constexpr size_t MaxResponseBytes = 1024 * 1024;

    size_t Collect(char* data, size_t size, size_t count, void* target) {
        auto* body = static_cast<std::string*>(target);
        size_t bytes = size * count;
        if (body->size() + bytes > MaxResponseBytes) {
            return 0; // aborts the transfer
        }
        body->append(data, bytes);
        return bytes;
    }

    class CurlTransport : public Transport {
    public:
        HttpResponse Send(const HttpRequest& request) override {
            HttpResponse response;
            CURL* curl = curl_easy_init();
            if (!curl) {
                response.error = "curl could not be initialised";
                return response;
            }

            char errorBuffer[CURL_ERROR_SIZE] = {};
            curl_slist* headers = nullptr;
            for (const auto& [name, value] : request.headers) {
                headers = curl_slist_append(headers, (name + ": " + value).c_str());
            }

            curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
            if (request.method != "GET") {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Collect);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeoutSeconds);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, std::min(request.timeoutSeconds, 15L));
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");

            CURLcode result = curl_easy_perform(curl);
            if (result == CURLE_OK) {
                response.completed = true;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
            } else {
                response.body.clear();
                response.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(result);
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return response;
        }
    };
}

std::unique_ptr<Transport> MakeCurlTransport() {
    return std::make_unique<CurlTransport>();
}

}
