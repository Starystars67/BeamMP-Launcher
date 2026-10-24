/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/


#include "Logger.h"
#include "Startup.h"
#include "Utils.h"
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include "Options.h"

static std::string FormatDate(std::chrono::system_clock::time_point time) {
    time_t tt = std::chrono::system_clock::to_time_t(time);
    tm local_tm {};
#if defined(_WIN32)
    localtime_s(&local_tm, &tt);
#else
    localtime_r(&tt, &local_tm);
#endif
    std::stringstream date;
    int S = local_tm.tm_sec;
    int M = local_tm.tm_min;
    int H = local_tm.tm_hour;
    std::string Secs = (S > 9 ? std::to_string(S) : "0" + std::to_string(S));
    std::string Min = (M > 9 ? std::to_string(M) : "0" + std::to_string(M));
    std::string Hour = (H > 9 ? std::to_string(H) : "0" + std::to_string(H));
    date
        << "["
        << local_tm.tm_mday << "/"
        << local_tm.tm_mon + 1 << "/"
        << local_tm.tm_year + 1900 << " "
        << Hour << ":"
        << Min << ":"
        << Secs
        << "] ";
    return date.str();
}
std::string getDate() {
    return FormatDate(std::chrono::system_clock::now());
}
void InitLog() {
    std::ofstream LFS;
    LFS.open(GetEP() + beammp_wide("Launcher.log"));
    if (!LFS.is_open()) {
        error("logger file init failed!");
    } else
        LFS.close();
}
void addToLog(const std::string& Line) {
    std::ofstream LFS;
    LFS.open(GetEP() + beammp_wide("Launcher.log"), std::ios_base::app);
    LFS << Line.c_str();
    LFS.close();
}
void addToLog(const std::wstring& Line) {
    std::wofstream LFS;
    LFS.open(GetEP() + beammp_wide("Launcher.log"), std::ios_base::app);
    LFS << Line.c_str();
    LFS.close();
}

namespace {
std::mutex LogMutex;
std::map<LogSinkId, LogSink> LogSinks;
LogSinkId LastSinkId = 0;

const char* LevelTag(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    case LogLevel::Except:
        return "EXCEP";
    }
    return "INFO";
}

bool ShouldPrint(LogLevel level) {
    return !options.headless && (level != LogLevel::Debug || options.verbose);
}

void NotifySinks(const LogRecord& record) {
    for (const auto& sink : LogSinks) {
        sink.second(record);
    }
}

void Emit(LogLevel level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    std::string Print = FormatDate(now) + "[" + LevelTag(level) + "] " + message + "\n";
    std::lock_guard<std::mutex> lock(LogMutex);
    if (ShouldPrint(level)) {
        beammp_stdout << Utils::ToWString(Print);
    }
    addToLog(Print);
    NotifySinks({ level, now, message });
}

#ifdef _WIN32
std::string ToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), result.data(), size, nullptr, nullptr);
    return result;
}

void Emit(LogLevel level, const std::wstring& message) {
    auto now = std::chrono::system_clock::now();
    std::wstring Print = Utils::ToWString(FormatDate(now)) + L"[" + Utils::ToWString(LevelTag(level)) + L"] " + message + L"\n";
    std::lock_guard<std::mutex> lock(LogMutex);
    if (ShouldPrint(level)) {
        std::wcout << Print;
    }
    addToLog(Print);
    if (!LogSinks.empty()) {
        NotifySinks({ level, now, ToUtf8(message) });
    }
}
#endif
}

LogSinkId AddLogSink(LogSink sink) {
    std::lock_guard<std::mutex> lock(LogMutex);
    LogSinks.emplace(++LastSinkId, std::move(sink));
    return LastSinkId;
}
void RemoveLogSink(LogSinkId id) {
    std::lock_guard<std::mutex> lock(LogMutex);
    LogSinks.erase(id);
}
void info(const std::string& toPrint) {
    Emit(LogLevel::Info, toPrint);
}
void debug(const std::string& toPrint) {
    Emit(LogLevel::Debug, toPrint);
}
void warn(const std::string& toPrint) {
    Emit(LogLevel::Warn, toPrint);
}
void error(const std::string& toPrint) {
    Emit(LogLevel::Error, toPrint);
}
void fatal(const std::string& toPrint) {
    Emit(LogLevel::Fatal, toPrint);
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::exit(1);
}
void except(const std::string& toPrint) {
    Emit(LogLevel::Except, toPrint);
}


#ifdef _WIN32
void info(const std::wstring& toPrint) {
    Emit(LogLevel::Info, toPrint);
}
void debug(const std::wstring& toPrint) {
    Emit(LogLevel::Debug, toPrint);
}
void warn(const std::wstring& toPrint) {
    Emit(LogLevel::Warn, toPrint);
}
void error(const std::wstring& toPrint) {
    Emit(LogLevel::Error, toPrint);
}
void fatal(const std::wstring& toPrint) {
    Emit(LogLevel::Fatal, toPrint);
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::exit(1);
}
void except(const std::wstring& toPrint) {
    Emit(LogLevel::Except, toPrint);
}
#endif
