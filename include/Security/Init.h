/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once
#include "Utils.h"
#include <string>
#include <vector>
void PreGame(const beammp_fs_string& GamePath);
std::string CheckVer(const std::filesystem::path& path);
void InitGame(const beammp_fs_string& Dir);
// Controlled mode only: the lifecycle must already be LaunchingGame.
void LaunchGame(const std::vector<std::string>& args);
bool StopGame();
beammp_fs_string GetGameDir();
/// Use this folder as the BeamNG.drive install instead of looking for it, or forget it again.
void SetGameDir(const std::filesystem::path& dir);
void ClearGameDir();
void LegitimacyCheck();
void CheckLocalKey();