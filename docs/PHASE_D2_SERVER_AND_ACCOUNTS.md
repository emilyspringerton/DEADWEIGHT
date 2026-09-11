# Phase D2 — server-authoritative 1v1, matchmaking, guest accounts

**Goal**: turn D1's proven-fun local loop into a real, server-authoritative networked 1v1 match,
with the same online-services/profile-tracking model `REDGARDEN`/`ECOWAR` already run in
production, plus the one genuinely new piece this whole project needs: a name-only guest account
that IDUNA has never supported before. Depends on D1 actually shipping and being fun first —
building the account system for a game nobody has confirmed is fun yet is the wrong order.

## Scope (in)

- `apps/deadweight_server`: hand-written C, server-authoritative UDP, the exact same architecture
  family as `apps/arena_server` (`REDGARDEN`/`ECOWAR`) — one match per process, no shared state
  between concurrent matches, real match/corpus logging from day one (not bolted on later, the
  same lesson `ECOWAR`'s own real incident — a matchmaker silently dead for 5 days behind a
  required, missing `EnvironmentFile` — already paid for once in this monorepo; D2's own systemd
  unit gets that file provisioned and load-bearing-tested before it's called done).
- `apps/deadweight_matchmaker`: **not a new binary** — `apps/matchmaker` is already generic
  (`--lobby-size`, `--server-bin`, `--first-game-port`); this phase just runs it with
  `--lobby-size 2` and a new `--server-bin`/port range, the same way ECOWAR's own matchmaker was
  stood up. Real work here is the seed-plumbing S370 already had to build for real for ECOWAR
  (the matchmaker generates a match seed, passes it to both the spawned server via `--seed` and
  every client via `MatchFoundMsg.seed`) — reused directly, not re-derived. **Note**: this copy
  (like every game's own `apps/matchmaker` copy) is a duplicated file, not shared infrastructure
  — `EMILY/docs/PARENGINE_NORTHSTAR.md` names this exact pattern (a live example from this same
  session: ECOWAR's own S370 seed fix already left REDGARDEN's identical copy behind). D2's own
  wire-protocol framing (`NetHeader` + typed snapshot packets, MTU-split) is named there as
  `stdlib/engine/net_snapshot.prn`'s own real first intended consumer — if that PARENA module
  exists by the time D2 actually starts, build against it instead of hand-porting ECOWAR's
  structs again; if it doesn't yet, port by hand as planned and this becomes the reason it gets
  built sooner rather than later.
- The real-time shared draft pool ("Debris Field"), server-authoritative, resolving the race
  condition `SPEC_REVIEW.md` §4 names: first-claim-received-wins, explicit "Sniped!" rejection to
  the losing claimant, never a silent client-side desync.
- A placeholder bot: **not** ECOWAR's own RL policy network (trained against ECOWAR's own hero
  kit — has no concept of a polyomino or an energy-routing grid, a real, named non-transfer named
  in `NORTHSTAR.md`). V0's bot is a simple heuristic — pack the highest-density legal items it can
  fit, wire the first generator to the first weapon it can path to — good enough for a bot-pool
  opponent, not good enough to be "the AI," and honestly labeled as such rather than oversold.
- IDUNA: new `game='deadweight'` scope on every player/session row
  (`202609050003_players_game_scope.sql`'s own real per-game scoping, reused directly); a new
  `DEADWEIGHT-BOTS` M2M agent identity (`ECOWAR-BOTS`'s own real precedent — a genuinely distinct
  identity from day one, not reusing REDGARDEN's/ECOWAR's, so this game's own match stats never
  mix into either).
- **The new work**: a `provider="guest"` player-account path in IDUNA. Real design, not yet
  built anywhere:
  - Client generates a random secret token on first launch, persists it locally (platform
    keystore/app-private-storage on Android, a local config file on Windows — the exact
    mechanism is a D3/D4 implementation detail, not an IDUNA concern).
  - `POST /api/v1/deadweight/guest-register {display_name}` → IDUNA creates a `players` row with
    `provider="guest"`, `provider_sub=<server-generated UUID>`, `game='deadweight'`, returns the
    UUID to the client to persist alongside the display name.
  - `POST /api/v1/deadweight/guest-login {provider_sub}` → resumes that same player's row (kills,
    deaths, sessions, whatever ladder MMR field D2 adds) if the token matches; **no token, no
    recovery** — the real, honest tradeoff the founder explicitly asked for ("you just create your
    name... if you don't [link an email] you can't get back into your account"), not softened
    into a fake-security "forgot password" flow.
  - Optional, later-in-D2-or-post-launch upgrade path: `POST .../guest-upgrade {provider,
    credential}` re-keys the SAME `player_id` onto `provider="google"`/`"iduna_local"` instead of
    creating a second row — the real reason this matters is a guest who upgrades keeps their
    match history and MMR, rather than starting over, which is the actual point of offering the
    upgrade at all.
  - **Real, honest security scoping, decided here rather than guessed at during implementation**:
    the guest secret is a bearer token for exactly one capability — "resume this one player's own
    stats" — and IDUNA should never trust it for anything beyond that (no elevated scopes, no
    cross-game reach, matching the same per-game scoping this whole path already opts into). This
    is a deliberately narrow, low-value credential, not a password-equivalent.

## Scope (out)

- No real matchmaking rating/ladder algorithm beyond "queue two players, match them" for V0 — an
  MMR number can live on the `players` row from day one, but ELO-style adjustment logic is a real,
  separate, tunable system that doesn't block a first playable match and shouldn't hold one up.
- No spectator mode, no replay storage beyond the match log every arena game already writes.
- No admin/moderation tooling beyond what IDUNA already has for every other game.

## Acceptance criteria

- [ ] Two real clients (even two copies of D1's own debug render, wired to the network instead of
  the dummy opponent) can queue, get matched, and complete a full match end-to-end against each
  other over the actual matchmaker/server, not localhost-only loopback assumptions.
- [ ] A guest account created with no email survives a client restart (token persisted, re-login
  resumes the same stats) and is **provably unrecoverable** without that token (the real, live-
  verified test: delete the local token, confirm there is no path back to that player row) —
  the honest tradeoff has to be demonstrated working as designed, not just documented.
- [ ] The Debris Field race condition (`SPEC_REVIEW.md` §4) is live-tested with two clients
  claiming the same item at genuinely close timing — confirm one wins, one gets a real "Sniped!"
  response, no silent desync on either side.
- [ ] `bash scripts/build.sh`-equivalent clean, a real headless smoke test suite (matching every
  other arena game in this monorepo — `tests/test_*.c` per subsystem, not one giant test), Apple
  + CHANGELOG + commit/push per `DEADWEIGHT/CLAUDE.md`'s own protocol.
- [ ] `ecowar-matchmaker.service`'s own real 5-day-dead incident does not repeat here — the new
  systemd unit's `EnvironmentFile` (if any) is verified present and loaded before this phase is
  called done, checked directly, not assumed from a config file existing on disk.
