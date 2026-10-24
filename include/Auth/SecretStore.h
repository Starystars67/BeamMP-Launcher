/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <memory>
#include <optional>
#include <string>

namespace Auth {

/// Holds the one long-lived secret of a sign-in (the refresh token) somewhere
/// the operating system protects it, never in a plain file.
class SecretStore {
public:
    virtual ~SecretStore() = default;

    /// False when it could not be stored; the sign-in then only lasts for this run.
    virtual bool Save(const std::string& secret) = 0;
    virtual std::optional<std::string> Load() = 0;
    virtual void Erase() = 0;
};

/// The macOS keychain item `service` / `account`. Elsewhere there is no
/// backend yet and nothing is stored.
std::unique_ptr<SecretStore> MakeSystemSecretStore(const std::string& service = "com.beammp.launcher", const std::string& account = "refresh-token");

}
