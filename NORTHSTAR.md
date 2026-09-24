# DEADWEIGHT — "Dark Sector: Hold Battles" — Scoping (real critical review, not rubber-stamped)

## Source

`DEADWEIGHT/LeetCode Skills Course Curriculum.pdf` — a captured Gemini chat transcript (same real
provenance pattern `MIXFORGE/legacy.txt` already established for this monorepo: a founder AI
brainstorm session, pulled in wholesale, reviewed directly rather than built blind). The filename
is real but misleading — the transcript starts as an actual LeetCode teaching curriculum (Two Sum,
Big-O, recursion/TCO/functional-programming teaching threads) and, from the prompt "write a simple
game that uses the first 6" onward, spirals across ~50 further turns into a fully-designed
multiplayer PvP game: **Dark Sector: Hold Battles**. Only that back half is a game spec; nothing
here touches the LeetCode-curriculum half, which stays exactly what it is (a teaching document, not
this repo's concern).

Founder real-time, this session: "check for new upstream DEADWEIGHT - it has a doc with a spec for
a game - lets start building it out with online services the same model as REDGARDEN full tracking
of games and profiles etc allow for the model of the mobile games where you just create your name
and you dont have to connect it to an email but if you dont you cant get back into your account -
1v1 so same bot pool set up as ECOWAR i believe - build it with PARENA so that we target both
ANDROID AND WINDOWS (JAVA, C) - so we really need 1 client but 2 totally different platforms like
HEARTHSTONE - we northstarred a meta ui framework yesterday establishing that pattern here may be
useful". Routed via `emily observe` first (obs `2026-09-11T19-12-36Z`, Apple #19014) per
`EMILY/docs/THE_EMILY_WAY.md` Principle 18, then scoped here — real investigation below, not a
guess, before any code.

## The real game, as the transcript actually specifies it

**The genuinely novel mechanical core** (this is the part worth building): a 6x6 spatial grid where
players pack polyomino-shaped cargo items under a weight/space budget — classic 0/1 Knapsack, then
evolved with **splittable item classes** (unsplittable / halvable / quarterable) that each incur a
**fragmentation tax** (a cut item's fragments cost more total space than the whole item did) —
so packing efficiently is a genuine, non-trivial spatial puzzle, not a spreadsheet a player solves
once and repeats forever. The same grid then **becomes the combat board**: item shape/orientation
+ position determines energy-routing paths from generators to perimeter weapons/defenses, and the
"waste" from a Panic Cut (Dead Squares) becomes armor. This dual-purpose mechanic — build a
container, then fight with the container you built — is the real, shippable idea.

**Everything after that is the transcript escalating, turn by turn, into a live-service feature
backlog for a game that doesn't exist yet**: a full options-pricing engine (a literal modified
Black-Scholes formula with real-time volatility scaling), a 4-tier insurance/derivatives market, a
30-second real-time ticker UI with Call/Put/Yield/Mystery contracts, Merkle-tree proof-of-cargo
for "hiding your layout," 4 status effects, 24 candidate Legendary items (13 fully statted, plus
9 more named), 16 Ultimate abilities each with 3 Flow-tier variants (48 distinct effect
descriptions) including several *deliberately* "why would anyone pick this" items meant to become
meta 2 years post-launch, a 3-Win Double-Elimination 8-player tournament bracket with phase-specific
financial rules, 2v2 team battles with cross-grid "Link Modules" and duo-ultimate combos, a
dual-axis mobile haptic-engine timing table, and full C++-flavored data structures (bitmask grid
representation, conduction-vector routing, a `GridTileStatus` bitfield struct) for all of it. This
is a multi-year live-service feature catalog imagined in one sitting — real, creative, and
**not a V0**, the same "real, honest pivot" judgment call `MIXFORGE/NORTHSTAR.md` and
`LO/NORTHSTAR.md` already made against their own source transcripts.

## The real, current state of every capability this ask leans on (checked directly, not assumed)

- **PARENA's C emitter (`src/emit.c`, 6218 lines) is real and mature** — but this monorepo's own
  established, universal precedent for every C-hosted PARENA game (`REDGARDEN`, `ECOWAR`,
  `PAPERCRAFT`, `WEAKNIGHT_BEDROCK_RACERS`) is **never** "the whole client written in PARENA, then
  emitted to C." It's a hand-written C/SDL2 host engine (window, input, networking, render, the
  full sim loop) with small, specific gameplay-**decision** functions compiled from PARENA mods
  and called into the host (`redgarden_host_tree_passive_strike` etc. — "PARENA mod is the
  trigger, host C does the real work," `ECOWAR/packages/simulation/arena_game.c`'s own established
  idiom). That pattern is real, proven, and directly reusable here.
- **PARENA's Java emitter (`src/emit_java.c`, 391 lines) is real but genuinely v0/embryonic** —
  checked directly, not assumed: it only accepts `defn`/`module`/`export`/`import` at the top
  level, only `I32`/`F64`/`Bool`/`String`/`Unit` scalar parameter/return types, and only
  numbers/symbols/plain-parameter-shape expression forms — **no `defstruct`, no loops, no `match`,
  no `Vec`, no collections at all**. Its one real, live precedent in this monorepo is
  `SPIDERBEETLE`: exactly two standalone scalar decision functions (a low-battery-warning check, a
  brightness clamp) compiled to Java and called from a hand-written native Android shell — nothing
  resembling a game engine, a grid, energy routing, or networking. **This is the real ceiling on
  "build it with PARENA so we target both Android and Windows... 1 client"** as literally stated
  today: PARENA cannot currently emit anywhere near a playable client to either target on its own,
  and the Java side specifically can only emit small, standalone, stateless helper functions —
  the same real, narrow slice `SPIDERBEETLE` already proved.
- **"1 client, 2 platforms, like Hearthstone" is the wrong mental model for what PARENA actually
  gives us today.** Hearthstone itself runs on a real cross-platform engine (Unity) that emits one
  client to many targets — PARENA is not that yet, and pretending otherwise would mean either
  under-delivering silently or blocking the whole project on PARENA's own emitter roadmap. The
  honest, buildable analog, matching this monorepo's own only two real precedents for "a game on
  two platforms" (`REDGARDEN`'s own stated "multiplayer + Android/iOS/Desktop cross-platform
  target" — aspirational, not yet built either — and `MJOLNIR`, the one real, live, native Android
  app in this monorepo) is: **two separate native shells** — a Windows C/SDL2 client (same
  architecture every other PARENA-integrated game here already uses) and an Android Kotlin/Java
  client (same architecture `MJOLNIR` already uses) — **sharing PARENA-compiled gameplay-decision
  modules wherever each emitter's real current capability actually reaches**, which today is
  "most of the C side, almost none of the Java side." The real cross-platform unification isn't a
  shared *engine*; it's a shared *server* (one Go/C matchmaker+arena-server pair, protocol-
  compatible with both clients, same as every other PARENA game's own server-authoritative
  architecture already is) plus a shared *design language* (see EOSUI below) — not a shared binary.
- **"we northstarred a meta ui framework yesterday" = real, found, not built yet:**
  `EMILY/docs/EOSUI_NORTHSTAR.md` (golden doc `EOSUI-NORTH`, kanban cards `EOSUI-1244`/
  `EOSUI-12444`, Apple #17813, `EMILY/BACKLOG.md` line ~30173) — a real cross-repo scoping pass
  that found **zero shared UI framework exists anywhere in this monorepo** (every game reinvents
  bespoke immediate-mode button-drawing code) and recommended, as the real buildable v0: **Option
  C**, a new `stdlib/ui/style.prn` extending PARENA's own existing (but currently one-widget-only)
  `stdlib/editor/widget.prn` with a small, real flexbox-*lite* property set (color, padding, row/
  column layout direction, font size) — explicitly rejecting a from-scratch HTML/CSS engine as
  disproportionate. **Not implemented anywhere yet.** This game's own spec is UI-dense (a shop
  panel, a 30-second ticker drawer, tabbed Black Market drawers, a 6x6 grid overlay, mobile
  thumb-zone layouts) in a way most PARENA games here aren't — making DEADWEIGHT a real, concrete
  second consumer for EOSUI's Option C (alongside BRAWLPIT's own already-scoped Phase 4), which is
  exactly the "this pattern here may be useful" instinct behind the founder's own reference to it.
  It still needs to actually get built — this repo does not unstick that on its own.
- **"full tracking of games and profiles etc, same model as REDGARDEN" — real, established,
  directly reusable:** `IDUNA`'s `players` table (`player_id`, `display_name`, `provider`,
  `provider_sub`, `email`, `kills`, `deaths`, `sessions`, `registered_at`, `last_seen`, and — since
  `202609050003_players_game_scope.sql` — a per-game `game` scoping column, S241's own real fix)
  plus per-match log files (`apps/arena_server`'s own `match_log_open`/`corpus_log_open`) plus
  Apple-based session tracking is the real, live pattern every PARENA-hosted arena game already
  uses. Directly reusable, no new design needed here — just a new `game='deadweight'` scope and a
  new M2M agent identity (`ECOWAR-BOTS`'s own real precedent, `IDUNA/migrations/truestore/
  202609110001_ecowar_bots_agent.sql`, is the template: **give this game its own agent identity
  from day one**, not REDGARDEN's/ECOWAR's, so match stats and any future leaderboard never mix).
- **"1v1, same bot pool set up as ECOWAR" — real, established, directly reusable:**
  `apps/matchmaker` (real, generic — the SAME binary already parameterized by `--lobby-size`/
  `--server-bin`/`--first-game-port`, no fork needed, just a new invocation on new ports) +
  `apps/arena_bot` (a real networked bot client, not the sim's internal practice-mode brain) is
  the exact reusable shape. **Real, honest gap this ask does NOT inherit for free**: ECOWAR's own
  bot AI (`arena_bot_tick`, `rl_policy_forward`) is a full MOBA hero-combat policy network trained
  against ECOWAR's own hero kit — it has no idea what a "polyomino" or "energy routing" is. A real
  bot for this game needs its own, new AI (even a simple heuristic placeholder, matching
  `arena_bot_enabled`'s own "1v1 team mode never uses the local-practice bot fallback" convention)
  before a real bot pool can exist — named here, not solved.
- **Guest accounts ("just create your name... don't have to connect it to an email but if you
  don't you can't get back into your account") — genuinely NEW, checked directly, not assumed.**
  `IDUNA`'s real, current player-auth model (`internal/http/handlers/register.go`,
  `internal/http/handlers/players.go`) supports exactly two `provider` values today: `"google"`
  (OAuth) and `"iduna_local"` (email + bcrypt password, `RegisterHandler`'s own doc comment: "Open
  registration: email + password + optional display_name"). **There is no name-only/anonymous
  provider anywhere in this codebase.** This is real, new work, not a config flag: a third
  provider (`provider="guest"`) whose `provider_sub` is a locally-generated, client-persisted
  secret token (lost on uninstall/data-clear = the real, honest "can't get back into your account"
  the founder is asking for — same real tradeoff Supercell's own guest-account-then-optional-
  linked-ID model makes), plus an upgrade path (`provider="guest"` → `"google"`/`"iduna_local"`,
  re-keying the same `player_id` rather than creating a second row) for the player who later
  decides to link an email. Real, honest, not yet designed in detail: the exact upgrade-flow
  wire shape and whether the guest secret lives in IDUNA at all or is purely client-side (leaning
  client-side: IDUNA should never need to trust an unauthenticated bearer of a guest secret with
  more than "resume this one player's own stats," a real security-scoping decision for the actual
  build pass, not this one).

## Recommended real V0 cut (a founder-level creative call, not yet actioned — matches the "real
fork named explicitly" precedent every other scoping doc in this monorepo already sets)

**Keep** (the real, shippable, genuinely novel core):
- 6x6 grid, polyomino items, three split classes (unsplittable/halvable/quarterable) with a real
  fragmentation tax.
- Energy-routing combat: generators → conductors → weapons/defenses, Dead Squares as armor,
  real-time auto-resolve once both players lock their grid.
- A small, fixed item catalog for V0 — **6-9 items**, not 24 (one clean Offense/Operations/Defense triangle
  member each, e.g. Iron Dwarf / Corrosion / Zhonya's Hourglass plus 3-6 more), enough to prove
  the rock-paper-scissors identity without needing years of live-service balancing before launch.
- A small, fixed set of **2-3 simple one-shot Ultimates** (not 16 abilities × 3 Flow tiers = 48
  effect descriptions) — earn one Flow-gated ability per match, keep the "why would anyone pick
  this becomes meta in 2 years" idea as a real, named *future* direction, not a V0 commitment.
- 1v1 ranked matchmaking + bot pool (ECOWAR's own real pattern) — **no tournament bracket for V0**.
  Double-elimination-3-wins-to-win with cross-match state carry-over is a real, substantial ranking
  system on top of a game that doesn't exist yet; a simple ladder/MMR (IDUNA's own existing
  `players.kills`/`.deaths`/`.sessions` columns are already most of what a V0 ladder needs) is the
  honest first step.
- Guest-account creation + the real, new IDUNA provider work named above.

**Defer, named explicitly so it isn't silently lost** (real future phases, not this pass):
- The full options-pricing/insurance/derivatives macro layer (Black-Scholes premium formula, 4
  policy tiers, the 30-second ticker, 4 exotic Mystery cards) — a real, distinctive differentiator
  worth building, but **after** the core spatial-combat loop is proven fun on its own merits; a
  fully financialized meta layer on top of an unproven core risks shipping neither well.
- Merkle-tree cargo-hiding — cosmetically clever, but the actual gameplay need ("don't let the
  opponent see my layout before combat locks") is already solvable with a plain "don't send it
  over the wire until combat starts" server-authoritative rule, the same real pattern this
  monorepo's own obstacle/fountain layouts already use (client and server independently compute
  or receive only what's needed, no cryptographic proof required for a two-party game with a
  trusted server in the middle). Genuinely unnecessary complexity for this game's actual trust
  model — named as a real, considered cut, not an oversight.
- 2v2 team battles, Link Modules, cross-grid energy tethering — real, fun-sounding, a true "V2"
  feature once 1v1 is live and has players.
- The mobile dual-axis haptic-engine timing table, the bitmask/`GridTileStatus` C++ data
  structures — real, useful engineering reference material once implementation actually starts;
  premature to lock in exact byte layouts before the core loop's own real shape (which fields the
  grid needs) is proven in a working prototype.

## Real holes found on a second pass (2026-09-11, same session)

Founder real-time: "review the document again it was written by gemini mostly flash so there may
be holes or opportunities for improving the spec." Full write-up: `docs/SPEC_REVIEW.md`. Real
findings, each resolved for V0 rather than just flagged: the transcript builds two incompatible
splitting systems (1-D quantized-fraction Knapsack, then a 2-D polyomino/Dead-Square system) and
never reconciles them — resolved by authoring fragment shapes per item rather than computing them
generically; no single-match win condition is ever stated (resolved: hull% at timeout, cargo value
as tiebreak); energy-routing conflict resolution for many-to-one wiring is unspecified (resolved:
one-to-many only, matching the Splitter Node's own real design); the real-time shared draft pool
has no stated race-condition rule (resolved: server-authoritative first-claim-wins with an
explicit "Sniped!" rejection); the `Naked Short` Mystery card is close to a free-roll exploit as
written (moot for V0 — the whole derivatives deck is already deferred — but named for whoever
designs that layer for real); and several 300-Flow Ultimates (`Realm Warp`, `Command: Shockwave`)
have no stated counterplay and are excluded even from the post-V0 catalog until redesigned, not
just deferred for complexity.

## Phased plan (not yet built)

Each phase below now has its own real, detailed spec doc in `docs/` — this section stays the
short index; see each doc for concrete tasks and acceptance criteria.

- [ ] **D0: repo hygiene.** `CLAUDE.md` (this session, alongside this doc), register `DEADWEIGHT-
  NORTH` in `EMILY/context/golden-docs-index.md`, add a `DEADWEIGHT` row to the root
  `/home/fatbaby/CLAUDE.md` repo table (every repo in this monorepo gets one).
- [ ] **D1: the core loop, single-player/local only** — `docs/PHASE_D1_CORE_LOOP.md`. Grid,
  polyomino placement, split mechanics +tax, energy routing, one weapon type, one defense type —
  playable against a fixed dummy grid, no networking yet. Proves the mechanical core is actually
  fun before a single line of server code exists.
- [ ] **D2: server-authoritative 1v1** — `docs/PHASE_D2_SERVER_AND_ACCOUNTS.md`. New
  `apps/deadweight_server` (hand-written C, same architecture as `arena_server`) +
  `apps/deadweight_matchmaker` (literally `apps/matchmaker` reused with new flags/ports) + a
  placeholder heuristic bot. IDUNA: new `game='deadweight'` scope, new `DEADWEIGHT-BOTS` M2M
  agent identity, guest-account provider (`provider="guest"`) built for real here — the one
  genuinely new IDUNA auth capability this whole ask needs.
- [ ] **D3: Windows client** — `docs/PHASE_D3_WINDOWS_CLIENT.md`. Hand-written C/SDL2 shell
  (`apps/deadweight_client`), same shape as `apps/arena`. First real PARENA mod integration for
  combat-decision logic (bot AI nudges, damage-resolution edge cases) — same "PARENA mod is the
  trigger, host does the real work" idiom.
- [ ] **D4: Android client** — `docs/PHASE_D4_ANDROID_CLIENT.md`. Hand-written native Kotlin/Java
  shell, `MJOLNIR`'s own real architecture as the template (not a fork of it — a new app).
  PARENA's Java emitter used only for the specific scalar decision helpers its real v0 capability
  actually reaches (mirroring `SPIDERBEETLE`'s own honest precedent) — full combat logic stays
  server-authoritative either way, so the Java emitter's narrowness is a real but non-blocking
  constraint, not a project-ending one.
- [ ] **D5: EOSUI Option C, shared** — `docs/PHASE_D5_D6_UI_AND_LAUNCH.md`. Once
  `stdlib/ui/style.prn` exists (a real, separate, cross-repo deliverable — `EOSUI-NORTH`'s own
  item, not owned solely by this repo), retrofit both clients' shop/ticker/tab UI onto it,
  becoming its second real consumer alongside BRAWLPIT.
- [ ] **D6: V0 launch bar** — `docs/PHASE_D5_D6_UI_AND_LAUNCH.md`. `bash scripts/build.sh` (or
  Bazel-equivalent, matching this monorepo's own convention) clean on both client targets + the
  server, live-verified 1v1 match (human vs. bot minimum, human vs. human if a second tester is
  available), Apple + CHANGELOG + commit/push per this repo's own `CLAUDE.md` protocol.

No code written yet — this pass is scoping only, matching `LO`, `MIXFORGE`, `PARENA`'s own LLVM-
backend scoping, and every other "real critical review before code" precedent this monorepo
already sets for a captured-transcript source document.

## Update 2026-09-18 (S503): VS0 build order changed

`docs/VS0_SCOPING.md` supersedes the D1–D6 *ordering* above for VS0: **card mode ships first** (small discrete
action space, no inventory UI, bot- and RL-friendly), Android-first over a TCP server-authoritative backend with a
bot pool of 3 from day one. The backpack battler (this doc's core mechanic, D1) becomes **VS1** on the same server
and wire protocol (`mode=1` reserved). The V0 item cut, deferrals, and hole findings in this doc all stand.

## Update 2026-09-24 (S537): online accounts + social features (friends/profiles/duels)

Founder real-time: "add iduna online accounts / add social features / profiles / friends /
friendly challenges (duels) / for DEADWEIGHT / WOTAN". This doc's own scoping above is
substantially overtaken by events elsewhere in the repo (the server, draft runs, tickets, and
guest/email accounts are all real and live — this section documents the social layer only, not a
rewrite of the doc above).

Real, checked-first state: IDUNA's per-game online-services layer (`game_online.go`) already gave
DEADWEIGHT full guest-register/guest-login/guest-upgrade/email-login accounts server-side, but
the web client (`web/src/client.ts`) never called any of it (`--no-auth server`) — the real gap
was client-side, not server-side. No friends/profile/duel concept existed anywhere in the
codebase before this.

Shipped this pass (IDUNA-side): `GET players/{id}/profile` (public), `POST`/`GET
friend-requests` + `{id}/accept`/`decline`, `GET friends` + `DELETE friends/{id}`, `POST`/`GET
duels` + `{id}/accept`/`decline`, all under `/api/v1/games/deadweight/...`
(`internal/http/handlers/game_social.go`). Friendship is derived from accepted `friend_requests`
rows; duels require an existing friendship. V0-scoped to the invite lifecycle only — an accepted
duel does not launch a live match yet. IDUNA `70b6b06`.

Shipped this pass (WOTAN-side): `friends.html` (login with a DEADWEIGHT account, manage
requests/friends/duels) and `profile.html` (public lookup). WOTAN `46e3ab0`.

Real, named, deferred (see `EMILY/BACKLOG.md` SECTION 537):
- Wiring `web/src/client.ts` itself to real accounts (the literal "online accounts for
  DEADWEIGHT" client-side gap).
- In-game friends/duel UI for both the web and native C clients.
- Duel Phase 2: turning an accepted duel into an actual live match instance (needs DEADWEIGHT's
  own ticket/queue match-start mechanism).
