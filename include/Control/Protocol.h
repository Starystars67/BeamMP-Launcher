/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace Control {

constexpr int ProtocolVersion = 1;
constexpr size_t MaxLineBytes = 1024 * 1024;
constexpr size_t MaxHelloBytes = 4096;

struct Request {
    int64_t id = 0;
    std::string name;
    nlohmann::json data = nlohmann::json::object();
};

/// Thrown by request handlers; becomes an error response.
class RequestError : public std::exception {
public:
    RequestError(std::string code, std::string message)
        : m_code(std::move(code))
        , m_message(std::move(message)) { }
    const char* what() const noexcept override { return m_message.c_str(); }
    const std::string& code() const { return m_code; }

private:
    std::string m_code;
    std::string m_message;
};

/// Returns nothing unless the line is a well-formed version 1 request.
std::optional<Request> ParseRequest(const std::string& line);

std::string MakeResponse(int64_t id, const nlohmann::json& data);
std::string MakeError(std::optional<int64_t> id, const std::string& code, const std::string& message);
std::string MakeEvent(const std::string& name, const nlohmann::json& data);

/// Compares in time independent of where the strings differ.
bool TokenMatches(const std::string& expected, const std::string& given);

/// Highest version offered by the client that this engine speaks.
std::optional<int> NegotiateVersion(const nlohmann::json& offered);

/// ISO 8601, UTC, millisecond precision.
std::string FormatTimestamp(std::chrono::system_clock::time_point time);

}
