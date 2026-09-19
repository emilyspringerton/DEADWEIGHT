# Card mode rules (v1 — the Underwriters' Guild expansion, 73 cards)

Every number and condition lives in `PARENA/stdlib/deadweight/card_rules.prn` (scalar, dual-emitted to C and Java);
`core/match.c` only owns state and the fixed resolution order. This document is the human-readable spec; the rules
source is the truth. Founder direction (2026-09-19): "ship 64 cards ... ensure some cards are statistically bad ...
also add powerful mechanics that cost more energy than they really should just to kind of make a meta for control
decks ... there should be some OP cards." The set is **deliberately not balanced by a program**: bad cards, trap cards
and generally-excellent "staples" are all intended (learning which cards only *look* good is part of the game).

## Match

- 2 players (seats 0/1). Each starts **hull 20**, **energy 2**, **credits 3**, **armor 0**.
- Catalog of **73 cards** (ids 0–72): the 9 original triangle cards + 64 Guild cards. **Interim deck rule:** each player
  shuffles the whole 73-card catalog (per-player shuffle from the match seed). Deckbuilding is next (see "Draft" below).
- Each player holds a **hand of 4**. Hands are hidden: a client sees its own hand and only the opponent's hand *size*.
- **Rounds 1..100.** Round start: +2 energy (cap 6), **+1 credit** (the Guild stipend), pending next-round energy deltas,
  a per-seat 0–99 **roll** (drives coin flips and Dark Pool), and any hand-slot **locks** placed by the opponent last round.
- Both players secretly **lock one action**: play a card in hand slot 0–3, or **pass** (slot −1, banks +1 energy). A card
  is legal iff `cost ≤ energy`, `credit-cost ≤ credits` and its slot is not locked. Playing spends its cost even if the
  card is later cancelled. The played card is discarded and the slot refills. Timer (live play): 40 s, timeout = pass. Matches run to 100 rounds.

## Resolution order (one round)

1. **Substitution & costs.** Dark Pool becomes Naked Short / Flash Crash / Pump & Dump / Hostile Takeover by its roll.
   Energy and credit costs are paid for the card *declared*.
2. **Cancel, then copy.** A `CANCEL` rider turns the opponent's card into a pass (mutual cancels cancel both); a `COPY`
   rider makes you resolve as the opponent's declared card (even one you cancelled).
3. **Triangle damage** — unchanged: BURST > TANK > SHIELD > BURST. The winner's kind deals its power; BURST vs BURST both
   deal full, TANK vs TANK both deal power/2, SHIELD vs SHIELD nothing; a passed opponent takes full power from BURST/TANK.
4. **Phase-1 riders** (they change the exchange): `BONUS` (extra damage, lost if this card is countered), `PIERCE`
   (ignore armor), `BYPASS` (at least N lands and the counter-reflect is nullified), `IMMUNE`, `LIFELINE`, `REFLECT`,
   `HALVE` (all damage this round, both ways).
5. **Attack damage** hits armor first (unless pierced), then hull.
6. **Phase-2 riders** are evaluated from the post-attack state, then applied in a fixed order: self damage, true
   (unblockable) damage, healing (capped at 20), armor (cap 12), energy/credit changes, cleanse, debt-free, hull
   equalize, Chronobreak. **Lifeline** then rescues anyone at ≤ 0 hull (once per playing card).
7. **Burn/regen tick** (statuses applied *this* round start ticking next round), then new statuses apply.
8. **Hand effects:** played cards discard and refill; Fire Sale redraws; Realm Warp swaps hands; discards and slot locks.
9. **End.** Hull ≤ 0 (or **bankruptcy**: credits below −6) loses immediately (both = draw). After round 100, higher hull
   wins; equal hull → **higher credits win** (cash is a scoring tiebreak, never a way to win outright); else draw.

## Effect engine (how a card's rider is stored)

Each card has up to two packed **effect words** `CCNNPPMAA` (channel, condition, condition parameter, amount mode,
amount). `fx-amount(word, context…)` in the `.prn` returns the amount to apply (0 if the condition fails); the host only
routes it to a channel. Channels, conditions and modes are listed in the header comment of `card_rules.prn`. Adding or
retuning a card is a table edit plus `scripts/gen_rules.sh`.

## Meters and statuses

| meter | start | notes |
|---|---|---|
| hull | 20 | 0 loses; heals cap at 20 |
| energy | 2 | +2 each round (cap 6); pass +1 |
| credits | 3 | +1 each round; some cards *cost* credits; below −6 = bankrupt (lose) |
| armor | 0 | soaks attack damage first; cap 12; persists |
| burn / regen | – | damage / healing per round for N rounds |
| hidden | – | Merkle Blindness hides your energy, armor and credits from the opponent |
| locks | – | Corrosion / Glacial Prison lock opponent hand slots for their next round |

## The catalog

Ids 9–30 are Burst, 31–51 Tank, 52–72 Shield. Traps, staples and control bombs are all on purpose: 5- and 6-energy
cards (Final Spark, Hostile Takeover, Bailout Package, Total Blackout) are priced above their weight to anchor a
control meta; Iron Dwarf, Quartermaster, Field Cleric and Aegis Defender are the generally-useful staples; Capacitor,
Underwrite, Glass Cannon, Pump & Dump and Yolo Roll only *look* good.

| id | Card | Kind | Energy | Credits | Power | What it does |
|---|---|---|---|---|---|---|
| 0 | Spark | Burst | 1 | - | 3 | (base card: triangle only) |
| 1 | Bolt | Burst | 2 | - | 6 | (base card: triangle only) |
| 2 | Railgun | Burst | 4 | - | 10 | (base card: triangle only) |
| 3 | Plating | Tank | 1 | - | 3 | (base card: triangle only) |
| 4 | Bulkhead | Tank | 2 | - | 6 | (base card: triangle only) |
| 5 | Fortress | Tank | 4 | - | 10 | (base card: triangle only) |
| 6 | Buckler | Shield | 1 | - | 3 | (base card: triangle only) |
| 7 | Barrier | Shield | 2 | - | 6 | (base card: triangle only) |
| 8 | Bastion | Shield | 4 | - | 10 | (base card: triangle only) |
| 9 | Call Option | Burst | 2 | - | 4 | If it lands: +1 energy. |
| 10 | Overclock | Burst | 1 | - | 8 | You take 4. |
| 11 | Infinity Edge | Burst | 4 | - | 8 | Shields can't reflect it; 4 always lands. |
| 12 | Rageblade | Burst | 3 | - | 3 | +1 damage per 2 rounds played. |
| 13 | Kraken Slayer | Burst | 2 | 2 | 5 | Costs 2 credits. Every 3rd round: +6 unblockable. |
| 14 | Statikk Shiv | Burst | 2 | - | 3 | +3 against Tank. |
| 15 | Liandry's Torment | Burst | 3 | - | 2 | If it lands: opp burns 2 for 3 rounds. |
| 16 | Death Mark | Burst | 2 | - | 2 | +8 if opp hull is 10 or less. |
| 17 | Final Spark | Burst | 5 | - | 14 | You take 4. |
| 18 | Short Squeeze | Burst | 2 | - | 1 | +1 per energy opp holds. |
| 19 | Margin Call | Burst | 3 | - | 3 | +8 if opp has no credits. |
| 20 | Pump & Dump | Burst | 2 | - | 12 | You take 6. |
| 21 | Flash Crash | Burst | 3 | - | 3 | All damage halved. Opp loses 2 credits. |
| 22 | Yolo Roll | Burst | 1 | - | 0 | 50%: +9 damage. Else you take 3. |
| 23 | Quick Draw | Burst | 1 | - | 2 | +4 if opp passed. |
| 24 | Glass Cannon | Burst | 1 | - | 9 | If you're hit, take 5 more. |
| 25 | Piercing Round | Burst | 3 | - | 5 | Ignores armor. |
| 26 | Executioner | Burst | 4 | - | 6 | +8 if opp hull is 8 or less. |
| 27 | Hail Mary | Burst | 3 | - | 3 | +11 if your hull is 6 or less. |
| 28 | Iron Dwarf | Burst | 2 | - | 6 | +2 damage. |
| 29 | Pressure | Burst | 2 | - | 3 | If it lands: opp loses 1 energy. |
| 30 | Bribe | Burst | 1 | 2 | 6 | Costs 2 credits. |
| 31 | Yield Contract | Tank | 1 | - | 2 | 3+ energy left: +1 energy. |
| 32 | Thornmail | Tank | 2 | - | 4 | Reflects half the damage you take. |
| 33 | Heartsteel | Tank | 4 | - | 3 | Gain armor equal to half damage taken. |
| 34 | Dark Pool | Tank | 2 | - | 0 | Becomes a mystery exotic. |
| 35 | Naked Short | Tank | 1 | - | 0 | +3 energy. If hit, take 5 more. |
| 36 | Hostile Takeover | Tank | 5 | - | 0 | Cancel opp's card and play it yourself. |
| 37 | Corporate Espionage | Tank | 3 | - | 0 | Opp takes 2x their card's cost, unblockable. |
| 38 | Synthetic Short | Tank | 1 | - | 0 | +3 energy if nobody is hurt. |
| 39 | Xeno-Biomass | Tank | 2 | - | 0 | Heal 2 per round for 4 rounds. |
| 40 | Sunfire Aegis | Tank | 3 | - | 2 | Opp burns 1 per round for 5 rounds. |
| 41 | Quartermaster | Tank | 1 | - | 3 | +1 energy. |
| 42 | Field Cleric | Tank | 2 | - | 4 | Heal 2. |
| 43 | Capacitor | Tank | 2 | - | 0 | Gain 2 energy. |
| 44 | Dividend Stock | Tank | 2 | - | 2 | +2 credits. |
| 45 | Bailout Loan | Tank | 1 | - | 0 | +4 energy, -4 credits. |
| 46 | Volatility | Tank | 2 | - | 1 | Opp loses 3 credits. |
| 47 | Merkle Blindness | Tank | 2 | - | 2 | Hide your meters for 3 rounds. |
| 48 | Corrosion | Tank | 2 | - | 0 | Lock 1 of opp's cards next round. |
| 49 | Fire Sale | Tank | 3 | - | 0 | Redraw your hand. +2 credits. |
| 50 | Total Blackout | Tank | 6 | - | 0 | Cancel opp's card. Drain all their energy. |
| 51 | Realm Warp | Tank | 4 | 3 | 0 | Costs 3 credits. Swap hands. |
| 52 | Put Option | Shield | 2 | - | 4 | Heal up to 3 of the damage you take. |
| 53 | Stasis Frame | Shield | 3 | - | 3 | Immune if your hull is 6 or less. |
| 54 | Underwrite | Shield | 1 | - | 0 | +1 energy. If hit for 6+, take 4 more. |
| 55 | Parametric Policy | Shield | 2 | - | 2 | Heal 4 if your hull is 8 or less. |
| 56 | Replacement Cost | Shield | 2 | - | 2 | If hit: +3 armor. |
| 57 | Credit Default Swap | Shield | 2 | - | 3 | Stops a Burst: +2 energy, +1 credit. |
| 58 | Reinsurance Bundle | Shield | 3 | - | 0 | Immune this round. Next round -2 energy. |
| 59 | Immortal Shieldbow | Shield | 3 | 2 | 4 | Costs 2 credits. Survive lethal at 1. |
| 60 | Zhonya's Hourglass | Shield | 3 | - | 0 | Immune this round. Next round -1 energy. |
| 61 | Seraph's Embrace | Shield | 3 | - | 3 | +4 armor. |
| 62 | Dead Squares | Shield | 1 | - | 0 | +2 armor. |
| 63 | Aegis Defender | Shield | 2 | - | 5 | +2 armor. |
| 64 | Monsoon | Shield | 4 | - | 4 | Reflect all damage taken. Clear burn. |
| 65 | Wish | Shield | 3 | - | 0 | Heal 6. |
| 66 | Bailout Package | Shield | 5 | - | 0 | Heal 8. Erase your debt. |
| 67 | Cold Steel | Shield | 2 | - | 3 | Opp played cost 3+: they lose 2 energy. |
| 68 | Mana Shield | Shield | 2 | - | 1 | Convert up to 4 energy to armor. |
| 69 | Corporate Merger | Shield | 4 | - | 0 | Both hulls become the average. |
| 70 | Glacial Prison | Shield | 4 | - | 2 | Lock 2 of opp's cards next round. |
| 71 | Chronobreak | Shield | 4 | - | 0 | Restore hull to last round's start. |
| 72 | Hush Money | Shield | 2 | - | 1 | +2 credits. Opp loses 2. |

## Draft (next feature — founder design, 2026-09-19, NOT in this ship)

At matchmaking, each player first drafts a deck. Each pick presents **2 cards from the catalog**; the player picks a card
*and* a multiplicity 1, 2 or 3, from three state-machine buckets: **ten 1-ofs, five 2-ofs, one 3-of** (16 picks → a
**23-card deck**). A bucket greys out once full, so only legal decks can be built. After a game a human may re-queue
with the same deck or redraft; **bots always redraft** (for PFSP training each generation drafts its own deck and plays
it). Until this lands, decks are the whole shuffled catalog.

## PARENA rules module API (all `I32`/`Bool`, single-expression, dual-emitted C+Java)

`card-kind` · `card-tier` · `card-cost` · `card-power` · `card-credit` · `kind-beats` · `is-legal-play(id, energy, credits)` ·
`damage-dealt(a, b)` (−1 = pass) · `round-start-energy` · `round-start-vault` · `energy-after-play` · `round-winner-by-hull` ·
`round-winner(h0, h1, v0, v1)` · `match-decided` · `bankrupt` · `card-substitute(id, roll)` · `card-fx-a/b(id)` ·
`fx-ch` / `fx-phase` / `fx-amount(word, dealt, taken, my-kind, opp-kind, opp-cost, my-hull, opp-hull, energy-left,
opp-energy, my-credits, opp-credits, round, roll)`. Constants are zero-parameter defns (`start-hull`, `start-energy`,
`start-vault`, `max-rounds`, `hand-size`, `num-cards`).
