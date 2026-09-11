# Phase D4 — Android client

**Goal**: a real, native Android client that talks to the exact same D2 server/matchmaker D3's
Windows client does — the actual cross-platform unification point for this project (one server,
two native shells), not "one PARENA-emitted binary for both platforms," which `NORTHSTAR.md`'s
own capability audit already found isn't real today. `MJOLNIR` is the template architecture (the
one real, live native Android app in this monorepo) — this is a new app built the way MJOLNIR
was, not a fork of it.

## Scope (in)

- A new Kotlin/Android Studio project, same real toolchain `MJOLNIR` already uses.
- Real touch UI for the mechanics D3 already proved work over the network: grid placement,
  rotation, Panic Cut, the draft pool, combat viewing. **This is the one place the transcript's
  own mobile-specific design work (tap-to-target selection instead of drag, double-tap/swipe
  rotation, long-press-for-Panic-Cut with haptic feedback, the vertical thumb-zone screen split)
  is directly, genuinely useful** — it was designed by the same transcript that got the mechanics
  right the first time, and none of `SPEC_REVIEW.md`'s findings touched the mobile-affordance
  design itself, only the underlying game rules it operates on. Reuse it.
- Networking: the same wire protocol D3's client speaks — a real Kotlin/Java implementation of
  `NetHeader` + the typed snapshot packets, not a redesign. This is genuinely the harder half of
  this phase relative to D3 (a brand-new network stack in a different language against an
  established binary wire format, versus D3 reusing C structs directly) and should be budgeted
  that way, not treated as symmetric effort to D3.
- PARENA's Java emitter, used for exactly the narrow slice its real v0 capability reaches
  (`NORTHSTAR.md`'s own audit: scalar `defn`s only, no structs/loops/collections) — the honest
  target here is a couple of small, standalone decision helpers (a damage-rounding rule, a
  UI-affordance gate like "is this placement even legal to attempt," anything genuinely stateless
  and scalar), matching `SPIDERBEETLE`'s own real, live precedent exactly. **Full combat logic
  stays server-authoritative regardless** — the Java emitter's narrowness is a real, named,
  non-blocking constraint precisely because nothing client-side needs to be authoritative in a
  server-authoritative game. This phase does not wait on PARENA's Java emitter maturing further.

## Scope (out)

- No offline/local-only mode — D1 already proved the mechanic is fun locally in C; D4 doesn't
  need to re-prove that in Kotlin. This phase is real-networked-client-only.
- No EOSUI styling (D5, same as D3) — this phase's UI is real and functional, matching D3's own
  bootstrapping order.
- No Google Play Store submission/signing/release-track work — that's a real, separate, later
  logistics phase once there's something worth shipping to a store.

## Acceptance criteria

- [ ] A human can complete a full real match on a real (or emulated) Android device against D3's
  Windows client or a bot, over the real network — the actual "1 client, 2 platforms" deliverable,
  correctly understood as two real native clients sharing one server, not one binary.
- [ ] The touch-affordance design (tap-to-target, double-tap rotate, long-press Panic Cut) is
  live-tested on a real touchscreen, not just reasoned about from the transcript's own diagrams —
  confirm it's actually comfortable one-thumb play before calling this phase done.
- [ ] Any PARENA-Java-emitted helper function round-trips for real (same bar D3's mod integration
  already set) — if the real v0 Java emitter can't cleanly reach even one small helper for this
  game's actual needs, that's a real, honest finding to report back to `PARENA`'s own roadmap, not
  something to paper over by writing the logic in plain Kotlin and pretending PARENA was used.
- [ ] This monorepo's own real, already-found gap (no Android SDK in this sandbox, confirmed
  during `SPIDERBEETLE`'s own build — even `MJOLNIR` itself has no `gradlew` checked out locally)
  means this phase's own build/test verification happens on the founder's own machine or real CI,
  not this sandbox — named here so it isn't silently assumed away when this phase starts.
