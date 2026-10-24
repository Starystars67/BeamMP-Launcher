/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Observers.h"
#include <doctest/doctest.h>
#include <string>

TEST_CASE("every observer is notified with the arguments") {
    Observers<int, const std::string&> observers;
    int first = 0, second = 0;
    std::string seen;
    observers.Add([&](int value, const std::string& text) { first = value; seen = text; });
    observers.Add([&](int value, const std::string&) { second = value * 2; });
    observers.Notify(21, "hello");
    CHECK(first == 21);
    CHECK(second == 42);
    CHECK(seen == "hello");
}

TEST_CASE("ids are unique and removal is exact") {
    Observers<> observers;
    int a = 0, b = 0;
    auto idA = observers.Add([&] { ++a; });
    auto idB = observers.Add([&] { ++b; });
    CHECK(idA != idB);
    observers.Remove(idA);
    observers.Notify();
    CHECK(a == 0);
    CHECK(b == 1);
    observers.Remove(idA); // removing twice is harmless
    observers.Remove(9999);
}

TEST_CASE("a callback may remove itself or add observers") {
    Observers<> observers;
    int calls = 0;
    Observers<>::Id self = 0;
    self = observers.Add([&] {
        ++calls;
        observers.Remove(self);
        observers.Add([&] { ++calls; });
    });
    observers.Notify();
    CHECK(calls == 1);
    observers.Notify();
    CHECK(calls == 2);
}
