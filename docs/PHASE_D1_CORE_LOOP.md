# Phase D1 — the core loop, local-only

**Goal**: prove the 6x6 spatial-knapsack-into-combat mechanic is actually fun, with zero
networking, zero accounts, zero UI polish. A single local binary, one human player against a
fixed, hand-authored dummy grid. If this isn't fun, nothing built in D2-D6 saves it — this phase
exists specifically to find that out cheaply, before a server, two clients, and an account system
are all built around a mechanic nobody has actually played yet.

Depends on: `NORTHSTAR.md`'s V0 item cut, `SPEC_REVIEW.md`'s resolved rules (§1 fragment tables,
§3 routing, §7 port-bit convention) — this phase is where those decisions first become code, and
where any of them that turn out to be wrong in practice get caught earliest and cheapest.

## Scope (in)

- The 6x6 grid, `uint64_t` triple-bitmask representation (occupancy / static-blockade / ruined-
  real-estate) — the transcript's own §3.1 design is genuinely good engineering (compact, fast
  placement-legality checks via a single `&`) and is **adopted as-is**, not reinvented; this
  review isn't purely a teardown.
- 6-9 V0 items (`NORTHSTAR.md`'s own cut), each with an authored fragment table per
  `SPEC_REVIEW.md` §1 — data, not code, so tuning doesn't require a rebuild.
- Placement, rotation (the resolved port-bit convention from `SPEC_REVIEW.md` §7), and the Panic
  Cut (fracture-on-demand) interaction.
- Energy routing: generators → conductors/splitters → weapons/defenses, one-to-many only
  (`SPEC_REVIEW.md` §3), the Back-EMF closed-loop meltdown as the real, only failure mode for
  bad wiring (no separate "illegal wiring" error state needed — a closed loop already self-
  punishes, matching the transcript's own design there, which this review found no fault with).
- Combat auto-resolution against ONE fixed, hand-built dummy grid (not a real bot yet — that's
  D2's job, once there's a server to run it against). Hull-%-at-timeout as the win condition
  (`SPEC_REVIEW.md` §2).
- A bare-minimum debug render (immediate-mode boxes, matching every other game in this monorepo's
  own real starting point before any shared UI layer exists) — not the real client UI; that's D3.

## Scope (out, explicitly — not forgotten, just not this phase)

- Networking, matchmaking, accounts, the real-time shared draft pool (`SPEC_REVIEW.md` §4's
  resolution needs a server to enforce against; nothing to build here yet).
- The Black Market/shop, Ultimates, any of the deferred derivatives/Merkle/tournament/2v2
  systems named in `NORTHSTAR.md`.
- Real art, sound, or the EOSUI styling layer (D5).

## Concrete technical tasks

1. `ItemDef` data table: shape (bitmask), base weight/value stat block, fragment table per
   `SPEC_REVIEW.md` §1 (list of `(N, [fragment shapes with dead-square counts])` per split tier).
2. Grid placement/legality (`M_Item & (M_O | M_B | M_R) == 0`, the transcript's own real formula).
3. Panic Cut: replaces an item's occupied cells with its authored fragment set, flags the vacated
   difference (if any) into the Ruined Real Estate mask.
4. Rotation: apply the resolved bit-rotation to the port vector, re-derive the shape's occupied
   cells under the new orientation.
5. Energy tick: per-tick directional flow solver, single-input-per-conductor enforcement
   (reject/no-op the second connection rather than merge), Back-EMF accumulation
   (`Φ_loop(t) = Φ_0 · e^{k·t}`, shatter past `2.5·Φ_0`) exactly as specified — this formula was
   checked and found sound, no change needed.
6. Combat resolution loop: weapon fire on charge threshold, damage → hull%, 30s timeout →
   hull-% tiebreak (`SPEC_REVIEW.md` §2).
7. A scripted, deterministic dummy opponent grid (fixed layout, not AI) — enough to have a fight
   to watch/tune numbers against.

## Acceptance criteria

- [ ] A human can pack a legal grid from the V0 catalog within a real time budget (target: same
  45-second scramble window the transcript itself proposes — untested until this phase, but a
  real number to build the first playtest against, not left vague).
- [ ] Two hand-packed layouts (one aggressive, one defensive) produce visibly different combat
  outcomes against the same dummy opponent — the minimum bar for "the packing choices matter."
- [ ] A deliberately-built closed energy loop actually triggers Back-EMF meltdown and shatters
  the loop, not a crash or a silent no-op.
- [ ] A Panic Cut mid-fight actually changes routing (severs or reroutes an active energy path)
  live, not just at grid-lock time.
- [ ] No crash, no undefined-behavior sanitizer complaint, across a real fuzz pass of random
  legal placements (this monorepo's own established bar — every C game here ships with at least
  a headless smoke-test suite; this phase's own equivalent, `tests/test_core_loop.c`, is part of
  "done," not a follow-up).

## Real open question this phase should answer, not guess at

Is the fragmentation-tax mechanic (§1 of `SPEC_REVIEW.md`'s own review: splitting is *always*
weight-worse than keeping an item whole, so it's a pure spatial-fit tool) actually interesting
often enough in a real 6x6 grid, or does it only matter in the rare "one cell short" case and
otherwise sit unused? This is exactly the kind of question a working local prototype answers in
an afternoon of play and a design doc cannot answer by further discussion — the real reason this
phase exists before any server/client investment.
