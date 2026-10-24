/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Observers.h"
#include <atomic>
#include <mutex>
#include <string>
#include <string_view>

/// A string that can be read and written from any thread.
class SharedString {
public:
    SharedString() = default;

    SharedString& operator=(std::string_view value);
    operator std::string() const;

private:
    mutable std::mutex m_mutex;
    std::string m_value;
};

struct Account {
    bool authenticated = false;
    std::string username;
    std::string role;
    int id = -1;
};

struct StatusSnapshot {
    Account account;
    int ping = -1;
    std::string uploadStatus;
    std::string modStatus;
    unsigned long gamePid = 0;
};

extern std::atomic<int> ping;
extern std::atomic<unsigned long> GamePID;
extern SharedString UlStatus;
extern SharedString MStatus;

Account GetAccount();
void SetAccount(Account account);

/// Called after every SetAccount, on the calling thread.
Observers<const Account&>::Id AddAccountObserver(Observers<const Account&>::Callback callback);
void RemoveAccountObserver(Observers<const Account&>::Id id);
StatusSnapshot GetStatusSnapshot();
