/* Emits the cross-target parity vectors: every rules function over its domain (the effect engine over a seeded
 * random sample of contexts), computed by the PARENA->C build. The Java test (android/.../ParityTest.java) recomputes
 * each line via the PARENA->Java build and must match exactly. Line format: "<fn> <int args...> = <result>". */
#include <stdio.h>
#include <stdint.h>
#include "card_rules.h"

static uint32_t rs = 0x1234567u;
static int rnd(int lo, int hi) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return lo + (int)(rs % (uint32_t)(hi - lo + 1)); }

int main(void) {
    printf("start_hull = %d\nstart_energy = %d\nstart_vault = %d\nmax_rounds = %d\nhand_size = %d\nnum_cards = %d\n",
           start_hull(), start_energy(), start_vault(), max_rounds(), hand_size(), num_cards());
    for (int e = -2; e <= 9; e++) printf("energy_cap %d = %d\n", e, energy_cap(e));
    for (int id = 0; id < num_cards(); id++) {
        printf("card_kind %d = %d\ncard_tier %d = %d\ncard_cost %d = %d\ncard_power %d = %d\ncard_credit %d = %d\n",
               id, card_kind(id), id, card_tier(id), id, card_cost(id), id, card_power(id), id, card_credit(id));
        printf("card_fx_a %d = %d\ncard_fx_b %d = %d\ncard_keyword %d = %d\n", id, card_fx_a(id), id, card_fx_b(id), id, card_keyword(id));
    }
    static const int rolls[] = {0, 24, 25, 49, 50, 74, 75, 99};
    for (int id = -1; id <= num_cards(); id++) for (int r = 0; r < 8; r++) printf("card_substitute %d %d = %d\n", id, rolls[r], card_substitute(id, rolls[r]));
    for (int a = 0; a <= 2; a++) for (int b = 0; b <= 2; b++) printf("kind_beats %d %d = %d\n", a, b, kind_beats(a, b));
    static const int vaults[] = {-2, 0, 1, 2, 3, 9};
    for (int id = -1; id <= num_cards(); id++) for (int e = 0; e <= 8; e++) for (int v = 0; v < 6; v++)
        printf("is_legal_play %d %d %d = %d\n", id, e, vaults[v], is_legal_play(id, e, vaults[v]));
    for (int a = -1; a < num_cards(); a++) for (int b = -1; b < num_cards(); b++) printf("damage_dealt %d %d = %d\n", a, b, damage_dealt(a, b));
    for (int e = 0; e <= 8; e++) printf("round_start_energy %d = %d\n", e, round_start_energy(e));
    static const int vs[] = {-100, -7, -1, 0, 1, 50, 98, 99};
    for (int i = 0; i < 8; i++) printf("round_start_vault %d = %d\n", vs[i], round_start_vault(vs[i]));
    for (int e = 0; e <= 8; e++) for (int p = -1; p < num_cards(); p++) printf("energy_after_play %d %d = %d\n", e, p, energy_after_play(e, p));
    for (int h0 = -3; h0 <= 21; h0++) for (int h1 = -3; h1 <= 21; h1++) {
        printf("round_winner_by_hull %d %d = %d\nmatch_decided %d %d = %d\n",
               h0, h1, round_winner_by_hull(h0, h1), h0, h1, match_decided(h0, h1));
    }
    static const int hh[] = {-2, 3, 7}, vv[] = {-3, 0, 5};
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) for (int l = 0; l < 3; l++)
        printf("round_winner %d %d %d %d = %d\n", hh[i], hh[j], vv[k], vv[l], round_winner(hh[i], hh[j], vv[k], vv[l]));
    for (int v = -10; v <= 5; v++) printf("bankrupt %d = %d\n", v, bankrupt(v));
    /* effect engine: decoders over every distinct word, amounts over a seeded random context sample */
    for (int id = 0; id < num_cards(); id++) {
        int ws[2] = { card_fx_a(id), card_fx_b(id) };
        for (int k = 0; k < 2; k++) {
            int w = ws[k]; if (!w) continue;
            printf("fx_ch %d = %d\nfx_phase %d = %d\n", w, fx_ch(w), fx_ch(w), fx_phase(fx_ch(w)));
            for (int t = 0; t < 24; t++) {
                int dealt = rnd(0, 14), taken = rnd(0, 14), mk = rnd(0, 2), ok = rnd(-1, 2), oc = rnd(0, 6), mh = rnd(-5, 20), oh = rnd(-5, 20);
                int el = rnd(0, 6), oe = rnd(0, 6), mv = rnd(-8, 10), ov = rnd(-8, 10), rd = rnd(1, 8), roll = rnd(0, 99);
                printf("fx_amount %d %d %d %d %d %d %d %d %d %d %d %d %d %d = %d\n", w, dealt, taken, mk, ok, oc, mh, oh, el, oe, mv, ov, rd, roll,
                       fx_amount(w, dealt, taken, mk, ok, oc, mh, oh, el, oe, mv, ov, rd, roll));
            }
        }
    }
    return 0;
}
