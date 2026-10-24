/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

/// Callbacks are invoked outside the internal lock, so they may add or remove observers.
template <typename... Args>
class Observers {
public:
    using Id = size_t;
    using Callback = std::function<void(Args...)>;

    Id Add(Callback callback) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_callbacks.emplace(++m_last, std::move(callback));
        return m_last;
    }

    void Remove(Id id) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_callbacks.erase(id);
    }

    void Notify(Args... args) const {
        std::vector<Callback> callbacks;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& entry : m_callbacks) {
                callbacks.push_back(entry.second);
            }
        }
        for (const auto& callback : callbacks) {
            callback(args...);
        }
    }

private:
    mutable std::mutex m_mutex;
    std::map<Id, Callback> m_callbacks;
    Id m_last = 0;
};
