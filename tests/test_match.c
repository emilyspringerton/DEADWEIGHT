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

/* every card 0..8 appears exactly once across hand+draw+discard for a seat */
static int deck_ok(const DwMatch *m, int s) {
    int seen[DW_DECK] = {0}, total = 0;
    for (int i = 0; i < DW_HAND; i++) if (m->hand[s][i] >= 0) { if (m->hand[s][i] >= DW_DECK || seen[(int)m->hand[s][i]]++) return 0; total++; }
    for (int i = 0; i < m->draw_n[s]; i++) { if (seen[(int)m->draw[s][i]]++) return 0; total++; }
    for (int i = 0; i < m->discard_n[s]; i++) { if (seen[(int)m->discard[s][i]]++) return 0; total++; }
    return total == DW_DECK;
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
    /* all-pass 8 rounds: energy stays capped at 6; hull 20 vs 17 -> seat 0 wins on rounds */
    dw_match_init(&m, 4); m.hull[1] = 17;
    for (int r = 1; r <= 8; r++) {
        dw_match_begin_round(&m); EQ(m.done, 0); EQ(m.energy[0] <= 6, 1);
        dw_match_lock(&m, 0, -1); dw_match_lock(&m, 1, -1); dw_match_resolve(&m, &o);
    }
    EQ(m.round, 8); EQ(m.done, 1); EQ(m.reason, DW_END_ROUNDS); EQ(m.result[0], DW_RES_WIN); EQ(m.result[1], DW_RES_LOSS);
    EQ(m.energy[0], 6);
    /* equal hull at round cap = draw */
    dw_match_init(&m, 5);
    for (int r = 1; r <= 8; r++) { dw_match_begin_round(&m); dw_match_lock(&m, 0, -1); dw_match_lock(&m, 1, -1); dw_match_resolve(&m, &o); }
    EQ(m.result[0], DW_RES_DRAW);
    /* forfeit */
    dw_match_init(&m, 6); dw_match_begin_round(&m); dw_match_forfeit(&m, 1);
    EQ(m.done, 1); EQ(m.reason, DW_END_FORFEIT); EQ(m.result[0], DW_RES_WIN); EQ(m.result[1], DW_RES_LOSS);
}

static void hidden_info(void) {
    DwMatch m; DwView v; dw_match_init(&m, 77); dw_match_begin_round(&m);
    dw_match_view(&m, 0, &v);
    EQ(memcmp(v.hand, m.hand[0], DW_HAND), 0); EQ(v.opp_hand_size, 4); EQ(v.round, 1); EQ(v.energy_you, 4);
    /* the view struct has no field that could carry the opponent's cards: only sizes/hull/energy */
    EQ(sizeof(DwView) <= 16, 1);
    for (int i = 0; i < DW_HAND; i++) EQ(m.hand[0][i] >= 0 && m.hand[0][i] < 9, 1);
}

/* play a full match with a scripted RNG-driven policy; returns a hash of the whole trace */
static uint32_t play(uint32_t seed, uint32_t policy_seed, int check, int *rounds_out) {
    DwMatch m; DwOutcome o; uint32_t h = 2166136261u, ps = policy_seed ? policy_seed : 1;
    dw_match_init(&m, seed);
    if (check) { EQ(deck_ok(&m, 0), 1); EQ(deck_ok(&m, 1), 1); }
    while (!m.done) {
        dw_match_begin_round(&m);
        if (check) { EQ(m.energy[0] <= 6 && m.energy[1] <= 6, 1); EQ(m.round <= 8, 1); }
        for (int s = 0; s < 2; s++) {
            int legal[5], n = 0;
            legal[n++] = -1;
            for (int i = 0; i < DW_HAND; i++) if (m.hand[s][i] >= 0 && is_legal_play(m.hand[s][i], m.energy[s])) legal[n++] = i;
            ps ^= ps << 13; ps ^= ps >> 17; ps ^= ps << 5;
            int pick = legal[ps % (uint32_t)n];
            EQ(dw_match_lock(&m, s, pick), 0);
            h = (h ^ (uint32_t)(pick + 2)) * 16777619u;
        }
        dw_match_resolve(&m, &o);
        h = (h ^ (uint32_t)(m.hull[0] + 100)) * 16777619u; h = (h ^ (uint32_t)(m.hull[1] + 100)) * 16777619u;
        if (check) {
            EQ(deck_ok(&m, 0), 1); EQ(deck_ok(&m, 1), 1);
            EQ(m.hull[0] <= 20 && m.hull[1] <= 20, 1); EQ(m.hull[0] >= -10 && m.hull[1] >= -10, 1);
        }
    }
    if (check) {
        EQ(m.round <= 8, 1);
        EQ(m.result[0] == DW_RES_WIN ? m.result[1] == DW_RES_LOSS : m.result[0] == DW_RES_LOSS ? m.result[1] == DW_RES_WIN : m.result[1] == DW_RES_DRAW, 1);
        if (m.reason == DW_END_HULL) EQ(m.hull[0] <= 0 || m.hull[1] <= 0, 1);
    }
    if (rounds_out) *rounds_out = m.round;
    return h;
}

int main(void) {
    scripted_a(); scripted_end(); hidden_info();
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
    EQ(longest <= 8, 1); EQ(ended_early > 0, 1);
    printf("test_match: %d checks, %d failures (longest game %d rounds, %d/10000 ended early)\n", checks, fails, longest, ended_early);
    return fails ? 1 : 0;
}
