/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Games/Preparer.h"

namespace Control {
class Controller;
}

/// Starts the control server for a controlled launcher and announces its port on stdout. Every game of
/// the preparer's registry is listed to the controller, and what the preparer finds out about a game is
/// reported to it (the modules have to outlive the process, they do).
/// The process exits (code 0) once a client asks for it, or when nobody attaches in time.
void StartControl(Games::GamePreparer& preparer);

/// The controller of a controlled launcher, nothing otherwise.
Control::Controller* GetController();
