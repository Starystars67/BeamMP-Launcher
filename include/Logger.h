/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once
#include <chrono>
#include <cstddef>
#include <functional>
#include <iostream>
#include <string>

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    Except,
};

struct LogRecord {
    LogLevel level;
    std::chrono::system_clock::time_point time;
    std::string message;
};

// Called under the logger lock for every record, debug included: must be quick and must not log.
using LogSink = std::function<void(const LogRecord&)>;

using LogSinkId = size_t;

LogSinkId AddLogSink(LogSink sink);
void RemoveLogSink(LogSinkId id);
void InitLog();
void except(const std::string& toPrint);
void fatal(const std::string& toPrint);
void debug(const std::string& toPrint);
void error(const std::string& toPrint);
void info(const std::string& toPrint);
void warn(const std::string& toPrint);

void except(const std::wstring& toPrint);
void fatal(const std::wstring& toPrint);
void debug(const std::wstring& toPrint);
void error(const std::wstring& toPrint);
void info(const std::wstring& toPrint);
void warn(const std::wstring& toPrint);
std::string getDate();
