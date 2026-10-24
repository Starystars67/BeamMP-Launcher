/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Auth/Session.h"

/// Creates the launcher's sign-in session (without restoring a saved sign-in:
/// the caller attaches its observers first and then calls Session::Restore).
/// Used when a UI controls the launcher.
void StartAuth();

/// The session after StartAuth, otherwise null.
Auth::Session* ActiveAuth();
