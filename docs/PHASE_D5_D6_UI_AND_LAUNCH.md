# Phases D5-D6 — EOSUI retrofit and the V0 launch bar

Shorter than D1-D4 on purpose: both are real, necessary phases, but neither is a design-risk
phase the way D1 (is the mechanic fun) or D2 (is the guest-account tradeoff real) are — they're
finishing work, gated on things outside this repo's own critical path.

## Phase D5 — EOSUI Option C retrofit

**Goal**: replace both clients' bespoke shop/draft-pool/tab UI with `stdlib/ui/style.prn`
(`EOSUI-NORTH`'s own recommended Option C), becoming its second real consumer alongside BRAWLPIT.

**Gated on**: `stdlib/ui/style.prn` actually existing. This is explicitly **not** this repo's own
deliverable to build — `EOSUI-NORTH` is a separate, cross-repo golden doc with its own real open
questions (`EMILY/docs/EOSUI_NORTHSTAR.md`'s own "which game gets the first real widget catalog"
question, not yet answered). D5 does not block D1-D4; it's scheduled to start whenever Option C
lands, whether that's before or after D3/D4 ship their own bespoke first-pass UI.

**Scope**: retrofit only — D3/D4's own grid/combat rendering stays exactly as it is (that's game
logic, not "interface," and EOSUI's own scope was never the grid itself); the shop panel, draft
tray, and any tabbed/drawer UI move onto the shared styling layer. A real, concrete before/after
worth keeping as evidence this repo actually used what it consumed: same UI, same behavior, less
bespoke per-game layout code.

**Acceptance criteria**:
- [ ] At least the shop/draft UI on both clients is rendered through `stdlib/ui/style.prn`, not
  bespoke immediate-mode layout code.
- [ ] No regression in D3/D4's own already-passing acceptance criteria — a shared UI layer that
  breaks a working feature to adopt is a net loss, not a win.

## Phase D6 — V0 launch bar

**Goal**: the actual "is this done" checklist for a real V0 — not a feature list, a *quality* bar,
matching every other game in this monorepo's own real launch discipline (REDGARDEN's/ECOWAR's own
`scripts/test_arena.sh` clean-suite bar, live-verified end-to-end matches, not just "it compiles").

**Checklist**:
- [ ] Both clients (D3 Windows, D4 Android) build clean from a fresh checkout, no manual
  undocumented setup steps.
- [ ] Server + matchmaker + bot pool build clean and pass a real headless test suite
  (`tests/test_*.c`, one file per subsystem — grid/split/routing from D1, draft-race resolution
  and guest-account flows from D2).
- [ ] A real, live-verified 1v1 match, human vs. bot at minimum (human vs. human if a second
  tester is available at launch time) — screen-recorded or otherwise durably evidenced, the same
  "live round-trip tested, not just compile-checked" bar this whole monorepo holds every PARENA
  mod to, applied here to the whole game.
- [ ] A guest account created fresh, played a match, and closed — confirmed its stats persisted
  and (separately) confirmed it's genuinely unrecoverable without its token, per D2's own
  acceptance criteria — re-verified here specifically because "did the launch build actually keep
  this working" is a different question than "did D2 prove the design once."
- [ ] `SPEC_REVIEW.md`'s open question from D1 (does the fragmentation-tax mechanic feel
  interesting often enough, or only in rare forced-fit moments) has a real answer from actual
  playtesting by this point, not just a hope it works out — if the answer turned out to be "rare
  and thin," D6 is the point to decide whether that's acceptable for V0 or needs a real design
  revisit before calling this launched.
- [ ] Apple filed, `CHANGELOG.md` updated, everything committed and pushed, per
  `DEADWEIGHT/CLAUDE.md`'s own standing protocol — the same close-out discipline every other
  phase in this plan already follows, named explicitly here because D6 is the point nothing is
  deferred past.

No new design decisions get made in D6 — everything it checks was already decided in D1-D5 or in
`SPEC_REVIEW.md`. It exists to catch the gap between "the plan says this phase is done" and "this
phase is actually, verifiably done," the same gap this monorepo's own incident history (the
`ecowar-matchmaker.service` dead-for-5-days story, the `S170-186` "the build script never built
the real client" story) keeps finding whenever it isn't checked for on purpose.
