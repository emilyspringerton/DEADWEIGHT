# DEADWEIGHT ↔ IDUNA HTTP contract (v1, S503-06)

Authoritative for lanes B (C server), D (Android), E (Python training). All bodies/responses JSON, small.
Errors everywhere: `{"error":"<message>"}` with the HTTP status (same shape as every other IDUNA handler).
Base URL default `http://localhost:8080` (`IDUNA_BASE_URL`). **These routes exist on prod only after IDUNA's next normal
deploy + `cmd/bootstrap` run (agent secrets); until then use a throwaway IDUNA.**

Game routes are generic under `/api/v1/games/{game}/…`; `{game}` must be a configured slug (VS0: `deadweight`). Adding a game =
one config row (`internal/games`), not new code. Players are scoped by `players.game`; a token minted for game A is refused by game B (403).

## Identities

| Who | Authenticates as | Permission(s) |
|---|---|---|
| Human (Android/Windows) | guest player token (below), ES256 JWT, 24 h | `deadweight.play` only (claims `game=deadweight`, `player_id`, `display_name`) |
| Bot (`dw_bot`) | M2M agent **DEADWEIGHT-BOTS** → `POST /api/v1/auth/agent {agent_name, agent_secret}` | `deadweight.bot.play` |
| Game server (`dw_server`) | M2M agent **DEADWEIGHT-SERVER** (separate from bots on purpose: bots must not be able to forge match results) | `deadweight.match.write` |
| Training / registry | M2M agent **DEADWEIGHT-RL** | `deadweight.checkpoints.write` |

Agent secrets are provisioned by `cmd/bootstrap` into `var/agent-secrets.env` (same as ECOWAR-BOTS etc.).

## Guest accounts (no email, no recovery by design)

`POST /api/v1/games/deadweight/guest-register` `{"display_name":"Ada"}` (1–16 chars after trim, no control chars; not unique)
→ 201 `{"player_id":"<uuid>","guest_secret":"<64 hex>","display_name":"Ada","token":"<jwt>","expires_at":<unix>}`.
**Persist `player_id` + `guest_secret` locally (Android: app-private storage). IDUNA stores only sha256(secret). Lose it = the
account is gone; there is no recovery path.** Rate limited per IP (429).

`POST /api/v1/games/deadweight/guest-login` `{"player_id","guest_secret"}` → 200 `{"player_id","display_name","token","expires_at"}`.
Wrong/unknown → 401 `{"error":"invalid credentials"}` (identical for both cases, constant-time compare). Rate limited (429).

Guest token claims: `sub=guest:<player_id>`, `player_id`, `display_name`, `game=deadweight`, `permissions=["deadweight.play"]`,
`aud=farthq-ecosystem`. It grants nothing else (no other game, no admin, no stats writes).
Not built (named): guest → google/email upgrade.

## Token verification (called by dw_server on HELLO; blocking loopback HTTP is fine)

`POST /api/v1/games/deadweight/verify` with `Authorization: Bearer <token from HELLO>` and optional body `{"name":"Ripper"}`.
→ 200 `{"player_id":"<uuid>","display_name":"Ada","kind":"human"|"bot","game":"deadweight"}`.
- guest token → `kind=human` (body ignored).
- DEADWEIGHT-BOTS agent token → `kind=bot`; `name` required (`[A-Za-z0-9_-]{1,16}`); IDUNA upserts a players row
  (`provider=deadweight_bot`, `provider_sub=name`) and returns its stable `player_id`. Bots therefore have persistent stats/rating.
- anything else (expired, bad signature, other game, google/local token, other agent) → 401/403.
No server credential is needed for verify (the token is self-validating).

## Match results (dw_server → IDUNA)

`POST /api/v1/games/deadweight/match-result`, `Authorization: Bearer <DEADWEIGHT-SERVER agent JWT>` (get one via
`/api/v1/auth/agent`; re-auth on 401). Body:
`{"match_id":123,"seed":456,"seat0_player_id":"<uuid>","seat1_player_id":"<uuid>","winner":0|1|2,"rounds":8,"reason":"hull|rounds|forfeit|server","mode":0}`
(`winner` 2 = draw, `mode` 0 = card). → 200 `{"ok":true,"duplicate":false,"seat0":{"rating":1516.0,"wins":1,"losses":0,"draws":0,"matches":1},"seat1":{…}}`.
Idempotent on `(match_id, seed, seat0_player_id, seat1_player_id)`: a replay returns 200 with `"duplicate":true` and changes nothing.
Both players must exist with `game=deadweight` (else 400). Elo K=32, start 1500, applied to humans and bots alike.

Reads (public, like other player profile routes):
`GET /api/v1/games/deadweight/players/{player_id}/stats` → `{"player_id","display_name","kind","rating","wins","losses","draws","matches"}`
`GET /api/v1/games/deadweight/leaderboard?limit=20` → `[{same fields}]` ordered by rating.

## Game-scoped checkpoint registry (training league; generalizes brawlpit-checkpoints)

Base `/api/v1/game-checkpoints/deadweight` — **same routes, fields and multipart format as `/api/v1/brawlpit-checkpoints`**
(`rl_registry.py` works with only the base path changed): `GET ?role=` list · `POST` multipart upload
(`role`,`generation`,`elo`,`source_location`,`file`, optional `weights_file`) · `GET /active` · `GET /{id}/download` ·
`GET /{id}/weights[?compress=none]` · `POST /match-result` `{"a_id","b_id","score_a"}`.
Writes (`POST`) need `deadweight.checkpoints.write` (DEADWEIGHT-RL); reads are public. Roles are lowercase:
`main`, `main_exploiter`, `league_exploiter`. Checkpoints are isolated per game (a deadweight id is a 404 under brawlpit's routes and vice-versa).
The existing `/api/v1/brawlpit-checkpoints…` routes are unchanged (they are the `brawlpit` game's alias).
