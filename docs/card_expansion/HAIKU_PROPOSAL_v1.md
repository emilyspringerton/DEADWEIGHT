# DEADWEIGHT Card Expansion Proposal

> **Terminology note (2026-09-19):** the game's three card kinds are now Offense (Red), Operations (Yellow) and Defense (Blue). This historical document has been mechanically re-worded to those names; its analysis predates the retheme (the middle kind was then a heavy/defensive one) and is kept as written otherwise.


## Executive Summary

DEADWEIGHT VS0 (card mode) ships with a proven, tight design: 9 cards in a 3×3 Offense/Operations/Defense × tier 0/1/2 matrix, clean RPS triangle, 8-round decision trees. This proposal adds strategic depth without breaking the core economy — six new special-purpose cards introducing conditional mechanics, energy manipulation, and risk/reward trades while preserving the triangle's integrity and the hand-decision space.

## Current State (VS0 Baseline)

| Metric | Value | Notes |
|--------|-------|-------|
| Cards | 9 (ids 0–8) | OFFENSE/OPERATIONS/DEFENSE, tiers 0/1/2 |
| Hand size | 4 | Drawn without replacement |
| Hull | 20 start | Simultaneous damage, draw at ≤0 both |
| Energy | 2 start, +2/round, cap 6 | Pass banks +1 (capped) |
| Rounds | 8 fixed | Round-based resolution |
| Triangle | OFFENSE > OPERATIONS > DEFENSE > OFFENSE | Equal kind: OFFENSE/OFFENSE full, OPERATIONS/OPERATIONS half, DEFENSE/DEFENSE 0 |
| Pass vs. card | OFFENSE/OPERATIONS deal full; DEFENSE deals 0 | Energy banking incentive |

**Balance health indicators:**
- RPS triangle constrains best play; tier-2 cards cost 4 (66% energy), forcing timing
- 8 rounds limits snowball; hull economy is tight (tier-2 Offense deals 10 from 20 hull)
- Hidden hands + energy banking create readable tells (saving 4 energy for a big play)

## Proposed Additions

### New Card Mechanics (Scalar-Only Constraints)

To preserve the scalar-only rule (no loops/state machines), new mechanics must be **deterministic functions of current game state** — energy, hull, last round's outcome. We expand `damage-dealt` to accept context:

```
damage-dealt(a-id, b-id, a-energy, a-hull, b-hull, prior-a-damage) → damage
```

No state persistence across rounds; all calculations are immediate.

### 1. Siphon Cards (new mechanic: damage + energy drain)

**Rationale:** Creates a tension against maxing energy cap. Siphon costs more but converts damage into board control (energy advantage for next round). High skill ceiling: spending to win energy trades.

**Card: Siphon Tier 1 (id 9)**
- Cost: 2, Power: 5, Siphon: 1
- Damage: 5 (normal RPS rules)
- Effect: If this card beats opponent's kind, steal 1 energy from opponent (cap their energy at 5 instead of 6 for next round)
- Integration: Pass siphon-amount to next round's energy-cap calculation
- Scalar implementation: `if (kind-beats(a, b)) opp-energy = min(5, opp-energy) else 6`

**Card: Siphon Tier 2 (id 10)**
- Cost: 3, Power: 8, Siphon: 2
- Similar to Tier 1, stronger trade-off

### 2. Leech Cards (new mechanic: damage + self-heal)

**Rationale:** Provides a comeback-window mechanic without full reset. Leech cards cost more and deal less direct damage, but recoup hull. Creates hull-preservation tactics.

**Card: Leech Tier 1 (id 11)**
- Cost: 2, Power: 4, Heal: 2
- Damage: 4 (normal RPS)
- Effect: Heal self for 2 hull (after round resolves)
- Scalar: `your-hull += 2` post-resolution
- Strategic use: Play on round 5-6 to stay above kill-threat threshold

**Card: Leech Tier 2 (id 12)**
- Cost: 3, Power: 7, Heal: 3
- Strong comeback tool; ties up energy but cheaper than tier-2 OFFENSE

### 3. Gambit Card (new mechanic: damage + self-damage, high risk/reward)

**Rationale:** Only card where the player takes damage. Rewards aggressive plays; adds a real decision point ("do I have hull slack to afford this?"). Mirrors the old hearthstone "reno, deal 15 to both players" style.

**Card: Gambit Tier 2 (id 13)**
- Cost: 3, Power: 12, Backlash: 4
- Damage to opponent: 12 (beats DEFENSE, ties OPERATIONS/OFFENSE)
- **Backlash:** You take 4 damage (regardless of outcome)
- Strategic: Win-condition finisher if opponent is at 5-12 hull; suicide play if you're low

### 4. Overcharge (new mechanic: exceed cap for extreme payoff)

**Rationale:** Allows a single bold play that breaks the energy-cap rule. Creates memory/game-state reading: "I'm spending to 7 energy, telegraphing a big play on round 8." Opponent can counter-predict.

**Card: Overcharge Tier 2 (id 14)**
- Cost: 5, Power: 14
- Requires energy ≥ 5; can exceed cap (lock at 7 if you have 6+)
- Damage: 14 (crushes any single card)
- Strategic: Round 8, if you're still standing, this ends the game
- Scalar: `is-legal-play(14, energy)` checks `energy ≥ 5` explicitly

### 5. Spark (new mechanic: scaling by energy bank)

**Rationale:** Rewards patient play and energy banking strategy. Inverse of Overcharge: spend low, deal medium, scale with restraint.

**Card: Spark Tier 1 (id 15)**
- Cost: 1, Power scales: 3 + (energy before play / 2)
- If you have 6 energy, Spark deals 6 (3 + 3)
- If you have 2 energy, Spark deals 4 (3 + 1)
- Strategic: Soft counter to Offense plays; rewards defensive banking

### Hand Size and Distribution

**Option A (conservative):** Keep hand of 4. Reduce Siphon/Leech to one tier each, keep Gambit and Overcharge. Total 12 cards. Variance increases; new cards are rarer.

**Option B (recommended):** Expand hand to **5** (cards 0-14, 15 ids). All 6 new cards fit. More frequent special-card encounters. Tuning: adjust starting energy to 2.5 → rounds start at +2.5 (cap 6) to keep tight economy.

**Option C (aggressive):** Expand to tier 3 for Siphon/Leech (ids 16-17), add one more, keep hand at 4. Total 17 cards. Revisit if needed.

**Recommended: Option B** — hand of 5, 15 total cards (0-14), tighter balance on starting/round energy.

## Balance Rationale

### RPS Triangle Integrity

- Core 9 cards (0-8) unchanged; triangle holds
- New cards are **variants on the triangle**, not orthogonal to it
  - Siphon Offense: Still OFFENSE (beats OPERATIONS), but with energy-denial upside
  - Leech OPERATIONS: Still OPERATIONS (halves damage vs OPERATIONS), heals on play
  - Gambit is a OFFENSE variant (high power, high risk)
  - Overcharge is a OFFENSE variant (highest power, highest cost, breaks cap)
  - Spark is a OFFENSE variant (scales with restraint, beats passivity)
- Triangle still holds: OFFENSE > OPERATIONS > DEFENSE in damage, just with side effects

### Energy Economy

**Current:** +2/round, cap 6, tier-2 costs 4 (66% of cap)
- Pass banks +1 (incentive to save)
- Siphon cards cost 2-3 and drain opponent's energy (tight on aggressor, good for saver)
- Leech cards cost 2-3 but cost hull (self-limiting)
- Overcharge costs 5 (83% of cap), only viable round 8 or with perfect setup

**Tuning knobs if needed:**
- Increase cap to 8 (reduces Overcharge cost % but makes Siphon less valuable)
- Reduce new card costs by 1 (higher frequency of special effects)
- Adjust siphon amounts (2-point drain vs. 1-point)

### Hull Economy

**Current:** 20 hp, tier-2 Offense = 10 damage, two tier-2s = one player death
- New Leech cards delay death (3 hull swing per play)
- Gambit forces a 4-hp tax upfront (real cost)
- Siphon doesn't change damage, just board state

**Win condition**: Still hull or round 8. No infinite loops.

## Implementation Changes

### PARENA Rules Module (`stdlib/deadweight/card_rules.prn`)

Expand:
1. `card-kind`, `card-tier`, `card-cost`, `card-power` to handle ids 0-14 (new cards return new values)
2. `damage-dealt` signature: add `(a-energy a-hull b-hull prior-dmg)` parameters
3. New helpers: `card-siphon`, `card-leech`, `card-backlash`, `card-is-scaling`
4. Unit tests: verify all 15 × 15 combinations (225 tests, mostly symmetric)

**Emission target:** C and Java. Java emitter bottleneck: scalar defns only, no loops. New functions are all scalar (if-then-else), so both targets remain compatible.

### Wire Protocol

**No changes.** Card ids are u8; we stay well under 256.
Hand size: expand from u8 array[4] to u8 array[5]. Backward compat: clients ignore extra slot if server sends 5; server accepts 4 or 5 from legacy clients.

### Server and Clients

- **Regenerate rules:** `scripts/gen_rules.sh` produces `core/card_rules.c` and Android `CardRules.java`
- **Hand distribution:** Reshuffle logic unchanged; just draw 5 instead of 4
- **UI:** Show 5 cards instead of 4 (layout adjustment, low-risk change)

## Testing and Validation

**Unit tests (scalar rules):**
```
All (a, b) ∈ (0..14)² combinations:
  - kind-beats preserves RPS for 0-8, new cards match expected kind
  - damage-dealt returns non-negative scalar
  - Siphon cards actually siphon only on a-beat-b
  - Leech cards heal on any play (no RPS check)
  - Gambit always backlashes (damage(self) = 4 always)
  - Overcharge requires energy ≥ 5
  - Spark scales correctly with energy input
```

**Play balance testing (bot vs. bot, statistical):**
```
100 bot-vs-bot matches, random seeding:
  - No card combo appears >40% of optimal (no runaway best card)
  - Siphon win rate vs. equal-tier OFFENSE: 45-55% (slightly worse, energy cost)
  - Leech win rate: balanced (trades damage for hull, wash in aggregate)
  - Gambit pick rate: ~10-15% (high-risk, situational)
  - Spark pick rate: ~5% (niche energy-bank play)
  - Overcharge pick rate: ~3-5% (round-8 finisher, rarest)
  - Average match duration: 5-7 rounds (same as baseline)
  - Draw rate: 15-20% (same as baseline)
```

**Integration test:**
```
Boot card_mode server with 15 cards, 5-card hand
Queue human vs. bot, play 10 matches
Verify:
  - All card ids are drawn (no missing card bug)
  - Hand refill logic handles 5 cards
  - Damage is calculated correctly (spot-check a few plays)
  - Server doesn't crash on new card combos
```

## Honest Gaps and Deferred Questions

1. **Spark scaling math:** Is `3 + energy/2` the right formula, or `3 + floor(energy/3)` or `power * (energy/6)` (proportional)?
   - Defer: balance-test bot matches; adjust from data

2. **Siphon multi-target:** Could Siphon hit both players, or just the one you beat? Current proposal: single-target (simpler).
   - Defer: if energy-denial is too weak, revisit as an alternate version

3. **Leech overkill:** Can you overheal to >20 hp, or cap at start-hull?
   - Decision: cap at 20 (health items always do; matches `NORTHSTAR.md`'s cargo-item design pattern)

4. **Gambit vs. Leech:** Do they ever appear in same hand of 5? Thematic tension (risk vs. recover)?
   - Data: yes, hand is random. Might want to adjust draw probabilities later to avoid "Gambit + Leech = guaranteed win" pattern, but leave for post-V0 tuning.

5. **Overcharge as finisher:** Is breaking the energy cap a clean design, or too punishing for early-game luck?
   - Data: Overcharge costs 5, only viable if you saved. If someone plays aggressively rounds 1-7 and gets unlucky, they're out. This is fine (tight economy is a feature).

6. **Rebalancing the core 9:** Do we need to weaken existing cards now that new ones exist?
   - Initial data: probably not (new cards are mostly side-effect upgrades, not pure damage upgrades). Monitor and decide post-launch.

## Rollout Plan

**Phase 1 (code):**
1. Update `card_rules.prn` with new card definitions and helpers
2. Regenerate C and Java via `scripts/gen_rules.sh`
3. Update hand-size constant to 5
4. Update protocol handling (minor)

**Phase 2 (test):**
1. Unit tests for all new card combinations
2. Bot league (100 matches per combo, check win rates)
3. Integration test (human vs. bot, spot-check plays)

**Phase 3 (launch):**
1. Deploy server + clients (Android, Windows)
2. Monitor pick rates and win rates for 1-2 weeks
3. Log data to Apples for decision-making
4. Adjust costs/effects if any card is >55% win rate or <30% pick rate

## Summary Table

| Card | ID | Type | Cost | Power | Effect | Rationale |
|------|----|----|------|-------|--------|-----------|
| Siphon T1 | 9 | OFFENSE | 2 | 5 | On-beat steal 1 energy | Board control, tight economy |
| Siphon T2 | 10 | OFFENSE | 3 | 8 | On-beat steal 2 energy | Scaling version |
| Leech T1 | 11 | OPERATIONS | 2 | 4 | Heal 2 | Comeback window |
| Leech T2 | 12 | OPERATIONS | 3 | 7 | Heal 3 | Stronger recovery |
| Gambit T2 | 13 | OFFENSE | 3 | 12 | Take 4 damage | Risk/reward finisher |
| Overcharge T2 | 14 | OFFENSE | 5 | 14 | Exceed cap | Round-8 hail-mary |
| Spark T1 | 15 | OFFENSE | 1 | 3 + energy/2 | Scaling | Reward restraint |

**Total cards:** 15 (ids 0-14)
**Hand size:** 5
**New mechanics:** Siphon, Leech, Backlash, Overcharge, Scaling
**RPS impact:** None (triangle preserved)
**Energy cap:** Unchanged at 6 (Overcharge is the only exception)
