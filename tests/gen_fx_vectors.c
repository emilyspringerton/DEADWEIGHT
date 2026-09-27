/* Emits the fx (round-resolution animation/audio DECISION layer) cross-target parity vectors: every
 * core/fx_rules.c function (generated from PARENA/stdlib/deadweight/fx_rules.prn) over its real
 * domain, computed by the PARENA->C build. android/.../FxParityTest.java recomputes each line via the
 * PARENA->Java build (generated/FxRules.java) and must match exactly -- S555 phase 2, see
 * docs/ANDROID_PARITY_NORTHSTAR.md. Same line format as tests/gen_vectors.c:
 * "<fn> <int args...> = <result>" (Bool results as 0/1). Compile with -include card_rules.h. */
#include <stdio.h>
#include "card_rules.h"
#include "fx_rules.h"

int main(void) {
    int n = num_cards();
    static const int ids[] = {-1, 0, 1, 2, 5, 9, 40, 72, 73, 104};
    const int nids = (int)(sizeof ids / sizeof ids[0]);
    for (int e = 0; e < nids; e++) for (int d = 0; d < nids; d++) for (int c = 0; c <= 1; c++)
        printf("fx_card_id %d %d %d = %d\n", ids[e], ids[d], c, fx_card_id(ids[e], ids[d], c));
    for (int id = -1; id < n; id++) printf("fx_kind_of %d = %d\n", id, fx_kind_of(id));
    for (int a = -1; a <= 2; a++) for (int b = -1; b <= 2; b++) for (int ca = 0; ca <= 1; ca++) for (int cb = 0; cb <= 1; cb++) {
        printf("fx_scenario %d %d %d %d = %d\n", a, b, ca, cb, fx_scenario(a, b, ca, cb));
        printf("fx_winner_seat %d %d %d %d = %d\n", a, b, ca, cb, fx_winner_seat(a, b, ca, cb));
    }
    static const int dmg[] = {0, 5, 7, 8, 9, 15}, pw[] = {0, 2, 4, 5, 7}, pl[] = {0, 1, 3, 4}, cost[] = {0, 3, 4, 5, 6};
    for (int sc = 0; sc <= 11; sc++) for (int i = 0; i < 6; i++) for (int j = 0; j < 5; j++) for (int k = 0; k < 4; k++) for (int m = 0; m < 5; m++)
        printf("fx_is_crit %d %d %d %d %d = %d\n", sc, dmg[i], pw[j], pl[k], cost[m], fx_is_crit(sc, dmg[i], pw[j], pl[k], cost[m]) ? 1 : 0);
    for (int sc = 0; sc <= 11; sc++) for (int cr = 0; cr <= 1; cr++) {
        printf("fx_clash_duration_ms %d %d = %d\n", sc, cr, fx_clash_duration_ms(sc, cr));
        for (int kw = 0; kw <= 5; kw++) printf("fx_clash_cue %d %d %d = %d\n", sc, cr, kw, fx_clash_cue(sc, cr, kw));
    }
    printf("fx_clash0 = %d\n", fx_clash0());
    static const int durs[] = {0, 700, 1000, 1100, 1200, 1400, 1500, 1700, 1900, 2000, 2200};
    for (int i = 0; i < (int)(sizeof durs / sizeof durs[0]); i++) {
        int cd = durs[i];
        printf("fx_clash1 %d = %d\n", cd, fx_clash1(cd));
        printf("fx_hull0 %d = %d\n", cd, fx_hull0(cd));
        for (int h = 0; h <= 1; h++) {
            printf("fx_hull1 %d %d = %d\n", cd, h, fx_hull1(cd, h));
            printf("fx_armor0 %d %d = %d\n", cd, h, fx_armor0(cd, h));
            for (int a = 0; a <= 1; a++) {
                printf("fx_armor1 %d %d %d = %d\n", cd, h, a, fx_armor1(cd, h, a));
                printf("fx_econ0 %d %d %d = %d\n", cd, h, a, fx_econ0(cd, h, a));
                for (int e = 0; e <= 1; e++) {
                    printf("fx_econ1 %d %d %d %d = %d\n", cd, h, a, e, fx_econ1(cd, h, a, e));
                    printf("fx_stat0 %d %d %d %d = %d\n", cd, h, a, e, fx_stat0(cd, h, a, e));
                    for (int s = 0; s <= 1; s++) {
                        printf("fx_stat1 %d %d %d %d %d = %d\n", cd, h, a, e, s, fx_stat1(cd, h, a, e, s));
                        printf("fx_settle0 %d %d %d %d %d = %d\n", cd, h, a, e, s, fx_settle0(cd, h, a, e, s));
                        printf("fx_timeline_total_ms %d %d %d %d %d = %d\n", cd, h, a, e, s, fx_timeline_total_ms(cd, h, a, e, s));
                    }
                }
            }
        }
    }
    static const int v[] = {-2, 0, 1, 3};
    for (int a = 0; a < 4; a++) for (int b = 0; b < 4; b++) for (int c = 0; c < 4; c++) for (int d = 0; d < 4; d++) {
        printf("fx_has_hull %d %d %d %d = %d\n", v[a], v[b], v[c], v[d], fx_has_hull(v[a], v[b], v[c], v[d]) ? 1 : 0);
        printf("fx_has_armor %d %d %d %d = %d\n", v[a], v[b], v[c], v[d], fx_has_armor(v[a], v[b], v[c], v[d]) ? 1 : 0);
        printf("fx_has_econ %d %d %d %d = %d\n", v[a], v[b], v[c], v[d], fx_has_econ(v[a], v[b], v[c], v[d]) ? 1 : 0);
        for (int e = 0; e <= 1; e++)
            printf("fx_has_stat %d %d %d %d %d = %d\n", v[a] & 3, v[b] & 3, v[c] & 1, v[d] & 1, e,
                   fx_has_stat(v[a] & 3, v[b] & 3, v[c] & 1, v[d] & 1, e) ? 1 : 0);
    }
    return 0;
}
