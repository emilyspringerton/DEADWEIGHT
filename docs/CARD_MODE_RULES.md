# Card mode rules (v2 — the Offense / Operations / Defense retheme, 105 cards)

Every number and condition lives in `PARENA/stdlib/deadweight/card_rules.prn` (scalar, dual-emitted to C and Java);
`core/match.c` only owns state and the fixed resolution order. This document is the human-readable spec; the rules
source is the truth. Founder direction (2026-09-19): "ship 64 cards ... ensure some cards are statistically bad ...
also add powerful mechanics that cost more energy than they really should just to kind of make a meta for control
decks ... there should be some OP cards." The set is **deliberately not balanced by a program**: bad cards, trap cards
and generally-excellent "staples" are all intended (learning which cards only *look* good is part of the game).

## The tactical triangle (v2 terminology)

Every card is one of three kinds (a colour):

| Kind | Colour | Identity | Beats |
|---|---|---|---|
| **Offense** | Red | Direct attacks, missiles, kinetic force. | Operations |
| **Defense** | Blue | Energy barriers, structural integrity, passive protection (repair, armor, reserves, immunity). | Offense |
| **Operations** | Yellow / amber | An active tactical suite that exploits stationary, defending ships (E-War, maneuvering, system hacking). | Defense |

**Offense > Operations > Defense > Offense.** (Kind ids in the rules: 0 Offense, 1 Operations, 2 Defense.)
Every **Operations** card carries exactly one of five keywords, and its rule text starts with it:

| Keyword | Meaning | What it does in the rules |
|---|---|---|
| **Lock** | Precision targeting that shatters defenses. | Ignores armor (pierce), guaranteed damage that can't be reflected (bypass), bonus damage against Defense or against big signatures. |
| **Sabotage** | Internal system hacks and viruses. | Burns (viruses), cancelling or hijacking the opponent's card, disabling cards in their hand, swapping/scrambling. |
| **Flank** | Positional movement to strike undefended areas. | Bonus damage against a passed opponent or a Defense card; guaranteed damage. The base Operations line (Sidewinder / Interceptor / Stormwing) is Flank: +1 / +2 / +3 against Defense. |
| **Scan** | Intel gathering, exposing the enemy. | Bonuses that read the opponent's hull, energy, credits or played card; taking their hand. |
| **Siphon** | Resource and energy theft. | Drains or steals the opponent's energy and credits. |

Offense and Defense cards do not use keywords. The catalog has **35 cards of each kind** and **7 cards per keyword**.
(The old wording for the hand-slot effect "lock a card" is now **disable**, so the word Lock only ever means the keyword.)

## Match

- 2 players (seats 0/1). Each starts **hull 20**, **energy 2**, **credits 3**, **armor 0**.
- Catalog of **105 cards** (ids 0–104): the 9 base triangle cards, the 64 Underwriters' Guild cards, and 32 cards added by the v2 retheme. **Random queue deck rule:** each player
  shuffles the whole 105-card catalog (per-player shuffle from the match seed). Draft mode (its own queue) builds a 23-card deck first (see "Queues and Draft mode" below).
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
3. **Triangle damage**: OFFENSE > OPERATIONS > DEFENSE > OFFENSE. The winner's kind deals its power; OFFENSE vs OFFENSE both
   deal full, OPERATIONS vs OPERATIONS both deal power/2, DEFENSE vs DEFENSE nothing; a passed opponent takes full power from OFFENSE/OPERATIONS (Defense deals nothing to a passed opponent).
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

Traps, staples and control bombs are all on purpose: 5- and 6-energy cards (Final Spark, Hostile Takeover, Bailout Package,
Total Blackout, Extinction Salvo, Sniper Uplink, Doomsday Warhead) are priced above their weight to anchor a control meta;
Iron Dwarf, Quartermaster, Field Cleric, Aegis Defender and Ion Cannon are the generally-useful staples; Capacitor, Underwrite,
Glass Cannon, Pump & Dump, Yolo Roll, Broadside and Dud Round only *look* good (or don't even).

**How the v2 retheme sorted the existing cards** (no card was removed; every id, cost, power and effect is unchanged unless a
row below says otherwise). The old three-way split is now the Offense / Operations / Defense triangle with the old
attack-triangle relationships preserved exactly (the old middle kind is Operations), and each card sits in the colour whose
identity it fits:

- **Offense (Red)** keeps the pure damage cards (Spark line, Overclock, Rageblade, Kraken Slayer, Statikk Shiv, Final Spark, Pump & Dump, Yolo Roll, Glass Cannon, Hail Mary, Iron Dwarf, Bribe, Call Option).
- **Operations (Yellow)** took the cards whose whole point is hacking, exposing or draining the enemy: the Lock cards (Infinity Edge, Piercing Round), Sabotage (Liandry's Torment, Dark Pool, Hostile Takeover, Sunfire Aegis, Corrosion, Glacial Prison), Flank (Quick Draw), Scan (Death Mark, Executioner, Short Squeeze, Margin Call, Corporate Espionage, Realm Warp) and Siphon (Pressure, Flash Crash, Volatility, Total Blackout, Cold Steel, Hush Money). The vanilla middle base line (Plating / Bulkhead / Fortress) became the **Flank** line Sidewinder / Interceptor / Stormwing, with a small +1 / +2 / +3 rider against Defense (the one change to an existing card's numbers).
- **Defense (Blue)** took the self-repair, armor, reserve and economy cards (Field Cleric, Heartsteel, Xeno-Biomass, Thornmail, Quartermaster, Capacitor, ...).
- Renames: the three base Operations cards became Sidewinder / Interceptor / Stormwing, and two cards whose old names used the retired archetype word are now Immortal Aegisbow and Mana Ward. Rule text was reworded to the new terms (Lock 1 of opp's cards → Sabotage: disable 1 of opp's cards).
- **32 cards were added** so each colour stays at 35: 11 Operations (5 Lock, 3 Flank, and one each of Scan, Sabotage, Siphon), 20 Offense (missiles, kinetic and unblockable damage, resource-fuelled and gamble cards) and 1 Defense.

| 0 | Spark | Offense | - | 1 | - | 3 | (vanilla: triangle only) |
| 1 | Bolt | Offense | - | 2 | - | 6 | (vanilla: triangle only) |
| 2 | Railgun | Offense | - | 4 | - | 10 | (vanilla: triangle only) |
| 3 | Sidewinder | Operations | Flank | 1 | - | 3 | Flank: +1 against Defense. |
| 4 | Interceptor | Operations | Flank | 2 | - | 6 | Flank: +2 against Defense. |
| 5 | Stormwing | Operations | Flank | 4 | - | 10 | Flank: +3 against Defense. |
| 6 | Buckler | Defense | - | 1 | - | 3 | (vanilla: triangle only) |
| 7 | Barrier | Defense | - | 2 | - | 6 | (vanilla: triangle only) |
| 8 | Bastion | Defense | - | 4 | - | 10 | (vanilla: triangle only) |
| 9 | Call Option | Offense | - | 2 | - | 4 | If it lands: +1 energy. |
| 10 | Overclock | Offense | - | 1 | - | 8 | You take 4. |
| 11 | Infinity Edge | Operations | Lock | 4 | - | 8 | Lock: 4 always lands and it can't be reflected. |
| 12 | Rageblade | Offense | - | 3 | - | 3 | +1 damage per 2 rounds played. |
| 13 | Kraken Slayer | Offense | - | 2 | 2 | 5 | Costs 2 credits. Every 3rd round: +6 unblockable. |
| 14 | Statikk Shiv | Offense | - | 2 | - | 3 | +3 against Operations. |
| 15 | Liandry's Torment | Operations | Sabotage | 3 | - | 2 | Sabotage: if it lands, opp burns 2 for 3 rounds. |
| 16 | Death Mark | Operations | Scan | 2 | - | 2 | Scan: +8 if opp hull is 10 or less. |
| 17 | Final Spark | Offense | - | 5 | - | 14 | You take 4. |
| 18 | Short Squeeze | Operations | Scan | 2 | - | 1 | Scan: +1 per energy opp holds. |
| 19 | Margin Call | Operations | Scan | 3 | - | 3 | Scan: +8 if opp has no credits. |
| 20 | Pump & Dump | Offense | - | 2 | - | 12 | You take 6. |
| 21 | Flash Crash | Operations | Siphon | 3 | - | 3 | Siphon: opp loses 2 credits. All damage halved. |
| 22 | Yolo Roll | Offense | - | 1 | - | 0 | 50%: +9 damage. Else you take 3. |
| 23 | Quick Draw | Operations | Flank | 1 | - | 2 | Flank: +4 if opp passed. |
| 24 | Glass Cannon | Offense | - | 1 | - | 9 | If you're hit, take 5 more. |
| 25 | Piercing Round | Operations | Lock | 3 | - | 5 | Lock: ignores armor. |
| 26 | Executioner | Operations | Scan | 4 | - | 6 | Scan: +8 if opp hull is 8 or less. |
| 27 | Hail Mary | Offense | - | 3 | - | 3 | +11 if your hull is 6 or less. |
| 28 | Iron Dwarf | Offense | - | 2 | - | 6 | +2 damage. |
| 29 | Pressure | Operations | Siphon | 2 | - | 3 | Siphon: if it lands, opp loses 1 energy. |
| 30 | Bribe | Offense | - | 1 | 2 | 6 | Costs 2 credits. |
| 31 | Yield Contract | Defense | - | 1 | - | 2 | 3+ energy left: +1 energy. |
| 32 | Thornmail | Defense | - | 2 | - | 4 | Reflects half the damage you take. |
| 33 | Heartsteel | Defense | - | 4 | - | 3 | Gain armor equal to half damage taken. |
| 34 | Dark Pool | Operations | Sabotage | 2 | - | 0 | Sabotage: becomes a mystery exotic. |
| 35 | Naked Short | Defense | - | 1 | - | 0 | +3 energy. If hit, take 5 more. |
| 36 | Hostile Takeover | Operations | Sabotage | 5 | - | 0 | Sabotage: cancel opp's card and play it yourself. |
| 37 | Corporate Espionage | Operations | Scan | 3 | - | 0 | Scan: opp takes 2x their card's cost, unblockable. |
| 38 | Synthetic Short | Defense | - | 1 | - | 0 | +3 energy if nobody is hurt. |
| 39 | Xeno-Biomass | Defense | - | 2 | - | 0 | Heal 2 per round for 4 rounds. |
| 40 | Sunfire Aegis | Operations | Sabotage | 3 | - | 2 | Sabotage: opp burns 1 per round for 5 rounds. |
| 41 | Quartermaster | Defense | - | 1 | - | 3 | +1 energy. |
| 42 | Field Cleric | Defense | - | 2 | - | 4 | Heal 2. |
| 43 | Capacitor | Defense | - | 2 | - | 0 | Gain 2 energy. |
| 44 | Dividend Stock | Defense | - | 2 | - | 2 | +2 credits. |
| 45 | Bailout Loan | Defense | - | 1 | - | 0 | +4 energy, -4 credits. |
| 46 | Volatility | Operations | Siphon | 2 | - | 1 | Siphon: opp loses 3 credits. |
| 47 | Merkle Blindness | Defense | - | 2 | - | 2 | Hide your meters for 3 rounds. |
| 48 | Corrosion | Operations | Sabotage | 2 | - | 0 | Sabotage: disable 1 of opp's cards next round. |
| 49 | Fire Sale | Defense | - | 3 | - | 0 | Redraw your hand. +2 credits. |
| 50 | Total Blackout | Operations | Siphon | 6 | - | 0 | Siphon: cancel opp's card and drain all their energy. |
| 51 | Realm Warp | Operations | Scan | 4 | 3 | 0 | Scan: costs 3 credits. Swap hands. |
| 52 | Put Option | Defense | - | 2 | - | 4 | Heal up to 3 of the damage you take. |
| 53 | Stasis Frame | Defense | - | 3 | - | 3 | Immune if your hull is 6 or less. |
| 54 | Underwrite | Defense | - | 1 | - | 0 | +1 energy. If hit for 6+, take 4 more. |
| 55 | Parametric Policy | Defense | - | 2 | - | 2 | Heal 4 if your hull is 8 or less. |
| 56 | Replacement Cost | Defense | - | 2 | - | 2 | If hit: +3 armor. |
| 57 | Credit Default Swap | Defense | - | 2 | - | 3 | Stops an Offense card: +2 energy, +1 credit. |
| 58 | Reinsurance Bundle | Defense | - | 3 | - | 0 | Immune this round. Next round -2 energy. |
| 59 | Immortal Aegisbow | Defense | - | 3 | 2 | 4 | Costs 2 credits. Survive lethal at 1. |
| 60 | Zhonya's Hourglass | Defense | - | 3 | - | 0 | Immune this round. Next round -1 energy. |
| 61 | Seraph's Embrace | Defense | - | 3 | - | 3 | +4 armor. |
| 62 | Dead Squares | Defense | - | 1 | - | 0 | +2 armor. |
| 63 | Aegis Defender | Defense | - | 2 | - | 5 | +2 armor. |
| 64 | Monsoon | Defense | - | 4 | - | 4 | Reflect all damage taken. Clear burn. |
| 65 | Wish | Defense | - | 3 | - | 0 | Heal 6. |
| 66 | Bailout Package | Defense | - | 5 | - | 0 | Heal 8. Erase your debt. |
| 67 | Cold Steel | Operations | Siphon | 2 | - | 3 | Siphon: opp played cost 3+: they lose 2 energy. |
| 68 | Mana Ward | Defense | - | 2 | - | 1 | Convert up to 4 energy to armor. |
| 69 | Corporate Merger | Defense | - | 4 | - | 0 | Both hulls become the average. |
| 70 | Glacial Prison | Operations | Sabotage | 4 | - | 2 | Sabotage: disable 2 of opp's cards next round. |
| 71 | Chronobreak | Defense | - | 4 | - | 0 | Restore hull to last round's start. |
| 72 | Hush Money | Operations | Siphon | 2 | - | 1 | Siphon: +2 credits, opp loses 2. |
| 73 | Targeting Laser | Operations | Lock | 1 | - | 3 | Lock: ignores armor. |
| 74 | Bunker Buster | Operations | Lock | 3 | - | 6 | Lock: +4 against Defense. |
| 75 | Phase Lance | Operations | Lock | 4 | - | 7 | Lock: ignores armor. 3 always lands. |
| 76 | Heat Seeker | Operations | Lock | 2 | - | 4 | Lock: +3 if opp played cost 3+. |
| 77 | Sniper Uplink | Operations | Lock | 5 | - | 9 | Lock: ignores armor. +5 if opp hull is 10 or less. |
| 78 | Blindside | Operations | Flank | 2 | - | 3 | Flank: +5 if opp passed. +2 against Defense. |
| 79 | Orbital Slingshot | Operations | Flank | 3 | - | 5 | Flank: 4 always lands. |
| 80 | Drift Strike | Operations | Flank | 1 | - | 1 | Flank: +6 against Defense. |
| 81 | Threat Assessment | Operations | Scan | 2 | - | 1 | Scan: +2 per point of opp's card cost. |
| 82 | Logic Bomb | Operations | Sabotage | 3 | - | 2 | Sabotage: if it lands, opp burns 3 for 2 rounds. |
| 83 | Power Tap | Operations | Siphon | 2 | - | 2 | Siphon: if it lands, opp loses 2 energy and you gain 2. |
| 84 | Point Defense Grid | Defense | - | 3 | - | 4 | +3 armor. Reflects 30% of the damage you take. |
| 85 | Missile Salvo | Offense | - | 3 | - | 4 | +1 damage per energy you have left. |
| 86 | Cluster Bomb | Offense | - | 2 | - | 2 | If it lands: +3 unblockable. |
| 87 | Hellfire Rack | Offense | - | 4 | - | 8 | If it lands: +4 unblockable. |
| 88 | Mass Driver | Offense | - | 3 | - | 8 | You take 2. |
| 89 | Recoil Cannon | Offense | - | 1 | - | 6 | You take 3. |
| 90 | Ripple Fire | Offense | - | 2 | - | 3 | +3 if opp played a card. |
| 91 | Salvage Rounds | Offense | - | 2 | - | 4 | If it lands: +1 credit. |
| 92 | Overwatch Strike | Offense | - | 2 | - | 3 | +3 if opp holds 4+ energy. |
| 93 | Extinction Salvo | Offense | - | 6 | - | 14 | If it lands: +6 unblockable. |
| 94 | Ricochet | Offense | - | 2 | - | 3 | 40%: +7 damage. Else +1. |
| 95 | Sponsored Barrage | Offense | - | 3 | - | 5 | +1 per 2 credits you hold. |
| 96 | Capacitor Discharge | Offense | - | 1 | - | 3 | +4 if you have 3+ energy left. |
| 97 | Gatling Rounds | Offense | - | 1 | - | 3 | Every 2nd round: +4. |
| 98 | Ion Cannon | Offense | - | 3 | - | 8 | +1 energy. |
| 99 | Tungsten Rod | Offense | - | 4 | - | 8 | +4 against Operations. |
| 100 | Doomsday Warhead | Offense | - | 5 | 3 | 10 | Costs 3 credits. If it lands: +8. |
| 101 | Broadside | Offense | - | 3 | - | 9 | If you're hit, take 3 more. |
| 102 | Dud Round | Offense | - | 1 | - | 1 | 50%: +5 damage. |
| 103 | Volley Fire | Offense | - | 3 | - | 6 | +3 if opp passed. |
| 104 | Fusion Torch | Offense | - | 2 | - | 5 | You take 1. |
wrote /tmp/cj.json (105 cards)

## Queues and Draft mode (shipped 2026-09-19)

There are separate queues (HELLO `mode`): **random** (mode 0, the original: each player's deck is the whole shuffled 105-card
catalog) and **draft** (mode 2). Constructed (bring your own deck) is a later mode; random may go away when it lands.
A player only ever meets someone in the same queue, so every queue needs its own bot pool of 3.

In draft mode each player first drafts a deck (`core/draft.[ch]`, host-owned, not PARENA). Each of 16 picks offers **2 cards
from the catalog** (a card already taken is never offered again); the drafter takes one card *and* a copy count from three
buckets: **ten 1-ofs, five 2-ofs, one 3-of** = **a 23-card deck**. A bucket greys out once full, so only legal decks can be
built. Offers are a deterministic function of the seed. The match then plays with each seat's own deck (`dw_match_init_decks`;
the draw pile is the deck, the discard is reshuffled into it as before).

After a match a human chooses **same deck** or **redraft** (`QUEUE same_deck`). Bots decide the same way by result: **a bot keeps
its deck after a win or draw and redrafts after a loss** (so a winning bot deck is something to counter with a new deck). The
bot drafting policy is deliberately simple (higher power-for-cost card of the offer, strong cards get the scarcer buckets); the
play brain is unchanged.

**Deck log.** The server appends every drafted deck to `decks.ndjson` (in `--match-log DIR`): a `draft` record (deck_id, time,
player, kind, card ids and names) when the draft completes, and a `match` record (deck_id, match_id, player, win/loss/draw) per
game played with it, so a deck's record can be looked up later (`jq -c 'select(.deck_id==42)' decks.ndjson`). `matches.ndjson`
draft records carry `deck_ids` and both `decks`, so draft matches replay exactly (`build/replay_check`).

## PARENA rules module API (all `I32`/`Bool`, single-expression, dual-emitted C+Java)

`card-kind` · `card-tier` · `card-cost` · `card-power` · `card-credit` · `kind-beats` · `is-legal-play(id, energy, credits)` ·
`damage-dealt(a, b)` (−1 = pass) · `round-start-energy` · `round-start-vault` · `energy-after-play` · `round-winner-by-hull` ·
`round-winner(h0, h1, v0, v1)` · `match-decided` · `bankrupt` · `card-substitute(id, roll)` · `card-fx-a/b(id)` ·
`fx-ch` / `fx-phase` / `fx-amount(word, dealt, taken, my-kind, opp-kind, opp-cost, my-hull, opp-hull, energy-left,
opp-energy, my-credits, opp-credits, round, roll)`. Constants are zero-parameter defns (`start-hull`, `start-energy`,
`start-vault`, `max-rounds`, `hand-size`, `num-cards`).
