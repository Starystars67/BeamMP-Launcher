/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#define DOCTEST_CONFIG_IMPLEMENT
#include <curl/curl.h>
#include <doctest/doctest.h>

int main(int argc, char** argv) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return doctest::Context(argc, argv).run();
}
