# Signing in to the launcher

How the launcher signs a user in without ever seeing the password, and what had to change in the API and the Accounts site. Everything here is local work, not deployed.

## Flow

1. The UI asks the engine (`account.sign_in`). The engine starts a one-shot web server on `127.0.0.1:<free port>` and opens
   `https://<accounts>/auth/external?client_id=beammp-launcher&redirect_uri=http://127.0.0.1:<port>/callback&response_type=code&scope=profile game offline_access&state=…&code_challenge=…&code_challenge_method=S256`
   in the system browser.
2. The user signs in on the BeamMP website (password, 2FA, whatever the site asks). The Accounts page skips the consent screen for first-party clients and redirects to the loopback address with `code` and `state`.
3. The engine checks `state` and the Host header, then `POST /v1/oauth/token` (`authorization_code`, PKCE verifier) and gets an access token (1 hour) and a refresh token (30 days, rotating).
4. `POST /v1/oauth/game-session` with the access token returns what `/userlogin` returns today (public key, private key, role, groups). The engine fills the account and the keys the game uses. No `key` file is written.
5. The refresh token goes to the macOS keychain (`com.beammp.launcher` / `refresh-token`). The next start restores the sign-in from it without the browser.
6. The game keys are single use per server join, so before every join (`CheckLocalKey`) the engine renews them: refresh the access token if it is about to expire, then `game-session` again. Refreshes are serialized because a refresh token is single use.
7. Sign out (UI, or the game's logout) revokes the refresh token's family on the server and wipes the keychain item.

The legacy password path through the game is unchanged and keeps working next to this.

## API changes (`API/`, uncommitted)

- **Loopback redirect** (RFC 8252 section 7.3): a client that registered `http://127.0.0.1/callback` (or `[::1]`) may use any port on it. `localhost`, other paths, queries, schemes or hosts still have to match exactly.
- **New scopes `game` and `offline_access`**, first-party clients only (`oauth_clients.first_party`). Authorize refuses them for others; the token endpoint re-checks.
- **Refresh tokens**: new table `oauth_refresh_tokens` (migration `20261001120000_add_oauth_refresh_tokens`), only the SHA-256 is stored. `grant_type=refresh_token` rotates: the presented token is spent and a new one issued in the same family. A spent token presented again revokes the whole family (replay detection). 30 days per token, 180 days per family. Only first-party clients; a confidential client must still present its secret. `POST /v1/oauth/revoke` accepts refresh tokens and revokes the family.
- **`POST /v1/oauth/game-session`**: bearer access token of a first-party client with the `game` scope; reuses the existing `user_login` (suspension check, key rotation, player record). Tighter rate limit than `userinfo`. `/userlogin` itself is untouched (only an `export` alias was added in `game.ts`).
- `openapi.yml` documents `token`, `revoke`, `userinfo` and `game-session`.

Tests: `tests/unit/oauth-launcher.test.ts` (55 cases) plus the existing OAuth tests; the whole suite passes.

## Accounts site (`Accounts/`, uncommitted)

`src/views/auth/ExternalAuth.vue`: consent descriptions for `game` and `offline_access` (the page refuses scopes it has no text for). First-party clients are approved without a click, so users do not see these texts.

## To do before this can ship (needs you)

1. **Register the client** (staff or SQL; the developer API cannot create first-party clients):

   ```sql
   INSERT INTO beammp.oauth_clients (client_id_hash, name, publisher, verified, first_party, enabled, updated_at)
   VALUES (encode(sha256('beammp-launcher'::bytea), 'hex'), 'BeamMP Launcher', 'BeamMP Ltd.', true, true, true, now());
   INSERT INTO beammp.oauth_client_redirect_uris (client_id, redirect_uri)
   SELECT id, 'http://127.0.0.1/callback' FROM beammp.oauth_clients
   WHERE client_id_hash = encode(sha256('beammp-launcher'::bytea), 'hex');
   ```
2. **Apply the migration**, deploy the API, then the Accounts change.
3. **Confirm the hostnames.** The engine defaults to `https://auth.beammp.com` (API, from `openapi.yml`) and `https://portal.beammp.com` (Accounts, guessed from the API's CORS list). Override with `BEAMMP_API_URL` / `BEAMMP_ACCOUNTS_URL` or change `Auth::Endpoints`.
4. A cleanup job for expired `oauth_refresh_tokens` rows (not written yet).
5. Keychain backends for Windows (Credential Manager) and Linux (Secret Service); until then those platforms sign in at every start.
6. Review of the whole design by someone on the team: it issues long-lived credentials.
