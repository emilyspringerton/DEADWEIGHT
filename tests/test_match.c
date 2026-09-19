/* Match state-machine tests: hand-typed scripted games (expected values derived by hand from
 * docs/CARD_MODE_RULES.md, not from the code), replay determinism, hidden-info, and 10k random-legal games
 * with invariant checks. Run under ASan/UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "match.h"
#include "card_rules.h"
#include "protocol.h"

static int fails = 0, checks = 0;
#define EQ(e, want) do { checks++; long g_ = (long)(e); if (g_ != (long)(want)) { fails++; \
    printf("FAIL %s:%d %s = %ld, want %ld\n", __FILE__, __LINE__, #e, g_, (long)(want)); } } while (0)

static uint32_t rng = 987654321;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void set_hand(DwMatch *m, int s, int a, int b, int c, int d) { m->hand[s][0] = (int8_t)a; m->hand[s][1] = (int8_t)b; m->hand[s][2] = (int8_t)c; m->hand[s][3] = (int8_t)d; }

/* Realm Warp swaps hands, so per-seat conservation no longer holds; the invariant is that every catalog card exists
 * exactly twice (once per player's deck) across both seats' hand+draw+discard. Before any swap that is once per seat. */
static int deck_ok_both(const DwMatch *m) {
    int seen[DW_DECK] = {0}, total = 0;
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < DW_HAND; i++) if (m->hand[s][i] >= 0) { if (m->hand[s][i] >= DW_DECK) return 0; seen[(int)m->hand[s][i]]++; total++; }
        for (int i = 0; i < m->draw_n[s]; i++) { seen[(int)m->draw[s][i]]++; total++; }
        for (int i = 0; i < m->discard_n[s]; i++) { seen[(int)m->discard[s][i]]++; total++; }
    }
    for (int id = 0; id < num_cards(); id++) if (seen[id] != 2) return 0;
    return total == 2 * num_cards();
}
static int deck_ok(const DwMatch *m, int s) {
    (void)s; return deck_ok_both(m);
}

static void scripted_a(void) {
    DwMatch m; DwOutcome o;
    dw_match_init(&m, 1);
    set_hand(&m, 0, 2, 5, 8, 0); set_hand(&m, 1, 5, 2, 0, 3);
    m.draw_n[0] = 3; m.draw[0][0] = 1; m.draw[0][1] = 4; m.draw[0][2] = 7;   /* top = 7 */
    m.draw_n[1] = 3; m.draw[1][0] = 8; m.draw[1][1] = 6; m.draw[1][2] = 1;   /* top = 1 */
    m.discard_n[0] = m.discard_n[1] = 0;
    /* round 1: energy 2+2=4 each. burst t2 (cost 4, power 10) vs burst t2: both take 10 */
    dw_match_begin_round(&m);
    EQ(m.round, 1); EQ(m.energy[0], 4); EQ(m.energy[1], 4);
    EQ(dw_match_lock(&m, 0, 0), 0); EQ(dw_match_lock(&m, 0, 1), DW_REJ_ALREADY_LOCKED);
    EQ(dw_match_lock(&m, 1, 4), DW_REJ_BAD_SLOT); EQ(dw_match_lock(&m, 1, -2), DW_REJ_BAD_SLOT);
    EQ(dw_match_lock(&m, 1, 1), 0); EQ(dw_match_both_locked(&m), 1);
    dw_match_resolve(&m, &o);
    EQ(o.card[0], 2); EQ(o.card[1], 2); EQ(o.dmg_to[0], 10); EQ(o.dmg_to[1], 10);
    EQ(m.hull[0], 10); EQ(m.hull[1], 10); EQ(m.energy[0], 0); EQ(m.energy[1], 0); EQ(m.done, 0);
    EQ(m.hand[0][0], 7); EQ(m.hand[1][1], 1);
    /* round 2: energy 2. p1 tries tank t2 (cost 4): illegal; passes. p0 burst t0 (cost 1) unopposed: 3 dmg */
    dw_match_begin_round(&m);
    EQ(m.energy[0], 2); EQ(m.energy[1], 2);
    EQ(dw_match_lock(&m, 1, 0), DW_REJ_ILLEGAL_CARD);
    EQ(dw_match_lock(&m, 1, -1), 0); EQ(dw_match_lock(&m, 0, 3), 0);
    dw_match_resolve(&m, &o);
    EQ(o.card[0], 0); EQ(o.card[1], -1); EQ(o.dmg_to[1], 3); EQ(o.dmg_to[0], 0);
    EQ(m.hull[0], 10); EQ(m.hull[1], 7); EQ(m.energy[0], 1); EQ(m.energy[1], 3); EQ(m.hand[0][3], 4);
    /* round 3: energy 3 / 5. p0 shield t1 (cost 2) vs p1 tank t0 (cost 1): tank beats shield, 3 dmg to p0 */
    dw_match_begin_round(&m);
    EQ(m.energy[0], 3); EQ(m.energy[1], 5);
    EQ(dw_match_lock(&m, 0, 0), 0); EQ(dw_match_lock(&m, 1, 3), 0);
    dw_match_resolve(&m, &o);
    EQ(o.card[0], 7); EQ(o.card[1], 3); EQ(o.dmg_to[0], 3); EQ(o.dmg_to[1], 0);
    EQ(m.hull[0], 7); EQ(m.hull[1], 7); EQ(m.energy[0], 1); EQ(m.energy[1], 4);
    EQ(m.discard_n[0], 3); EQ(m.discard_n[1], 2);
}

static void scripted_end(void) {
    DwMatch m; DwOutcome o;
    /* p0 passes at hull 3 vs unopposed burst t0 (3): p1 wins by hull */
    dw_match_init(&m, 2); set_hand(&m, 1, 0, 1, 2, 3); m.hull[0] = 3;
    dw_match_begin_round(&m); dw_match_lock(&m, 0, -1); dw_match_lock(&m, 1, 0); dw_match_resolve(&m, &o);
    EQ(m.done, 1); EQ(m.reason, DW_END_HULL); EQ(m.result[0], DW_RES_LOSS); EQ(m.result[1], DW_RES_WIN);
    EQ(dw_match_lock(&m, 0, -1), DW_REJ_NO_MATCH);
    /* burst t0 vs burst t0 at hull 3/3: both die = draw */
    dw_match_init(&m, 3); set_hand(&m, 0, 0, 1, 2, 3); set_hand(&m, 1, 0, 1, 2, 3); m.hull[0] = m.hull[1] = 3;
    dw_match_begin_round(&m); dw_match_lock(&m, 0, 0); dw_match_lock(&m, 1, 0); dw_match_resolve(&m, &o);
    EQ(m.done, 1); EQ(m.result[0], DW_RES_DRAW); EQ(m.result[1], DW_RES_DRAW);
    /* all-pass full-length game: energy stays capped at 6; hull 20 vs 17 -> seat 0 wins on rounds */
    dw_match_init(&m, 4); m.hull[1] = 17;
    for (int r = 1; r <= max_rounds(); r++) {
        dw_match_begin_round(&m); EQ(m.done, 0); EQ(m.energy[0] <= 6, 1);
        dw_match_lock(&m, 0, -1); dw_match_lock(&m, 1, -1); dw_match_resolve(&m, &o);
    }
    EQ(m.round, max_rounds()); EQ(m.done, 1); EQ(m.reason, DW_END_ROUNDS); EQ(m.result[0], DW_RES_WIN); EQ(m.result[1], DW_RES_LOSS);
    EQ(m.energy[0], 6);
    /* equal hull at round cap = draw */
    dw_match_init(&m, 5);
    for (int r = 1; r <= max_rounds(); r++) { dw_match_begin_round(&m); dw_match_lock(&m, 0, -1); dw_match_lock(&m, 1, -1); dw_match_resolve(&m, &o); }
    EQ(m.result[0], DW_RES_DRAW);
    /* forfeit */
    dw_match_init(&m, 6); dw_match_begin_round(&m); dw_match_forfeit(&m, 1);
    EQ(m.done, 1); EQ(m.reason, DW_END_FORFEIT); EQ(m.result[0], DW_RES_WIN); EQ(m.result[1], DW_RES_LOSS);
}


/* ---- Guild card mechanics: hand-computed scripted rounds (expected values derived by hand from docs/CARD_MODE_RULES.md) ---- */
static void start(DwMatch *m, uint32_t seed, int a, int b, int c, int d, int e, int f, int g, int h) {
    dw_match_init(m, seed);
    set_hand(m, 0, a, b, c, d); set_hand(m, 1, e, f, g, h);
    dw_match_begin_round(m);
    m->energy[0] = m->energy[1] = 6;
}
static DwOutcome go(DwMatch *m, int s0, int s1) {
    DwOutcome o; dw_match_lock(m, 0, s0); dw_match_lock(m, 1, s1); dw_match_resolve(m, &o); return o;
}

static void guild_cards(void) {
    DwMatch m; DwOutcome o;
    /* Iron Dwarf (28: burst 2 energy, power 6, +2) vs pass: 8 damage; vs Barrier (7, shield): countered, bonus lost, takes 6 */
    start(&m, 11, 28, 0, 0, 0, 7, 0, 0, 0);
    o = go(&m, 0, -1); EQ(m.hull[1], 12); EQ(m.energy[0], 4); EQ(o.dmg_to[1], 8);
    start(&m, 12, 28, 0, 0, 0, 7, 0, 0, 0);
    o = go(&m, 0, 0); EQ(m.hull[0], 14); EQ(m.hull[1], 20); EQ(o.dmg_to[0], 6);
    /* Infinity Edge (11) vs Barrier (7): at least 4 lands and the shield can't reflect */
    start(&m, 13, 11, 0, 0, 0, 7, 0, 0, 0);
    o = go(&m, 0, 0); EQ(m.hull[0], 20); EQ(m.hull[1], 16);
    /* Thornmail (32, tank 4) vs Railgun (2, burst 10): burst beats tank, seat 0 takes 10 and reflects half (5) */
    start(&m, 14, 32, 0, 0, 0, 2, 0, 0, 0);
    o = go(&m, 0, 0); EQ(m.hull[0], 10); EQ(m.hull[1], 15);
    /* Hostile Takeover (36, 5 energy): cancels Railgun, then plays it: seat 1 takes 10, seat 0 nothing; both pay their declared cost */
    start(&m, 15, 36, 0, 0, 0, 2, 0, 0, 0);
    o = go(&m, 0, 0); EQ(m.hull[0], 20); EQ(m.hull[1], 10); EQ(m.energy[0], 1); EQ(m.energy[1], 2);
    EQ(o.eff[1], -1); EQ(o.eff[0], 2); EQ((o.flags[1] & DW_FX_CANCELLED) != 0, 1);
    /* Stasis Frame (53) at hull 5 is immune to Railgun and still reflects its own power (3) */
    start(&m, 16, 53, 0, 0, 0, 2, 0, 0, 0); m.hull[0] = 5;
    o = go(&m, 0, 0); EQ(m.hull[0], 5); EQ(m.hull[1], 17); EQ((o.flags[0] & DW_FX_IMMUNE) != 0, 1);
    /* Immortal Shieldbow (59, costs 2 credits) vs Fortress (5): tank beats shield for 10, lifeline keeps seat 0 at 1 hull */
    start(&m, 17, 59, 0, 0, 0, 5, 0, 0, 0); m.hull[0] = 3;
    EQ(m.vault[0], 4); o = go(&m, 0, 0); EQ(m.hull[0], 1); EQ(m.vault[0], 2); EQ((o.flags[0] & DW_FX_LIFELINE) != 0, 1);
    /* Total Blackout (50, 6 energy) vs Railgun: cancelled, and the opponent's energy is drained to 0 */
    start(&m, 18, 50, 0, 0, 0, 2, 0, 0, 0);
    o = go(&m, 0, 0); EQ(m.hull[0], 20); EQ(m.hull[1], 20); EQ(m.energy[1], 0); EQ(m.energy[0], 0);
    /* Corporate Merger (69): both hulls become the average, rounded up; Chronobreak (71) restores last round's start hull */
    start(&m, 19, 69, 0, 0, 0, 0, 0, 0, 0); m.hull[1] = 10;
    o = go(&m, 0, -1); EQ(m.hull[0], 15); EQ(m.hull[1], 15);
    start(&m, 20, 2, 71, 0, 0, 2, 0, 0, 0);
    o = go(&m, 0, 0); EQ(m.hull[0], 10); EQ(m.hull[1], 10);          /* both Railguns: 10 each */
    dw_match_begin_round(&m); m.energy[0] = 6; m.energy[1] = 6;
    o = go(&m, 1, -1); EQ(m.hull[0], 20);                            /* Chronobreak: hull back to the start of round 1 */
    /* Bankruptcy: Bailout Loan (45) spends 4 credits; from -4 (after the stipend) that is -8 < -6 -> immediate loss */
    dw_match_init(&m, 21); set_hand(&m, 0, 45, 0, 0, 0); m.vault[0] = -5; dw_match_begin_round(&m); EQ(m.vault[0], -4);
    o = go(&m, 0, -1); EQ(m.done, 1); EQ(m.reason, DW_END_BANKRUPT); EQ(m.result[0], DW_RES_LOSS); EQ(m.result[1], DW_RES_WIN);
    /* Liandry's Torment (15): lands, then burns 2 per round for 3 rounds (ticks from the NEXT round) */
    start(&m, 22, 15, 0, 0, 0, 0, 0, 0, 0);
    o = go(&m, 0, -1); EQ(m.hull[1], 18); EQ(m.burn_left[1], 3);
    dw_match_begin_round(&m); o = go(&m, -1, -1); EQ(m.hull[1], 16); EQ(m.burn_left[1], 2);
    /* Dark Pool (34) with roll 10 becomes Naked Short (35): +3 energy; unhurt, so no default */
    start(&m, 23, 34, 0, 0, 0, 0, 0, 0, 0); m.roll[0] = 10;
    o = go(&m, 0, -1); EQ(o.eff[0], 35); EQ(m.energy[0], 6); EQ(m.hull[0], 20);
    /* ... and if you are hit the default costs 5 more: Dark Pool(roll 10 -> Naked Short) vs Railgun burst: 10 + 5 */
    start(&m, 24, 34, 0, 0, 0, 2, 0, 0, 0); m.roll[0] = 10;
    o = go(&m, 0, 0); EQ(m.hull[0], 5);
    /* Realm Warp (51: 4 energy + 3 credits) swaps hands after refills */
    start(&m, 25, 51, 1, 2, 3, 4, 5, 6, 7); m.draw_n[0] = 1; m.draw[0][0] = 60;
    o = go(&m, 0, -1);
    EQ(m.hand[0][0], 4); EQ(m.hand[0][1], 5); EQ(m.hand[0][2], 6); EQ(m.hand[0][3], 7);
    EQ(m.hand[1][0], 60); EQ(m.hand[1][1], 1); EQ(m.hand[1][2], 2); EQ(m.hand[1][3], 3); EQ((o.flags[0] & DW_FX_SWAPPED) != 0, 1);
    /* Corrosion (48) locks exactly one opposing slot next round, and the server-side lock check rejects it */
    start(&m, 26, 48, 0, 0, 0, 0, 0, 0, 0);
    o = go(&m, 0, -1); dw_match_begin_round(&m);
    { int bits = 0, slot = -1; for (int i = 0; i < 4; i++) if ((m.lock_mask[1] >> i) & 1) { bits++; slot = i; }
      EQ(bits, 1); EQ(m.lock_mask[0], 0); m.energy[1] = 6; EQ(dw_match_lock(&m, 1, slot), DW_REJ_ILLEGAL_CARD); }
    /* Dead Squares (62) armor soaks: 2 armor eats 2 of Railgun's 10; Piercing Round (25) ignores armor */
    start(&m, 27, 62, 0, 0, 0, 2, 0, 0, 0);
    o = go(&m, 0, 0); EQ(m.armor[0], 2);                             /* shield counters burst: takes 0, gains armor */
    start(&m, 28, 3, 0, 0, 0, 25, 0, 0, 0); m.armor[0] = 5;          /* Plating (tank) vs Piercing Round (burst 5, pierce) */
    o = go(&m, 0, 0); EQ(m.armor[0], 5); EQ(m.hull[0], 15);
    start(&m, 29, 3, 0, 0, 0, 0, 0, 0, 0); m.armor[0] = 5;           /* Plating vs Spark (burst 3): armor soaks all 3 */
    o = go(&m, 0, 0); EQ(m.armor[0], 2); EQ(m.hull[0], 20);
    /* Bribe (30: 1 energy + 2 credits) hits for 6; after paying, seat 0 has 4-2 = 2 credits */
    start(&m, 30, 30, 0, 0, 0, 0, 0, 0, 0);
    o = go(&m, 0, -1); EQ(m.hull[1], 14); EQ(m.vault[0], 2);
    /* the 8-round tiebreak: equal hull, more credits wins */
    dw_match_init(&m, 31); m.vault[0] = 9; m.vault[1] = 1; m.round = max_rounds() - 1;   /* jump to the last round (credits cap at 99, so a 100-round pass-fest would tie) */
    dw_match_begin_round(&m); dw_match_lock(&m, 0, -1); dw_match_lock(&m, 1, -1); dw_match_resolve(&m, &o);
    EQ(m.done, 1); EQ(m.reason, DW_END_ROUNDS); EQ(m.result[0], DW_RES_WIN); EQ(m.result[1], DW_RES_LOSS);
}

static void hidden_info(void) {
    DwMatch m; DwView v; dw_match_init(&m, 77); dw_match_begin_round(&m);
    dw_match_view(&m, 0, &v);
    EQ(memcmp(v.hand, m.hand[0], DW_HAND), 0); EQ(v.opp_hand_size, 4); EQ(v.round, 1); EQ(v.energy_you, 4);
    /* the view struct has no field that could carry the opponent's cards: only sizes/hull/meters */
    EQ(sizeof(DwView) <= 32, 1);
    for (int i = 0; i < DW_HAND; i++) EQ(m.hand[0][i] >= 0 && m.hand[0][i] < num_cards(), 1);
    EQ(v.vault_you, start_vault() + 1); EQ(v.armor_you, 0); EQ(v.lock_mask, 0);
}

/* play a full match with a scripted RNG-driven policy; returns a hash of the whole trace */
static uint32_t play(uint32_t seed, uint32_t policy_seed, int check, int *rounds_out) {
    DwMatch m; DwOutcome o; uint32_t h = 2166136261u, ps = policy_seed ? policy_seed : 1;
    dw_match_init(&m, seed);
    if (check) { EQ(deck_ok(&m, 0), 1); EQ(deck_ok(&m, 1), 1); }
    while (!m.done) {
        dw_match_begin_round(&m);
        if (check) { EQ(m.energy[0] <= 6 && m.energy[1] <= 6, 1); EQ(m.round <= max_rounds(), 1); }
        for (int s = 0; s < 2; s++) {
            int legal[5], n = 0;
            legal[n++] = -1;
            for (int i = 0; i < DW_HAND; i++) if (m.hand[s][i] >= 0 && !((m.lock_mask[s] >> i) & 1) && is_legal_play(m.hand[s][i], m.energy[s], m.vault[s])) legal[n++] = i;
            ps ^= ps << 13; ps ^= ps >> 17; ps ^= ps << 5;
            int pick = legal[ps % (uint32_t)n];
            EQ(dw_match_lock(&m, s, pick), 0);
            h = (h ^ (uint32_t)(pick + 2)) * 16777619u;
        }
        dw_match_resolve(&m, &o);
        h = (h ^ (uint32_t)(m.hull[0] + 100)) * 16777619u; h = (h ^ (uint32_t)(m.hull[1] + 100)) * 16777619u;
        if (check) {
            EQ(deck_ok(&m, 0), 1); EQ(deck_ok(&m, 1), 1);
            EQ(m.hull[0] <= 20 && m.hull[1] <= 20, 1); EQ(m.hull[0] >= -100 && m.hull[1] >= -100, 1);
            EQ(m.vault[0] >= -100 && m.vault[0] <= 99 && m.armor[0] <= DW_ARMOR_MAX && m.armor[1] <= DW_ARMOR_MAX, 1);
        }
    }
    if (check) {
        EQ(m.round <= max_rounds(), 1);
        EQ(m.result[0] == DW_RES_WIN ? m.result[1] == DW_RES_LOSS : m.result[0] == DW_RES_LOSS ? m.result[1] == DW_RES_WIN : m.result[1] == DW_RES_DRAW, 1);
        if (m.reason == DW_END_HULL) EQ(m.hull[0] <= 0 || m.hull[1] <= 0, 1);
        if (m.reason == DW_END_BANKRUPT) EQ(bankrupt(m.vault[0]) || bankrupt(m.vault[1]), 1);
    }
    if (rounds_out) *rounds_out = m.round;
    return h;
}

int main(void) {
    scripted_a(); scripted_end(); guild_cards(); hidden_info();
    /* determinism: same (seed, plays) -> identical trace; different seed -> different initial deal */
    for (uint32_t s = 1; s <= 200; s++) EQ(play(s, s * 7, 0, NULL), play(s, s * 7, 0, NULL));
    DwMatch a, b; dw_match_init(&a, 1); dw_match_init(&b, 1); EQ(memcmp(&a, &b, sizeof a), 0);
    dw_match_init(&b, 2); EQ(memcmp(a.hand, b.hand, sizeof a.hand) != 0, 1);
    dw_match_init(&a, 9); EQ(memcmp(a.hand[0], a.hand[1], DW_HAND) != 0 || a.rng[0] != a.rng[1], 1);
    int longest = 0, ended_early = 0, draws = 0;
    for (int i = 0; i < 10000; i++) {
        int r; play(rnd(), rnd(), 1, &r);
        if (r > longest) longest = r;
        if (r < 8) ended_early++;
    }
    for (uint32_t i = 0; i < 300; i++) { DwMatch m; dw_match_init(&m, i); (void)m; }
    (void)draws;
    EQ(longest <= max_rounds(), 1); EQ(ended_early > 0, 1);
    printf("test_match: %d checks, %d failures (longest game %d rounds, %d/10000 ended early)\n", checks, fails, longest, ended_early);
    return fails ? 1 : 0;
}
