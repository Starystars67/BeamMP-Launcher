/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Logger.h"
#include "Options.h"
#include "TestSupport.h"
#include <doctest/doctest.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

namespace {
std::mutex RecordsMutex;
std::vector<LogRecord> Records;

// Sinks cannot be removed, so one capturing sink is registered for the whole run.
void EnsureSink() {
    static const bool Registered = [] {
        AddLogSink([](const LogRecord& record) {
            std::lock_guard<std::mutex> lock(RecordsMutex);
            Records.push_back(record);
        });
        return true;
    }();
    (void)Registered;
}

std::vector<LogRecord> TakeRecords() {
    std::lock_guard<std::mutex> lock(RecordsMutex);
    return std::exchange(Records, {});
}

struct CapturedConsole {
    std::ostringstream buffer;
    std::streambuf* original;
    CapturedConsole()
        : original(std::cout.rdbuf(buffer.rdbuf())) { }
    ~CapturedConsole() { std::cout.rdbuf(original); }
    std::string str() const { return buffer.str(); }
};

std::vector<std::string> LogFileLines() {
    std::ifstream file(TestDirectory() / "Launcher.log");
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        lines.push_back(line);
    }
    return lines;
}

struct Fresh {
    Fresh() {
        options = Options();
        EnsureSink();
        InitLog();
        TakeRecords();
    }
};

const std::regex LineFormat(R"(^\[\d+/\d+/\d+ \d{2}:\d{2}:\d{2}\] \[(DEBUG|INFO|WARN|ERROR|EXCEP)\] .*$)");
}

TEST_CASE_FIXTURE(Fresh, "records carry the level and message, in call order") {
    options.verbose = true;
    CapturedConsole console;
    debug("d");
    info("i");
    warn("w");
    error("e");
    except("x");

    auto records = TakeRecords();
    REQUIRE(records.size() == 5);
    CHECK(records[0].level == LogLevel::Debug);
    CHECK(records[1].level == LogLevel::Info);
    CHECK(records[2].level == LogLevel::Warn);
    CHECK(records[3].level == LogLevel::Error);
    CHECK(records[4].level == LogLevel::Except);
    CHECK(records[0].message == "d");
    CHECK(records[4].message == "x");
    CHECK(records[0].time <= records[4].time);
}

TEST_CASE_FIXTURE(Fresh, "console line format is unchanged and matches the log file line") {
    CapturedConsole console;
    info("hello");

    const std::string printed = console.str();
    REQUIRE(!printed.empty());
    CHECK(printed.back() == '\n');
    CHECK(std::regex_match(printed.substr(0, printed.size() - 1), LineFormat));
    CHECK(printed.find("[INFO] hello") != std::string::npos);

    auto lines = LogFileLines();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] + "\n" == printed);
}

TEST_CASE_FIXTURE(Fresh, "debug is only printed when verbose, but is always logged and sent to sinks") {
    {
        CapturedConsole console;
        debug("quiet");
        CHECK(console.str().empty());
    }
    {
        options.verbose = true;
        CapturedConsole console;
        debug("loud");
        CHECK(console.str().find("[DEBUG] loud") != std::string::npos);
    }

    CHECK(TakeRecords().size() == 2);
    auto lines = LogFileLines();
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].find("[DEBUG] quiet") != std::string::npos);
}

TEST_CASE_FIXTURE(Fresh, "headless prints nothing but still logs to the file and sinks") {
    options.headless = true;
    options.verbose = true;
    CapturedConsole console;
    info("a");
    warn("b");
    error("c");
    debug("d");
    except("e");

    CHECK(console.str().empty());
    CHECK(LogFileLines().size() == 5);
    CHECK(TakeRecords().size() == 5);
}

TEST_CASE_FIXTURE(Fresh, "concurrent logging never interleaves lines and the file order matches the sink order") {
    constexpr int Threads = 8;
    constexpr int PerThread = 250;
    options.headless = true;

    std::vector<std::thread> threads;
    for (int t = 0; t < Threads; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < PerThread; ++i) {
                info("t" + std::to_string(t) + "-" + std::to_string(i));
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    auto records = TakeRecords();
    auto lines = LogFileLines();
    REQUIRE(records.size() == Threads * PerThread);
    REQUIRE(lines.size() == records.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        REQUIRE(std::regex_match(lines[i], LineFormat));
        const std::string suffix = "[INFO] " + records[i].message;
        REQUIRE(lines[i].size() >= suffix.size());
        REQUIRE(lines[i].compare(lines[i].size() - suffix.size(), suffix.size(), suffix) == 0);
    }
}

TEST_CASE_FIXTURE(Fresh, "a removed sink is no longer called, others keep working") {
    options.headless = true;
    int removedCalls = 0;
    auto id = AddLogSink([&](const LogRecord&) { ++removedCalls; });
    info("one");
    RemoveLogSink(id);
    info("two");
    RemoveLogSink(id); // removing twice is harmless

    CHECK(removedCalls == 1);
    CHECK(TakeRecords().size() == 2);
}
