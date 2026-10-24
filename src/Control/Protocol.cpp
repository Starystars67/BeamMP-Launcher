/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Control/Protocol.h"

#include <ctime>

namespace Control {

namespace {
    constexpr size_t MaxNameLength = 64;
    constexpr int MaxNesting = 32;

    // Cheap pre-check so hostile input cannot make the parser recurse deeply.
    bool NestsTooDeeply(const std::string& line) {
        int depth = 0;
        bool inString = false;
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (inString) {
                if (c == '\\') {
                    ++i;
                } else if (c == '"') {
                    inString = false;
                }
            } else if (c == '"') {
                inString = true;
            } else if (c == '{' || c == '[') {
                if (++depth > MaxNesting) {
                    return true;
                }
            } else if (c == '}' || c == ']') {
                --depth;
            }
        }
        return false;
    }

    nlohmann::json Envelope(const char* type) {
        return { { "v", ProtocolVersion }, { "type", type } };
    }
}

std::optional<Request> ParseRequest(const std::string& line) {
    if (line.size() > MaxLineBytes || NestsTooDeeply(line)) {
        return std::nullopt;
    }
    nlohmann::json message = nlohmann::json::parse(line, nullptr, false);
    if (!message.is_object()) {
        return std::nullopt;
    }
    auto version = message.find("v");
    auto type = message.find("type");
    auto id = message.find("id");
    auto name = message.find("name");
    if (version == message.end() || !version->is_number_integer() || *version != ProtocolVersion
        || type == message.end() || *type != "request"
        || id == message.end() || !id->is_number_integer()
        || name == message.end() || !name->is_string()) {
        return std::nullopt;
    }
    Request request;
    request.id = id->get<int64_t>();
    request.name = name->get<std::string>();
    if (request.name.empty() || request.name.size() > MaxNameLength) {
        return std::nullopt;
    }
    auto data = message.find("data");
    if (data != message.end()) {
        if (!data->is_object()) {
            return std::nullopt;
        }
        request.data = *data;
    }
    return request;
}

std::string MakeResponse(int64_t id, const nlohmann::json& data) {
    nlohmann::json message = Envelope("response");
    message["id"] = id;
    message["ok"] = true;
    message["data"] = data;
    return message.dump();
}

std::string MakeError(std::optional<int64_t> id, const std::string& code, const std::string& message) {
    nlohmann::json response = Envelope("response");
    response["id"] = id ? nlohmann::json(*id) : nlohmann::json(nullptr);
    response["ok"] = false;
    response["error"] = { { "code", code }, { "message", message } };
    return response.dump();
}

std::string MakeEvent(const std::string& name, const nlohmann::json& data) {
    nlohmann::json message = Envelope("event");
    message["name"] = name;
    message["data"] = data;
    return message.dump();
}

bool TokenMatches(const std::string& expected, const std::string& given) {
    if (expected.size() != given.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        difference |= static_cast<unsigned char>(expected[i] ^ given[i]);
    }
    return difference == 0;
}

std::optional<int> NegotiateVersion(const nlohmann::json& offered) {
    if (!offered.is_array()) {
        return std::nullopt;
    }
    for (const auto& version : offered) {
        if (version.is_number_integer() && version == ProtocolVersion) {
            return ProtocolVersion;
        }
    }
    return std::nullopt;
}

std::string FormatTimestamp(std::chrono::system_clock::time_point time) {
    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
    time_t seconds = static_cast<time_t>(millis / 1000);
    int fraction = static_cast<int>(millis % 1000);
    if (fraction < 0) {
        fraction += 1000;
        --seconds;
    }
    tm utc {};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &utc);
    char result[40];
    std::snprintf(result, sizeof(result), "%s.%03dZ", buffer, fraction);
    return result;
}

}
