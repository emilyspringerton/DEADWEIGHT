/* Emits the cross-target parity vectors: every rules function over its full input domain, computed by the
 * PARENA->C build. The Java test (android/.../ParityTest.java) recomputes each line via the PARENA->Java build
 * and must match exactly. Line format: "<fn> <args...> = <result>". */
#include <stdio.h>
#include "card_rules.h"

int main(void) {
    printf("start_hull = %d\nstart_energy = %d\nmax_rounds = %d\nhand_size = %d\nnum_cards = %d\n",
           start_hull(), start_energy(), max_rounds(), hand_size(), num_cards());
    for (int e = -2; e <= 9; e++) printf("energy_cap %d = %d\n", e, energy_cap(e));
    for (int id = 0; id <= 8; id++) {
        printf("card_kind %d = %d\ncard_tier %d = %d\ncard_cost %d = %d\ncard_power %d = %d\n",
               id, card_kind(id), id, card_tier(id), id, card_cost(id), id, card_power(id));
    }
    for (int a = 0; a <= 2; a++) for (int b = 0; b <= 2; b++) printf("kind_beats %d %d = %d\n", a, b, kind_beats(a, b));
    for (int id = -1; id <= 9; id++) for (int e = 0; e <= 8; e++) printf("is_legal_play %d %d = %d\n", id, e, is_legal_play(id, e));
    for (int a = -1; a <= 8; a++) for (int b = -1; b <= 8; b++) printf("damage_dealt %d %d = %d\n", a, b, damage_dealt(a, b));
    for (int e = 0; e <= 8; e++) printf("round_start_energy %d = %d\n", e, round_start_energy(e));
    for (int e = 0; e <= 8; e++) for (int p = -1; p <= 8; p++) printf("energy_after_play %d %d = %d\n", e, p, energy_after_play(e, p));
    for (int h0 = -3; h0 <= 21; h0++) for (int h1 = -3; h1 <= 21; h1++) {
        printf("round_winner_by_hull %d %d = %d\nmatch_decided %d %d = %d\n",
               h0, h1, round_winner_by_hull(h0, h1), h0, h1, match_decided(h0, h1));
    }
    return 0;
}
