# Spec Review — real holes in the upstream transcript, and the rules chosen to close them

Second pass over `DEADWEIGHT/LeetCode Skills Course Curriculum.pdf`, at the founder's own request
("review the document again it was written by gemini mostly flash so there may be holes or
opportunities for improving the spec"). `NORTHSTAR.md`'s first pass named the transcript's
*scope* problem (a multi-year live-service catalog, not a V0) and cut it down. This pass is
different: it goes looking for *correctness and completeness* problems inside the parts of the
spec that made the V0 cut — the 6x6 grid, the split/tax mechanic, energy routing, and the match
loop — and, where a hole would block real implementation, **decides the rule** rather than just
flagging it, the same way `NORTHSTAR.md`'s own V0 cut was a real decision, not a list of options
left open.

## The root cause, named once so it doesn't need repeating per-finding

A long single-session chat transcript with a fast, low-latency model, where every new user prompt
("now add X," "evolve this into Y," "combine A and B") gets answered as a pure *addition* on top
of everything said before, with no editorial pass ever re-reading the whole thing for consistency.
That production process is exactly why the holes below exist — not because any one answer is
wrong in isolation, but because two answers 30 turns apart were never checked against each other.
Knowing the cause doesn't excuse skipping the review; it's why this review is needed at all before
any of this becomes code.

## 1. Real inconsistency: two incompatible "splitting" systems were never reconciled

The transcript builds splitting mechanics **twice**, in two different geometries, and never
merges them:

- **Turns ~8–14** (the 0/1 Knapsack evolution): items live in a 1-D weight budget ("Tons"). A
  split item becomes 2 or 4 *quantized fractions*, each with a flat per-piece weight tax and
  value scaled down proportionally — `"One Half: Weight = (W/2)+1, Value = V/2"`. This system is
  internally consistent and its own math checks out (see §2 below).
- **Turns ~15 onward** (the card-game pivot, "design it as a card game... backpack battles"):
  items become 2-D **polyomino shapes** on a 6x6 grid. Splitting now produces smaller polyomino
  *fragments* carrying **Dead Squares** (wasted grid cells, not a weight number), and combat is
  driven by shape/orientation/adjacency, none of which exist in the 1-D model at all.

The second system is the real one — it's what every later turn (energy routing, Ultimates, the
mobile UI, the tournament) actually builds on — but the transcript never says so explicitly, and
the fragment tables it does give are **per-item flavor examples, not a general rule**:

| Item (base shape) | Split ratio | Fragment shape given |
|---|---|---|
| Titanium Beam (1x4 line) | Halves | 1x3 (2 base + 1 dead) |
| Aether Ore (2x2 square) | Quarters | 1x2 (1 base + 1 dead) |
| Xeno-Biomass (4-square L) | Thirds | 1x2 ("total grid usage goes from 4 to 6") |

Three items, three unrelated fragment-shape rules, with no formula connecting base shape → split
count → fragment shape → dead-square count. A fourth item added later would need its own
hand-invented rule, forever, with no way to check a new one for consistency against the first
three.

**Resolved for V0**: fragment shapes are **authored per item, not computed at runtime.** Each
item's data definition carries an explicit list of *N* fragment polyominoes (shape + dead-square
count) for each of its split tiers, exactly like the three examples above — a lookup table, not a
geometry algorithm. Runtime polyomino subdivision (given an arbitrary base shape, produce *N*
legal, tileable sub-shapes) is a real, hard combinatorial-geometry problem with no gameplay payoff
proportional to its cost for a fixed, small (6-9 item) V0 catalog — the same "disproportionate
cost for the actual stated need" judgment `NORTHSTAR.md` already made about a full HTML/CSS engine
for EOSUI. If the item catalog grows enough post-V0 that hand-authoring every fragment set becomes
the real bottleneck, *that's* the point to revisit a generative rule — not before.

## 2. Real gap: no win condition is ever actually stated for a single match

The transcript describes combat ending ("Once one hull integrity bar reaches 0% (or a 30-second
combat limit is reached), the screen halts. The enemy ship warps away or explodes...") but never
says what happens **at the 30-second limit if neither hull hits 0%.** Every later system that
assumes a match produces a clean winner — the 3-Win Double-Elimination bracket, "you go up the
winners bracket / down the losers bracket" — silently assumes this question is already answered.
It isn't. A bracket cannot advance a draw.

(V0 cuts the bracket itself per `NORTHSTAR.md`, but the *single-match* win condition is still
load-bearing for even the simplest ladder — "who gets the MMR win" needs the same answer.)

**Resolved for V0**: hull integrity is the primary win condition (0% first = loss, matching the
transcript's own framing everywhere else). On a 30-second timeout with both ships still standing,
**remaining hull integrity % is the tiebreak** (simplest, most legible outcome to show a player —
"you had more ship left" needs no further explanation), with total intact cargo value as a
secondary tiebreak only in the (rare, symmetric-timeout) case of an exact hull-% tie.

## 3. Real gap: energy-routing conflict resolution is never specified

Generators, conductors, splitters, and weapons are all given directional ports and an
`Φ_out = Φ_in · η − R_status` transmission formula, and a `Splitter Node` explicitly turns *one*
incoming stream into *two* outgoing ones — but nothing says what happens on the reverse case: two
separate conductor paths feeding into the **same** weapon or the same downstream conductor. Do the
two inputs sum? Does the higher-Φ path win and the other back up (feeding the Back-EMF meltdown
math)? Is this simply illegal wiring? The transcript's own worked examples (Buzz's `[RAGEBLADE]`
diagram, the Support→ADC "Link Module" diagram) are all single-path chains — the many-to-one case
is never drawn or described, even though the grid's own topology makes it trivially easy for a
player to build by accident.

**Resolved for V0**: **one-to-many is legal (a Splitter Node), many-to-one is not.** A conductor
or weapon may accept energy from exactly one upstream port; a second incoming connection attempt
is treated the same as a dead-end (the port simply doesn't conduct — no damage, no explosion, just
an inert wire), not a merge or a conflict. This is the smallest rule that avoids inventing a real
flow-summation/priority model the spec never asked for, keeps routing legible on a phone screen
(a player can trace one continuous line per weapon), and can be relaxed into real summation later
if a build variety problem shows up in actual playtesting — not guessed at now.

## 4. Real gap: the shared real-time draft pool has no stated tie-resolution rule

`"The Debris Field... shared, simultaneous grab... Once a piece is grabbed, it is gone from the
pool."` For a genuinely real-time, multi-client draft, two players *will* tap the same item within
the same network round-trip sometimes — this is not an edge case, it's a certainty at any real
scale. The transcript never addresses it: does the losing client see the item silently vanish
mid-drag with no explanation? Does the server just process claims in arrival order and hope
latency differences don't feel unfair?

**Resolved for V0**: server-authoritative, first-claim-received-wins (the exact same "same-box
retry-race" resolution discipline `apps/matchmaker`'s own existing `recently_matched` logic
already uses for a different race in this monorepo) — and the **losing** claimant gets an explicit
rejection response the client renders as a real, named state ("Sniped!" — a flash + the item
snapping back to available, not a silent disappearance), rather than assuming the client's own
optimistic removal was correct. This is a real, necessary piece of "online services... full
tracking," not a nice-to-have — a draft phase that silently desyncs under latency is a support-
ticket generator, not a shipped feature.

## 5. Real, named balance risk: the "Naked Short" Mystery card is close to a free-roll exploit as
written

`"Naked Short: Grants $1,500 cash up front. If that item takes even a single point of damage
before the next 30-second ticker, you instantly default... If it survives, you keep the cash
completely scot-free."` As written, buying this *after* the point in a match where a player can
already tell they're winning (their exposed item is no longer in danger) is close to free money
with no real downside — not the "high-stakes gamble" the design intends. This isn't a scope
question (the whole Exotic/Mystery derivatives deck is already deferred past V0 per
`NORTHSTAR.md`) — it's a real, findable balance hole in the ruleset **as written**, worth
recording now so whoever designs that layer for real doesn't rediscover it the hard way: any
version of this card needs either a cost that scales with remaining match time (closing the
"buy it once the coast is clear" window) or a default condition broader than "this one specific
already-safe item."

## 6. Real, named fun/fairness risk: several Ultimates have no counterplay as written

`Realm Warp` at 300 Flow ("Spatial Collapse: Swaps the structural layouts of both grids entirely
for the rest of the round. You now control their ship weapons and inventory configuration, and
they control yours") and `Command: Shockwave`'s 300-Flow tier (flips the enemy's entire grid
upside down) are total, one-button state swaps with no telegraph, no partial mitigation, and no
stated way for the target to reduce or resist the effect. A design built around 16 abilities ×
3 tiers already accepts that a few should be "why would anyone pick this" until players solve the
game (the transcript says so explicitly, and that's a real, fine design goal) — but "why would
anyone pick this" and "the opponent has no counterplay to what I picked" are different problems.
The first is a discovery curve; the second is a fairness bug.

**Resolved for V0**: none of the full-grid-swap-shaped Ultimates make the V0 candidate list.
`NORTHSTAR.md` already cut the Ultimate catalog to 2-3 simple, single-target abilities for V0 —
this review adds the explicit reason *why* `Realm Warp`/`Command: Shockwave`/`Hostile Takeover`
specifically are excluded even from the future post-V0 catalog without a real redesign (a
telegraph window, a partial/resistible version, or a cost proportional to how much control it
actually takes away) — not just deferred for being complex, but flagged as needing real design
work before they're safe to ship at all.

## 7. Smaller, real, worth-a-sentence-each items

- The rotation math (`V'_C = (V_C ≫ 1) | ((V_C & 1) ≪ 3)`, a 4-bit cyclic rotate for a port
  vector declared as `[N, E, S, W]`) is only correct if the bit-index-to-direction mapping is
  fixed and consistent everywhere a port is read or written — the transcript never states which
  bit is which. **Resolved for V0**: bit 3=N, bit 2=E, bit 1=S, bit 0=W (matches the array's own
  left-to-right order), documented once here so `D1_CORE_LOOP.md`'s own implementation has a
  single source of truth instead of re-deriving it.
- The three scoring currencies the spec accumulates across its own turns — cargo dollar value,
  combat hull%, and (deferred) derivatives P&L — are never reconciled into one "did I win"
  statement; §2 above resolves this for V0 by making hull% (not cash) the actual win condition,
  cash stays a meta-progression/cosmetic-economy number, not a way to win a match outright.
- No disconnect/reconnect behavior is specified anywhere (unsurprising — the transcript never
  touches networking at all, this whole layer is this monorepo's own addition). `D2_SERVER_AND_
  ACCOUNTS.md` names the real, already-solved precedent (`REDGARDEN`'s own requeue-after-a-
  networked-match handling, S170-148's obstacle-resync bugfix as the cautionary tale for "don't
  assume client state survives a reconnect cleanly") to build against instead of re-discovering
  the same bug class from scratch.

## What this review does *not* do

It does not re-litigate `NORTHSTAR.md`'s own V0 scope cut — that's still the standing decision.
It does not touch the LeetCode-curriculum half of the source PDF, which was never this repo's
concern. And it does not invent new features — every "resolved for V0" above closes a real gap
the transcript left open, using the smallest rule that makes the existing design implementable,
not a new design.
