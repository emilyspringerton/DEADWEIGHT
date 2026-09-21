# DEADWEIGHT

## What this is

New repo (2026-09-11); VS0 (card mode, Android-first) build started 2026-09-18 (S503) — see `docs/VS0_SCOPING.md`. Home for **Dark Sector: Hold Battles**,
a 1v1 real-time PvP spatial-knapsack auto-battler: players pack polyomino cargo items (some
splittable, at a real fragmentation-tax cost) into a 6x6 grid under a Black-Market draft phase,
then the same grid becomes their ship's combat layout — item shape/orientation routes energy from
generators to perimeter weapons/defenses, and the wasted "Dead Squares" from cutting an item
become armor. Source is a captured Gemini chat transcript (`DEADWEIGHT/LeetCode Skills Course
Curriculum.pdf` — misleadingly named; it starts as an actual LeetCode teaching curriculum and
spirals into the full game spec from "write a simple game that uses the first 6" onward), same
real provenance pattern `MIXFORGE/legacy.txt` already established. **Read `NORTHSTAR.md` before
assuming any of the transcript's own scope is this repo's real V0** — the transcript escalates
into a multi-year live-service feature catalog (full options-pricing derivatives engine, Merkle-
tree proofs, 24 items, 16 Ultimates, a double-elimination tournament bracket, 2v2 team battles);
`NORTHSTAR.md`'s own "Recommended real V0 cut" section names exactly what's in vs. deferred, and
why, before any of it gets built.

## Stack (VS0 in progress — see `docs/VS0_SCOPING.md`, `EMILY/BACKLOG.md` SECTION 503)

**VS0 = card mode first, Android-first, multiplayer + bots from day one.** Backpack battler (6x6 grid) is VS1.

- **Rules**: `PARENA/stdlib/deadweight/card_rules.prn` is the single source of truth (scalar-only), emitted to C
  (`core/card_rules.c`) and Java (`android/.../generated/CardRules.java`); regenerate with `scripts/gen_rules.sh`.
  **Firm constraint: no FFI is ever added to PARENA's Java target and the Java side makes no syscalls.** The Java
  emitter's real ceiling is scalar single-expression defns; anything beyond that is PARENA emitter work, scoped
  separately, never a workaround here.
- **Server**: hand-written C `dw_server` (TCP, `poll()`, many matches/process, `--fast-forward`, `--port`),
  authoritative; bots (`dw_bot`, pool of 3) and the training env speak the same wire protocol as humans
  (`docs/WIRE_PROTOCOL.md`). Training league = same binary on separate ports, PFSP 3-role (BRAWLPIT's `rl_league.py`).
- **Clients**: Android = hand-written Java shell (Bazel + rules_android, KARAMBIT precedent) over a plain-JVM
  `core_lib`; Windows = C (headless in VS0, SDL2 UI in VS0.5); Browser (`web/`, dev/VS0.5-web) = hand-written
  TypeScript over a WebSocket↔TCP bridge (`web/bridge/ws-tcp-bridge.js`, dumb byte relay — `dw_server` stays TCP-only),
  calling `web/src/generated/CardRules.ts` (PARENA's TypeScript emitter, `scripts/gen_rules.sh`) directly. See
  `web/README.md` for honest status/limits.
- **IDUNA**: `game='deadweight'` scope, `DEADWEIGHT-BOTS`/`DEADWEIGHT-RL` M2M agents, guest accounts
  (`provider="guest"`), game-scoped checkpoint registry — multi-tenant by default. Art via NOCK, not PARENA FFI.
- **Build**: `scripts/build.sh [--windows|--android|--all]`; CI in `.github/workflows/ci.yml`, auto minor-version
  releases on every green `main` push. **Clean builds first**: keep it green before adding anything.
- **Sandbox**: run Bazel with `HOME=/tmp/dw-home` (parent `go.work` leak), see `README.md`.

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

## CONSTRUCT File Generation (standing instruction, monorepo Principle 21)

DEADWEIGHT auto-generates a CONSTRUCT file on every release via CI (`.github/workflows/ci.yml`). The CONSTRUCT is a deterministic plaintext snapshot of all repo source files with SHA256 hashes and sizes — used for reproducible builds, audit trails, and offline source access.

The generation script (`scripts/generate_construct.sh`) uses `git ls-files` for byte-for-byte determinism and verifies itself on the second run (same tree → identical output). No manual work needed — it happens automatically during the release build.

See the main `CLAUDE.md`'s "Principle 21: CONSTRUCT Files" section for the full rationale and shared implementation patterns across the monorepo.

## README Reality — SAGA reconciliation (standing instruction, monorepo-wide)

Founder real-time, 2026-09-18: if a change of yours **substantially changes the claim of this project's core README**,
then per SAGA protocols (`EMILY/docs/SAGA_SYSTEM_AUDIT_2026-07-18.md`, HQ-SPEC-DOC-102: intent ↔ claim ledger ↔ reality)
you **must update `README.md` in the same unit of work** so it reflects current reality. The README is the project's public
claim; it must not lag behind the code.

- **When it applies:** a capability is added or removed; status moves ("design only" → "working", "planned" → "shipped");
  the stack, build, run or install steps change; a claim in the README is now false or stale; or you add a **meaningful,
  genuinely interesting piece of kit** (a new tool, engine capability, protocol, pipeline, game system). For that last case
  especially: put it in the README — what it is, how to run it, and its honest status and limits.
- **When it does not:** ordinary fixes, refactors and small features that leave the README's claims true.
- **How:** re-read the README against what you just changed; fix or delete stale lines (including "not built yet" notes that
  are now built); verify any new claim by actually running it, and mark anything untested as untested; commit the README
  with (or immediately after) the change, and mention it in the CHANGELOG entry.

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
