/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Status.h"
#include <atomic>
#include <doctest/doctest.h>
#include <thread>
#include <vector>

namespace {
struct Fresh {
    Fresh() {
        SetAccount({});
        ping = -1;
        GamePID = 0;
        UlStatus = "";
        MStatus = "";
    }
};
}

TEST_CASE_FIXTURE(Fresh, "a fresh snapshot is empty") {
    StatusSnapshot snapshot = GetStatusSnapshot();
    CHECK_FALSE(snapshot.account.authenticated);
    CHECK(snapshot.account.username.empty());
    CHECK(snapshot.account.role.empty());
    CHECK(snapshot.account.id == -1);
    CHECK(snapshot.ping == -1);
    CHECK(snapshot.uploadStatus.empty());
    CHECK(snapshot.modStatus.empty());
    CHECK(snapshot.gamePid == 0);
}

TEST_CASE_FIXTURE(Fresh, "the snapshot reflects the existing globals") {
    SetAccount({ true, "user", "EA", 42 });
    ping = 37;
    GamePID = 1234;
    UlStatus = "UlLoading Resource a.zip";
    MStatus = "Mod list";

    StatusSnapshot snapshot = GetStatusSnapshot();
    CHECK(snapshot.account.authenticated);
    CHECK(snapshot.account.username == "user");
    CHECK(snapshot.account.role == "EA");
    CHECK(snapshot.account.id == 42);
    CHECK(snapshot.ping == 37);
    CHECK(snapshot.gamePid == 1234);
    CHECK(snapshot.uploadStatus == "UlLoading Resource a.zip");
    CHECK(snapshot.modStatus == "Mod list");
}

TEST_CASE_FIXTURE(Fresh, "the status strings keep the string assignment semantics they had as globals") {
    std::string value = "Ulstart";
    UlStatus = value;
    UlStatus = "UlDisconnected: " + value;

    std::string read = UlStatus;
    CHECK(read == "UlDisconnected: Ulstart");
}

TEST_CASE_FIXTURE(Fresh, "logging out clears the account") {
    SetAccount({ true, "user", "EA", 42 });
    SetAccount({});

    Account account = GetAccount();
    CHECK_FALSE(account.authenticated);
    CHECK(account.username.empty());
    CHECK(account.id == -1);
}

TEST_CASE_FIXTURE(Fresh, "an account is never observed half written") {
    std::atomic<bool> done = false;
    std::atomic<int> torn = 0;

    std::thread writer([&] {
        for (int i = 0; i < 20000; ++i) {
            if (i % 2) {
                SetAccount({ true, "user", "EA", 7 });
            } else {
                SetAccount({});
            }
        }
        done = true;
    });
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&] {
            while (!done) {
                Account account = GetAccount();
                bool loggedIn = account.authenticated && account.username == "user" && account.role == "EA" && account.id == 7;
                bool loggedOut = !account.authenticated && account.username.empty() && account.role.empty() && account.id == -1;
                if (!loggedIn && !loggedOut) {
                    ++torn;
                }
            }
        });
    }
    writer.join();
    for (auto& reader : readers) {
        reader.join();
    }
    CHECK(torn == 0);
}

TEST_CASE_FIXTURE(Fresh, "status strings can be written and read concurrently") {
    std::atomic<bool> done = false;
    std::atomic<int> torn = 0;

    std::thread writer([&] {
        for (int i = 0; i < 20000; ++i) {
            UlStatus = (i % 2) ? std::string(200, 'a') : std::string(200, 'b');
        }
        done = true;
    });
    std::thread reader([&] {
        while (!done) {
            StatusSnapshot snapshot = GetStatusSnapshot();
            const std::string& value = snapshot.uploadStatus;
            if (!value.empty() && value != std::string(200, 'a') && value != std::string(200, 'b')) {
                ++torn;
            }
        }
    });
    writer.join();
    reader.join();
    CHECK(torn == 0);
}

TEST_CASE_FIXTURE(Fresh, "account observers hear about every change until removed") {
    std::vector<Account> seen;
    auto id = AddAccountObserver([&](const Account& account) { seen.push_back(account); });
    SetAccount({ true, "user", "EA", 1 });
    SetAccount({});
    RemoveAccountObserver(id);
    SetAccount({ true, "other", "", 2 });

    REQUIRE(seen.size() == 2);
    CHECK(seen[0].username == "user");
    CHECK(seen[0].authenticated);
    CHECK_FALSE(seen[1].authenticated);
}
