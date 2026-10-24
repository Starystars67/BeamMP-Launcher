/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Net/Download.h"
#include "Net/Sha256.h"
#include <atomic>
#include <chrono>
#include <doctest/doctest.h>
#include <fstream>
#include <httplib.h>
#include <thread>

using namespace Net;
namespace fs = std::filesystem;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("beammp-download-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

struct FileServer {
    httplib::Server http;
    std::thread thread;
    int port = 0;
    std::string body;

    FileServer() {
        body.resize(300 * 1024);
        for (size_t i = 0; i < body.size(); i++) {
            body[i] = static_cast<char>(i * 7);
        }
        http.Get("/file", [this](const httplib::Request&, httplib::Response& response) { response.set_content(body, "application/octet-stream"); });
        http.Get("/missing", [](const httplib::Request&, httplib::Response& response) { response.status = 404; });
        http.Get("/redirect", [](const httplib::Request&, httplib::Response& response) { response.set_redirect("/file"); });
        port = http.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { http.listen_after_bind(); });
        http.wait_until_ready();
    }
    ~FileServer() {
        http.stop();
        thread.join();
    }
    std::string Url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port) + path; }
};

DownloadOptions Loopback() {
    DownloadOptions options;
    options.allowLoopbackHttp = true;
    return options;
}
}

TEST_CASE("SHA-256 of known texts") {
    CHECK(Sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256Hex(std::string("a\0b", 3)) != Sha256Hex("ab"));
}

TEST_CASE("SHA-256 of a file, in pieces") {
    TempDir dir;
    CHECK_FALSE(Sha256FileHex(dir.path / "missing").has_value());
    std::string big(200 * 1024 + 5, 'x');
    {
        std::ofstream(dir.path / "big", std::ios::binary) << big;
        std::ofstream(dir.path / "empty", std::ios::binary);
    }
    CHECK(Sha256FileHex(dir.path / "big") == Sha256Hex(big));
    CHECK(Sha256FileHex(dir.path / "empty") == Sha256Hex(""));
}

TEST_CASE("only https is accepted, and plain http only to this machine when allowed") {
    CHECK(DownloadUrlAllowed("https://builds.bepinex.dev/x.zip", false));
    CHECK_FALSE(DownloadUrlAllowed("http://builds.bepinex.dev/x.zip", false));
    CHECK_FALSE(DownloadUrlAllowed("http://builds.bepinex.dev/x.zip", true));
    CHECK_FALSE(DownloadUrlAllowed("http://127.0.0.1:80/x", false));
    CHECK(DownloadUrlAllowed("http://127.0.0.1:80/x", true));
    CHECK(DownloadUrlAllowed("http://localhost/x", true));
    CHECK_FALSE(DownloadUrlAllowed("http://127.0.0.1.evil.com/x", true));
    CHECK_FALSE(DownloadUrlAllowed("http://127.0.0.1@evil.com/x", true));
    CHECK_FALSE(DownloadUrlAllowed("http://127.0.0.1:80@evil.com/x", true));
    CHECK_FALSE(DownloadUrlAllowed("http://127.0.0.1\\@evil.com/x", true));
    CHECK_FALSE(DownloadUrlAllowed("http://127.0.0/x", true));
    CHECK_FALSE(DownloadUrlAllowed("http://127.0.0.1.2/x", true));
    CHECK_FALSE(DownloadUrlAllowed("http://128.0.0.1/x", true));
    CHECK(DownloadUrlAllowed("http://127.1.2.3:8080/x?y=1", true));
    CHECK(DownloadUrlAllowed("http://[::1]:8080/x", true));
    CHECK_FALSE(DownloadUrlAllowed("http://[::2]:8080/x", true));
    CHECK_FALSE(DownloadUrlAllowed("ftp://example.com/x", true));
    CHECK_FALSE(DownloadUrlAllowed("file:///etc/passwd", true));
    CHECK_FALSE(DownloadUrlAllowed("", true));
    CHECK_FALSE(DownloadUrlAllowed("https://", false));
}

TEST_CASE("a download is written and moved into place") {
    FileServer server;
    TempDir dir;
    auto result = DownloadFile(server.Url("/file"), dir.path / "sub" / "out.bin", Loopback());
    CHECK(result.ok);
    CHECK(Slurp(dir.path / "sub" / "out.bin") == server.body);
    CHECK_FALSE(fs::exists(dir.path / "sub" / "out.bin.part"));
}

TEST_CASE("an http download to another machine, or without permission, is refused before anything is written") {
    TempDir dir;
    auto result = DownloadFile("http://example.com/file", dir.path / "out.bin", Loopback());
    CHECK_FALSE(result.ok);
    CHECK(fs::is_empty(dir.path));
    FileServer server;
    result = DownloadFile(server.Url("/file"), dir.path / "out.bin");
    CHECK_FALSE(result.ok);
    CHECK_FALSE(fs::exists(dir.path / "out.bin"));
}

TEST_CASE("an error status fails and leaves nothing behind") {
    FileServer server;
    TempDir dir;
    auto result = DownloadFile(server.Url("/missing"), dir.path / "out.bin", Loopback());
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.cancelled);
    CHECK_FALSE(result.error.empty());
    CHECK_FALSE(fs::exists(dir.path / "out.bin"));
    CHECK_FALSE(fs::exists(dir.path / "out.bin.part"));
}

TEST_CASE("a redirect to plain http is not followed") {
    FileServer server;
    TempDir dir;
    auto result = DownloadFile(server.Url("/redirect"), dir.path / "out.bin", Loopback());
    CHECK_FALSE(result.ok);
    CHECK_FALSE(fs::exists(dir.path / "out.bin"));
}

TEST_CASE("a download over the limit is stopped") {
    FileServer server;
    TempDir dir;
    DownloadOptions options = Loopback();
    options.maxBytes = 100 * 1024;
    auto result = DownloadFile(server.Url("/file"), dir.path / "out.bin", options);
    CHECK_FALSE(result.ok);
    CHECK(result.error.find("larger") != std::string::npos);
    CHECK_FALSE(fs::exists(dir.path / "out.bin"));
    CHECK_FALSE(fs::exists(dir.path / "out.bin.part"));
}

TEST_CASE("progress is reported and a download can be cancelled") {
    FileServer server;
    TempDir dir;
    std::atomic<int> calls = 0;
    uint64_t last = 0;
    DownloadOptions options = Loopback();
    options.progress = [&](uint64_t done, uint64_t total) {
        ++calls;
        last = done;
        (void)total;
        return true;
    };
    CHECK(DownloadFile(server.Url("/file"), dir.path / "a.bin", options).ok);
    CHECK(calls > 0);

    options.progress = [](uint64_t, uint64_t) { return false; };
    auto result = DownloadFile(server.Url("/file"), dir.path / "b.bin", options);
    CHECK_FALSE(result.ok);
    CHECK(result.cancelled);
    CHECK_FALSE(fs::exists(dir.path / "b.bin"));
    CHECK_FALSE(fs::exists(dir.path / "b.bin.part"));
}

TEST_CASE("an unreachable server fails") {
    TempDir dir;
    DownloadOptions options = Loopback();
    options.connectTimeoutSeconds = 2;
    auto result = DownloadFile("http://127.0.0.1:1/file", dir.path / "out.bin", options);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(fs::exists(dir.path / "out.bin"));
}
