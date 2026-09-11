# DEADWEIGHT

## What this is

New repo (2026-09-11), scoping-only so far — no code yet. Home for **Dark Sector: Hold Battles**,
a 1v1 real-time PvP spatial-knapsack auto-battler: players pack polyomino cargo items (some
splittable, at a real fragmentation-tax cost) into a 6x6 grid under a Black-Market draft phase,
then the same grid becomes their ship's combat layout — item shape/orientation routes energy from
generators to perimeter weapons/shields, and the wasted "Dead Squares" from cutting an item
become armor. Source is a captured Gemini chat transcript (`DEADWEIGHT/LeetCode Skills Course
Curriculum.pdf` — misleadingly named; it starts as an actual LeetCode teaching curriculum and
spirals into the full game spec from "write a simple game that uses the first 6" onward), same
real provenance pattern `MIXFORGE/legacy.txt` already established. **Read `NORTHSTAR.md` before
assuming any of the transcript's own scope is this repo's real V0** — the transcript escalates
into a multi-year live-service feature catalog (full options-pricing derivatives engine, Merkle-
tree proofs, 24 items, 16 Ultimates, a double-elimination tournament bracket, 2v2 team battles);
`NORTHSTAR.md`'s own "Recommended real V0 cut" section names exactly what's in vs. deferred, and
why, before any of it gets built.

## Stack (planned, not yet built)

Two separate native client shells sharing one server-authoritative backend — **not** a single
PARENA-emitted binary for both platforms; see `NORTHSTAR.md`'s own real capability audit of
PARENA's C vs. Java emitters for why "1 client, 2 platforms" doesn't mean what it sounds like yet:

- **Server**: hand-written C, same server-authoritative UDP architecture as `REDGARDEN`/`ECOWAR`'s
  `apps/arena_server`. Matchmaker + bot pool reuse `apps/matchmaker`'s own real, already-generic
  binary (`--lobby-size 2`, new ports) — no fork needed.
- **Windows client**: hand-written C/SDL2, same shape as `apps/arena`.
- **Android client**: hand-written native Kotlin/Java, `MJOLNIR`'s own architecture as the
  template (a new app, not a fork).
- **PARENA**: mod-first for gameplay-decision logic on the C side (mature emitter, this
  monorepo's own established "PARENA mod is the trigger, host does the real work" idiom); on the
  Java side, only the narrow scalar-helper slice `SPIDERBEETLE` already proved is real today —
  named as a real, non-blocking constraint in `NORTHSTAR.md`.
- **UI**: `EOSUI-NORTH`'s own recommended Option C (`stdlib/ui/style.prn`, PARENA-native
  flexbox-lite styling) once it exists — this repo is a real, planned second consumer alongside
  BRAWLPIT, not the owner of building it.
- **Accounts**: IDUNA `players` table, new `provider="guest"` (name-only, no email — genuinely new
  work, not built anywhere in IDUNA yet, see `NORTHSTAR.md`), new `game='deadweight'` scope
  (`202609050003_players_game_scope.sql`'s own real per-game scoping), new `DEADWEIGHT-BOTS` M2M
  agent identity (`ECOWAR-BOTS`'s own real precedent).

## Related Repos

- `REDGARDEN` / `ECOWAR` — the real, live server-authoritative PARENA-arena-game precedent this
  repo's server/matchmaker/bot-pool/IDUNA-tracking architecture is directly copied from.
- `MJOLNIR` — the one real, live native Android app in this monorepo; the template for this
  repo's own Android client, not a fork of it.
- `PARENA` — mod-first gameplay logic on both emit targets; see `NORTHSTAR.md` for the real,
  current maturity gap between its C and Java emitters.
- `EMILY` — RSI loop / backlog coordination (`EMILY/BACKLOG.md` SECTION 370 for this repo's own
  scoping); `EOSUI-NORTH` golden doc for the shared UI styling layer this repo plans to consume.
- `IDUNA` — player accounts, match tracking, M2M agent identity; the guest-account provider this
  game needs is real, new work tracked here, not assumed to already exist.

## Founder Real-Time Direction

Whenever the founder gives real-time direction — a new ask, a correction, a "can we also..." —
route it through `emily observe -s info "Founder real-time: <summary>"` first, even if it isn't
this repo's usual domain, then sprint-plan it into `EMILY/BACKLOG.md` (a real scoped SECTION/
sub-item, not just a one-line log), and only then implement. See `EMILY/docs/THE_EMILY_WAY.md`
Principle 18 ("Pave the Cow Paths").

## Apple Filing Protocol

After any meaningful change, file an Apple:
```bash
emily apples post -t completion -repo DEADWEIGHT "<title>" "<body with commit hash>"
```
Then mark the item done in `EMILY/BACKLOG.md` and commit.

## CHANGELOG Protocol

After any meaningful change, update CHANGELOG.md:
```bash
emily changelog add DEADWEIGHT "<what changed>"
# or manually: append a dated bullet under ## YYYY-MM-DD in DEADWEIGHT/CHANGELOG.md
```

## Frame-Break Reframing

Founder-sourced prompting technique (REDGARDEN/NORTHSTAR.md §28, full origin in
REDGARDEN/docs2/MULTI_AGENT_RD_RESEARCH_NOTES.md §5): given a request, name the underlying
structural/systemic pattern it's one instance of — one level of abstraction up — as an added
lens during planning/triage/judgment calls. Use it to spot the general case behind a specific
ask. It augments judgment, it does not replace doing the work: direct, concrete execution of
the literal task asked for still happens every time.

## Commit Protocol (standing instruction)

Always commit and push completed work immediately — don't wait to be asked. This is the default
for every repo in this monorepo.

Every commit — human-written or produced by automated code paths — must carry the active `emily
session` fingerprint as a `session: <tag>` trailer (blank line, then the trailer).
