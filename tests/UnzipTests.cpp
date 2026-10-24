/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Net/Unzip.h"
#include <chrono>
#include <doctest/doctest.h>
#include <fstream>

using namespace Net;
namespace fs = std::filesystem;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("beammp-unzip-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

std::string Slurp(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::string Clean(const std::string& name) {
    std::string cleaned;
    return SafeArchivePath(name, cleaned) ? cleaned : "<refused>";
}
}

TEST_CASE("archive paths: the safe ones are cleaned") {
    CHECK(Clean("a.txt") == "a.txt");
    CHECK(Clean("dir/a.txt") == "dir/a.txt");
    CHECK(Clean("dir\\sub\\a.txt") == "dir/sub/a.txt");
    CHECK(Clean("./a.txt") == "a.txt");
    CHECK(Clean("dir/./a.txt") == "dir/a.txt");
    CHECK(Clean("dir/") == "dir");
    CHECK(Clean("BepInEx/core/") == "BepInEx/core");
    CHECK(Clean("a..b/c") == "a..b/c");
    CHECK(Clean("..a/b") == "..a/b");
}

TEST_CASE("archive paths: everything that could leave the destination is refused") {
    for (const char* bad : { "", "..", "../a", "a/../b", "a/..", "dir/../../a", "/etc/passwd", "\\windows\\x", "C:\\x", "c:/x", "C:x", "a//b", ".", "./", "/", "a\\..\\..\\b", "..\\a" }) {
        CHECK(Clean(bad) == "<refused>");
    }
    CHECK(Clean(std::string("a\0b", 3)) == "<refused>");
}

TEST_CASE("an archive is unpacked with its folders") {
    TempDir dir;
    REQUIRE(CreateZip(dir.path / "a.zip", { { "top.txt", "one" }, { "dir/", "" }, { "dir/inner.bin", std::string("\0\1\2", 3) }, { "deep/er/file.txt", "two" } }));
    std::vector<size_t> seen;
    auto result = Unzip(dir.path / "a.zip", dir.path / "out", {}, [&](size_t done, size_t total) {
        seen.push_back(done);
        CHECK(total == 4);
    });
    REQUIRE(result.ok);
    CHECK(Slurp(dir.path / "out" / "top.txt") == "one");
    CHECK(Slurp(dir.path / "out" / "dir" / "inner.bin") == std::string("\0\1\2", 3));
    CHECK(Slurp(dir.path / "out" / "deep" / "er" / "file.txt") == "two");
    CHECK(result.files.size() == 3);
    CHECK(seen == std::vector<size_t> { 1, 2, 3, 4 });
}

TEST_CASE("an entry that would land outside is refused and nothing outside is written") {
    TempDir dir;
    fs::create_directories(dir.path / "out");
    REQUIRE(CreateZip(dir.path / "evil.zip", { { "fine.txt", "ok" }, { "../escaped.txt", "bad" }, { "after.txt", "no" } }));
    auto result = Unzip(dir.path / "evil.zip", dir.path / "out");
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("Unsafe") != std::string::npos);
    CHECK_FALSE(fs::exists(dir.path / "escaped.txt"));
    CHECK_FALSE(fs::exists(dir.path / "out" / "after.txt"));
    // (the writer used here may itself refuse a name like these; the names are checked on their own above)
    if (CreateZip(dir.path / "evil2.zip", { { "/tmp/beammp-escaped.txt", "bad" } })) {
        CHECK_FALSE(Unzip(dir.path / "evil2.zip", dir.path / "out").ok);
        CHECK_FALSE(fs::exists("/tmp/beammp-escaped.txt"));
    }
    if (CreateZip(dir.path / "evil3.zip", { { "a\\..\\..\\up.txt", "bad" } })) {
        CHECK_FALSE(Unzip(dir.path / "evil3.zip", dir.path / "out").ok);
        CHECK_FALSE(fs::exists(dir.path / "up.txt"));
    }
}

TEST_CASE("limits stop an archive") {
    TempDir dir;
    REQUIRE(CreateZip(dir.path / "a.zip", { { "a", std::string(1000, 'x') }, { "b", std::string(1000, 'y') } }));
    UnzipLimits limits;
    limits.maxTotalBytes = 1500;
    CHECK_FALSE(Unzip(dir.path / "a.zip", dir.path / "o1", limits).ok);
    limits = {};
    limits.maxFileBytes = 999;
    CHECK_FALSE(Unzip(dir.path / "a.zip", dir.path / "o2", limits).ok);
    limits = {};
    limits.maxFiles = 1;
    CHECK_FALSE(Unzip(dir.path / "a.zip", dir.path / "o3", limits).ok);
    CHECK(Unzip(dir.path / "a.zip", dir.path / "o4").ok);
}

TEST_CASE("something that is not an archive fails cleanly") {
    TempDir dir;
    CHECK_FALSE(Unzip(dir.path / "missing.zip", dir.path / "out").ok);
    { std::ofstream(dir.path / "text.zip") << "this is not a zip"; }
    auto result = Unzip(dir.path / "text.zip", dir.path / "out");
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}
