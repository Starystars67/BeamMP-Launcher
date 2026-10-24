/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Status.h"

std::atomic<int> ping = -1;
std::atomic<unsigned long> GamePID = 0;
SharedString UlStatus;
SharedString MStatus;

static std::mutex AccountMutex;
static Account CurrentAccount;
static Observers<const Account&> AccountObservers;

SharedString& SharedString::operator=(std::string_view value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_value = value;
    return *this;
}

SharedString::operator std::string() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_value;
}

Account GetAccount() {
    std::lock_guard<std::mutex> lock(AccountMutex);
    return CurrentAccount;
}

void SetAccount(Account account) {
    {
        std::lock_guard<std::mutex> lock(AccountMutex);
        CurrentAccount = account;
    }
    AccountObservers.Notify(account);
}

Observers<const Account&>::Id AddAccountObserver(Observers<const Account&>::Callback callback) {
    return AccountObservers.Add(std::move(callback));
}

void RemoveAccountObserver(Observers<const Account&>::Id id) {
    AccountObservers.Remove(id);
}

StatusSnapshot GetStatusSnapshot() {
    StatusSnapshot snapshot;
    snapshot.account = GetAccount();
    snapshot.ping = ping;
    snapshot.uploadStatus = UlStatus;
    snapshot.modStatus = MStatus;
    snapshot.gamePid = GamePID;
    return snapshot;
}
