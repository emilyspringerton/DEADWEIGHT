# Phase D3 — Windows client

**Goal**: a real, human-playable SDL2/GL client that connects to D2's real server, replacing D1's
debug render with the actual game UI (grid, item tray, combat view) and D2's dummy-opponent test
harness with real matchmaker-mediated 1v1 matches. Same architecture family as `apps/arena`
(`REDGARDEN`/`ECOWAR`) — a new, standalone client, not a fork of either.

## Scope (in)

- `apps/deadweight_client`: SDL2 window, OpenGL render, the same "shader-based on purpose"
  reasoning `REDGARDEN/NORTHSTAR.md` already gives for its own client (only needs GL 3.x entry
  points via `SDL_GL_GetProcAddress`, no GLU dependency).
- Real rendering for: the 6x6 grid + placed items (from D1's own item catalog/shapes), the item
  tray/draft pool (D2's Debris Field, real server state not a mock), energy-flow visualization
  (the transcript's own "pulsing neon trail along conductors" idea is genuinely good and cheap to
  render — adopted), combat resolution (weapon fire, hull%, the D2-resolved timeout tiebreak
  shown plainly, not just "a winner appears").
- Real input: mouse-driven placement/rotation/Panic Cut (desktop's own natural equivalent of the
  transcript's own mobile tap-to-target scheme — D3 does NOT need to ape the phone-specific
  affordances the transcript designed for touch; a mouse already does drag+scroll-wheel-rotate
  better than tap-to-target does, the same way `REDGARDEN`'s own client never pretended to be a
  touch UI either).
- Networking: connect to `apps/deadweight_matchmaker`, handle `PACKET_MATCH_FOUND`, render the
  live snapshot stream from `apps/deadweight_server` — the exact wire-protocol shape
  (`NetHeader` + typed payload packets, split across multiple packets if any single one risks the
  MTU ceiling, the same real lesson `ECOWAR`'s own `PACKET_ARENA_SNAPSHOT_HEROES`/
  `PACKET_ARENA_SNAPSHOT_OBSTACLES` splits already paid for) copied directly, not redesigned.
- First real PARENA mod integration: a small, specific gameplay-decision function (a candidate:
  the D2 placeholder bot's own item-priority heuristic, or a damage-resolution edge case) compiled
  via PARENA and called from the host C client/server — proving the "PARENA mod is the trigger,
  host does the real work" idiom actually works for this game before D4 needs the same pattern to
  work for the (much narrower) Java emitter too.

## Scope (out)

- No EOSUI styling (D5) — this phase's own UI is real, functional, and ugly on purpose, the same
  bootstrapping order every other game in this monorepo already went through (bespoke immediate-
  mode boxes first, a shared design language only once one exists to adopt).
- No guest-account UI polish beyond a functional name-entry + "resume" flow — this phase proves
  the account system works end-to-end from a real client, it doesn't need to look finished.

## Acceptance criteria

- [ ] A human can complete a full real match — queue, draft against a live opponent (bot or a
  second human client), pack a grid, watch combat resolve — entirely through this client, no
  debug fallback paths.
- [ ] The PARENA mod round-trips for real (compiled, called, its output actually changes game
  state observably) — matching the "live round-trip tested, not just compile-checked" bar
  `test_tree_passive.c`/`test_bloodflower.c` already set for every other PARENA mod in this
  monorepo.
- [ ] `bash scripts/build.sh`-equivalent clean for this binary specifically (this monorepo's own
  found-the-hard-way lesson, `S170-186`: "this script never built apps/arena itself... only
  scripts/build_arena.sh did" — the actual player-facing client binary must be a real, named part
  of whatever this repo's own build script checks, not silently assumed covered).
- [ ] Live-verified against D2's real matchmaker/server, not just localhost with both processes
  hand-launched by the same developer in the same terminal session — a real network round trip,
  even if that just means two processes on the same box talking over a real socket.
