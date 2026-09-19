# Review: Haiku's card-expansion proposal (v1)

> **Terminology note (2026-09-19):** the game's three card kinds are now Offense (Red), Operations (Yellow) and Defense (Blue). This historical document has been mechanically re-worded to those names; its analysis predates the retheme (the middle kind was then a heavy/defensive one) and is kept as written otherwise.


Reviewed: `HAIKU_PROPOSAL_v1.md` (kept verbatim beside this file), checked line-by-line against
`docs/CARD_MODE_RULES.md`, `PARENA/stdlib/deadweight/card_rules.prn`, `docs/WIRE_PROTOCOL.md`, and the
C/Java hosts (`core/match.h`, `core/brain.c`, `core/protocol.h`, `android/.../MainActivity.java`).
Every finding below was checked against the code, not against Haiku's own paraphrase of it.

## Verdict

**Grade: C-.** Good instincts (variants on the triangle, side-effect mechanics, honest "deferred questions"
section), but it is not implementable as written. It contains rules-level errors that would ship a broken
game, a false central claim ("no wire changes"), and it under-scopes the change by roughly an order of
magnitude. Salvageable as a *menu of mechanic ideas*, not as a plan.

| Dimension | Grade | One line |
|---|---|---|
| Fidelity to actual rules | D | Gets the triangle direction wrong; invents fractional energy |
| Implementability (PARENA/wire/hosts) | F | "Wire: no changes" is false; ids 9+ break `card-kind` |
| Balance reasoning | D+ | One card strictly dominates; numbers are asserted, not derived |
| Test/validation plan | D | 100 matches cannot resolve any of its own thresholds |
| Mechanic ideas | B- | Siphon/Leech/Gambit/Overcharge/Spark are a fair starting menu |
| Honesty about gaps | B | The gaps section is the best part; it misses the biggest gaps |

## Blocking defects (would ship broken)

1. **The triangle is misapplied.** Proposal says Gambit (OFFENSE) "beats DEFENSE, ties OPERATIONS/OFFENSE".
   Actual: `kind-beats(a,b)` is true iff `b = a+1 mod 3` → **OFFENSE beats OPERATIONS; DEFENSE beats OFFENSE**.
   Gambit loses to DEFENSE (and DEFENSE deals its own power back) and beats OPERATIONS. The whole risk/reward
   analysis of Gambit is inverted. (The v1 meta prompt inherits and repeats similar slips.)

2. **`id = kind*3 + tier` breaks at id 9.** `card-kind(9) = 3`, `card-kind(12) = 4`; `kind-beats` on kind 3/4 is
   nonsense. "Siphon is a OFFENSE variant" is not expressible with the current id scheme: new cards need a
   table-driven kind (a scalar `if`-ladder, still Java-emittable) and `is-legal-play`'s hard-coded `<= 8`
   must change. The proposal never notices.

3. **"Wire protocol: no changes" is false.** Hand size 4→5 changes `ROUND_START` (`i8 hand[4]`), `PLAY` slot range
   (0–3 → 0–4), the mask, and `DW_HAND`/`hand[4]` in `match.h`, `protocol.h`, `client.c`, `policy.c`,
   `brain.c`, `apps/gui/main.c`, and the Android `MainActivity`/`Protocol.java` (4 `CardView`s, `weightSum 4`).
   "Clients ignore the extra slot" means a shipped APK silently can't see or play the fifth card. Auto-releases
   mean old APKs are in the wild. Needs a ruleset/protocol version (HELLO already has a `mode` byte).

4. **Fractional energy.** "Starting energy 2.5 → +2.5 per round." Energy is an integer (`u8`, `I32` in the
   rules). Not implementable; the knob it was trying to turn (economy tightness) needs an integer proposal.

5. **Overcharge is incoherent.** Cost 5 ≤ cap 6, so it is playable without exceeding the cap; "can exceed
   cap (lock at 7)" describes nothing the rules can do. Either the card is just "cost 5, power 14" (fine, say
   that) or it needs a cap change (huge blast radius, unmentioned).

6. **Spark strictly dominates.** Power `3 + energy/2`, cost 1. Tier-0 Offense is power 3, cost 1. Spark ≥ that
   at every energy value and hits 6 (a Tier-1 Offense's power at half the cost) at energy 6. There is no state in
   which Tier-0 Offense is the better card. Its own "pick rate ~5%" prediction is therefore fiction.

7. **Effects can't be returned by `damage-dealt`.** Heal, self-damage, and energy drain are multiple outputs;
   the rules module is single-expression scalar `defn`s. The proposal's "expand `damage-dealt` to take
   context and return damage" does not carry the other effects. The right shape is one small pure function per
   effect (`card-heal`, `card-backlash`, `card-drain`) plus host-side application — which then touches
   `match.c` resolution order (simultaneous heal vs. lethal damage: does Leech save you at hull 2?). Undefined
   in the proposal; must be specified.

8. **Protocol can't report the effects.** `ROUND_RESULT` carries `dmg_to_you/opp` as `u8` and final hulls; heal and
   backlash are recoverable only by subtraction, and drain not at all until next `ROUND_START`. Fine for
   correctness, but the client UI ("You took 3, dealt 6") will be wrong/confusing unless the result frame or
   the UI derivation is specified.

## Serious omissions (real cost, unmentioned)

- **Bot brain.** `DW_N_ACTIONS = 5`, obs dim 58 = 5 + 4 slots × 10 one-hot (9 cards + "empty"). `brain.c` maps any id
  outside 0–8 to the *empty* bucket: new cards would be **silently invisible to the live bots**. Hand 5 changes
  the action space and the observation; the weight blob's layer widths no longer match. Retrain/re-distill
  required; the three heuristic archetypes hard-code kinds.
- **Training env/league**: `dw_env.py`, `dw_rules.py` (Python port), the Colab league. All keyed to 9 cards/5 actions.
- **Parity corpus.** 1610 vectors cover 9×9(+pass); 15 or 16 cards ≈ triples it and must be regenerated *and*
  the hand-typed oracle in `test_rules.c` extended (the oracle is what stops generated code grading itself).
- **NOCK art.** Card faces/icons exist for 9 cards; new ids need art or a placeholder path.
- **Replay determinism.** Changing deck size changes every seeded shuffle; old match logs stop replaying.
- **The rest of the deck maths.** A hand of 5 from 15 with 8 rounds means ~13 of 15 cards are seen per player;
  with hand 4 from 9 the deck fully cycles. Variance profile changes; unmentioned.
- **Count inconsistencies.** Text says 6 new cards; the table lists 7; "15 cards, ids 0–14" but Spark is id 15
  (so 16). Small, but it means nobody counted.

## Validation plan: not fit for purpose

- **100 bot-vs-bot matches** gives a win-rate standard error of ~5 points, so "45–55%", "40–60%", "<30% pick
  rate" thresholds cannot be resolved. Use thousands (the fast-forward server does this in seconds).
- **Bots are heuristic/distilled**, so "pick rate" measures heuristic weights, not card power. Balance from bots
  is only meaningful with a league-trained policy or, better, *analytic* checks first.
- **"Win rate per card" is confounded** (cards played when ahead win more). Use marginal-effect measures
  (win rate when card is in the deck/hand vs. matched hands), or solve the one-round matrix game.
- **No analytic pass at all.** The cheapest high-value check is missing: enumerate every (card, energy) state and
  test for strict dominance (cost ≥ and power ≤ and no compensating effect). That would have caught Spark in
  seconds. See v2 meta prompt, gate G2.
- Thresholds are unmotivated ("no combo >40% of optimal" — of what?).

## What to keep

- Siphon (on-win energy denial), Leech (hull recovery capped at start-hull), Gambit (self-cost), a single
  "expensive finisher" — sound archetypes for a game whose only current decisions are bank-vs-spend and
  triangle reads.
- Keeping the core 9 untouched and adding variants rather than a 4th kind.
- The "honest gaps" habit; the Leech overheal cap decision (cap at start-hull).

## Recommended re-scope (what a good proposal would have said)

Classify by blast radius and ship the cheap tier first:

- **Tier A — corpus only** (hand stays 4, 5 actions, wire frames unchanged): new card ids 9..N via a table-driven
  `card-kind/tier/cost/power` plus per-effect scalar fns; new **ruleset id** so shipped clients don't
  mis-render; regenerate C/Java/parity; extend the NOCK art; widen the obs one-hot (10→N+1) and re-distill
  the bots. Ideal home for Siphon/Leech/Gambit/Spark-fixed.
- **Tier B — new host state or wire fields** (hand 5, persistent statuses, new result fields): protocol version bump,
  every host, retrain. Defer until Tier A data exists.
- **Tier C — new kinds / cap or round changes**: re-derive the whole economy. Out of scope.

Hand-size change should be justified on its own evidence, not bundled into "add cards".

## Suggested next step

Do not build from v1. Run the v2 meta prompt (`METAPROMPT_v2.md`) constrained to Tier A, keep its outputs as
*proposals*, and put every candidate through gates G1–G4 before any `.prn` edit.
