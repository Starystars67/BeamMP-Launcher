/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Options.h"
#include "Startup.h"
#include "TestSupport.h"

Options options;

std::filesystem::path TestDirectory() {
    static const std::filesystem::path Directory = [] {
        auto path = std::filesystem::temp_directory_path() / "beammp-launcher-tests";
        std::filesystem::create_directories(path);
        return path;
    }();
    return Directory;
}

beammp_fs_string GetEP(const beammp_fs_char*) {
    return TestDirectory().string() + "/";
}
