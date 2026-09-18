# Card mode — VS0 rules (v0, tunable)

Design proposal for VS0; every number lives in `PARENA/stdlib/deadweight/card_rules.prn` (or
`match.c` constants where marked) so tuning never touches networking or UI. Scalar-only on purpose: it must
compile through *both* the C and the Java emitter (no structs/loops/collections in the rules module).

## Match

- 2 players (seats 0/1). Each starts **hull 20**, **energy 2**.
- Fixed catalog of **9 cards**, ids 0–8. No deckbuilding in VS0. Every player draws from the same 9.
- Each player holds a **hand of 4**, dealt without replacement from a seeded shuffle of the 9 (per-player
  shuffle, seed derived from the match seed and seat; when the draw pile is empty, reshuffle the discard).
  Hands are hidden: a client sees its own hand and only the opponent's hand *size*.
- **Rounds 1..8.** Round start: each player gains **+2 energy (cap 6)**. Both players secretly **lock one
  action**: play card in hand slot 0–3, or **pass** (slot −1). A card is legal iff `cost ≤ energy`.
  Playing spends its cost; passing banks energy (+1 bonus energy, still capped at 6).
  Played card is discarded; the slot refills from the draw pile.
- Timer (live play only): 20 s. Timeout = pass. `--fast-forward` disables timers.
- Illegal action → server rejects, client may resend until the deadline; still nothing after deadline = pass.

## Cards (`id = kind*3 + tier`, tier 0..2)

| kind | id 0,1,2 / 3,4,5 / 6,7,8 |
|---|---|
| 0 BURST  | ids 0–2 |
| 1 TANK   | ids 3–5 |
| 2 SHIELD | ids 6–8 |

| tier | cost | power |
|---|---|---|
| 0 | 1 | 3 |
| 1 | 2 | 6 |
| 2 | 4 | 10 |

## Resolution (simultaneous reveal)

Triangle: **BURST > TANK > SHIELD > BURST.**

For plays *a* (yours) vs *b* (theirs), `damage(a → b)` = hull damage *a* deals to the other player:

- *a* beats *b's* kind: *a* deals `power(a)`; the countered card deals 0. (A SHIELD that counters a BURST
  therefore *reflects* its own power back; a SHIELD never deals damage otherwise.)
- Same kind: BURST vs BURST both deal full power; TANK vs TANK both deal `power/2` (integer div);
  SHIELD vs SHIELD nothing.
- *a* is countered by *b's* kind: 0.
- *b* passes: *a* deals `power(a)` if BURST or TANK, 0 if SHIELD.
- *a* passes: 0.

Both damages apply simultaneously.

## End

- Hull ≤ 0 ends the match immediately after the round resolves: the other player wins; both ≤ 0 = draw.
- After round 8: higher hull wins; equal hull = draw. (NORTHSTAR's "hull% at timeout" rule, minus cargo-value
  tiebreak — VS0 has no cargo.)
- Disconnect/abandon: opponent wins (`reason=forfeit`); bots never forfeit.

## Why this shape

One 5-way discrete action per round (slot 0–3 or pass) → trivially maskable for RL, trivially scriptable for
heuristic bots. Hidden hands + energy banking make it more than rock-paper-scissors: a tier-2 Burst costs 4,
so *saving* is a readable tell the opponent can punish with a Shield. The Burst/Tank/Shield triangle is the
same one `NORTHSTAR.md` names for backpack items, so card stats become item stat blocks in VS1.

## PARENA rules module API (all `I32`/`Bool`, single-expression, dual-emitted C+Java)

`card-kind(id)` · `card-tier(id)` · `card-cost(id)` · `card-power(id)` · `is-legal-play(id, energy)` ·
`damage-dealt(a-id, b-id)` (id `-1` = pass) · `round-start-energy(e)` · `energy-after-play(e, played-id)` (`-1` = pass banks +1) ·
`round-winner-by-hull(h0, h1)` (0 = seat 0, 1 = seat 1, 2 = draw) · `match-decided(h0, h1)` (Bool).
Constants are zero-parameter defns (`start-hull`, `start-energy`, `max-rounds`, `hand-size`, `num-cards`) —
verified to emit through both the C and Java targets — so hosts read them from the rules, not from copies.
