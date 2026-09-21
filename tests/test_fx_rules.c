/* Independent oracle for core/fx_rules.c (generated from PARENA/stdlib/deadweight/fx_rules.prn) --
 * every expected value here is a hand-transcription of apps/gui/fx.c's ORIGINAL scenario/timeline
 * logic (as it existed before that file was refactored to call these generated functions), not
 * derived from fx_rules.c itself, so a real regression here can't grade itself. Same discipline
 * tests/test_rules.c already uses for card_rules.c.
 *
 * Exhaustive over every real kind/cancel/scenario/keyword combination the game can actually
 * produce (3 kinds + "no card" x2 seats x2 cancel flags = the same 64-case space this logic was
 * first cross-checked against live, 2026-09-21, before apps/gui/fx.c's own refactor landed --
 * see PARENA/CHANGELOG.md and DEADWEIGHT/CHANGELOG.md the same date). */
#include <stdio.h>
#include "card_rules.h"
#include "fx_rules.h"

static int fails = 0, checks = 0;
#define EQ(expr, want) do { checks++; int got_ = (expr); if (got_ != (want)) { fails++; \
    printf("FAIL %s:%d  %s = %d, want %d\n", __FILE__, __LINE__, #expr, got_, (want)); } } while (0)

/* Independent oracle transcription of the ORIGINAL apps/gui/fx.c logic (SC_NONE=0 .. SC_CANCEL=10). */
static void oracle_scenario_winner(int a, int b, int ca, int cb, int *sc, int *win) {
    *win = -1;
    if (ca || cb) { *sc = 10; return; }                       /* SC_CANCEL */
    if (a < 0 && b < 0) { *sc = 9; return; }                   /* SC_BOTH_PASS */
    if (a < 0 || b < 0) {                                      /* unopposed / hold-shield */
        int w = a >= 0 ? 0 : 1; *win = w;
        int wk = a >= 0 ? a : b;
        *sc = wk == 2 ? 8 : 7;                                 /* SC_HOLD_SHIELD : SC_UNOPPOSED */
        return;
    }
    if (a == b) { *sc = a == 0 ? 4 : a == 1 ? 5 : 6; return; } /* mirror */
    int w = kind_beats(a, b) ? 0 : 1; *win = w;
    int wk = w == 0 ? a : b;
    *sc = wk == 0 ? 1 : wk == 2 ? 2 : 3;                       /* BLITZ : BLOCK : BYPASS */
}

static int oracle_clash_ms(int sc, int crit) {
    switch (sc) {
    case 1: return crit ? 1900 : 1500;
    case 2: return crit ? 2000 : 1700;
    case 3: return crit ? 2200 : 2000;
    case 4: case 5: case 6: return 1400;
    case 7: return 1200;
    case 8: return 1000;
    case 9: return 700;
    case 10: return 1100;
    default: return 1400;
    }
}

static int oracle_cue(int sc, int crit, int kw) {
    switch (sc) {
    case 1: return crit ? 1 : 0;
    case 2: return crit ? 3 : 2;
    case 3: return crit ? 9 : (kw == 1 ? 4 : kw == 2 ? 5 : kw == 3 ? 6 : kw == 4 ? 7 : 8);
    case 4: return 10; case 5: return 11; case 6: return 12;
    case 7: return 13; case 8: return 16; case 9: return 14; case 10: return 15;
    default: return 14;
    }
}

static int oracle_crit(int sc, int dmg_lose, int power_win, int power_lose, int cost_win) {
    if (sc == 1) return dmg_lose >= 8;
    if (sc == 2) return (power_win - power_lose) >= 3;
    if (sc == 3) return dmg_lose >= 8 || cost_win >= 5;
    return 0;
}

int main(void) {
    /* fx_card_id */
    EQ(fx_card_id(5, 2, 0), 5);        /* eff >= 0 wins regardless of declared/cancelled */
    EQ(fx_card_id(-1, 7, 1), 7);       /* no eff, cancelled: falls back to declared */
    EQ(fx_card_id(-1, 7, 0), -1);      /* no eff, not cancelled: pass */
    EQ(fx_kind_of(-1), -1);
    EQ(fx_kind_of(0), card_kind(0));

    /* scenario + winner, exhaustive over the real kind/cancel space */
    for (int a = -1; a <= 2; a++) for (int b = -1; b <= 2; b++) for (int ca = 0; ca <= 1; ca++) for (int cb = 0; cb <= 1; cb++) {
        int osc, owin; oracle_scenario_winner(a, b, ca, cb, &osc, &owin);
        EQ(fx_scenario(a, b, ca, cb), osc);
        EQ(fx_winner_seat(a, b, ca, cb), owin);
    }

    /* clash duration + clash cue, exhaustive over every real scenario/crit/keyword */
    for (int sc = 0; sc <= 10; sc++) for (int crit = 0; crit <= 1; crit++) {
        EQ(fx_clash_duration_ms(sc, crit), oracle_clash_ms(sc, crit));
        for (int kw = 0; kw <= 5; kw++) EQ(fx_clash_cue(sc, crit, kw), oracle_cue(sc, crit, kw));
    }

    /* crit, over a real range of damage/power/cost inputs for each real crit-eligible scenario */
    for (int sc = 0; sc <= 10; sc++) for (int dl = 0; dl <= 12; dl += 3) for (int pw = 0; pw <= 12; pw += 4) for (int pl = 0; pl <= 12; pl += 4) for (int cw = 0; cw <= 8; cw += 4)
        EQ(fx_is_crit(sc, dl, pw, pl, cw), oracle_crit(sc, dl, pw, pl, cw));

    /* timeline stage math -- the exact running-total layout begin_stages() used before this refactor */
    for (int clash = 700; clash <= 2200; clash += 300) for (int hh = 0; hh <= 1; hh++) for (int ha = 0; ha <= 1; ha++) for (int he = 0; he <= 1; he++) for (int hs = 0; hs <= 1; hs++) {
        int t = 600; int clash0 = t; t += clash; int clash1 = t; t += 220;
        int hull0 = t; if (hh) t += 950; int hull1 = t; if (hh) t += 120;
        int armor0 = t; if (ha) t += 850; int armor1 = t; if (ha) t += 100;
        int econ0 = t; if (he) t += 950; int econ1 = t; if (he) t += 100;
        int stat0 = t; if (hs) t += 750; int stat1 = t;
        int settle0 = t; t += 320;
        int total = t < 9000 ? t : 9000;
        EQ(fx_clash0(), clash0);
        EQ(fx_clash1(clash), clash1);
        EQ(fx_hull0(clash), hull0);
        EQ(fx_hull1(clash, hh), hull1);
        EQ(fx_armor0(clash, hh), armor0);
        EQ(fx_armor1(clash, hh, ha), armor1);
        EQ(fx_econ0(clash, hh, ha), econ0);
        EQ(fx_econ1(clash, hh, ha, he), econ1);
        EQ(fx_stat0(clash, hh, ha, he), stat0);
        EQ(fx_stat1(clash, hh, ha, he, hs), stat1);
        EQ(fx_settle0(clash, hh, ha, he, hs), settle0);
        EQ(fx_timeline_total_ms(clash, hh, ha, he, hs), total);
    }

    /* has-* stage triggers */
    EQ(fx_has_hull(0, 0, 0, 0), 0); EQ(fx_has_hull(1, 0, 0, 0), 1); EQ(fx_has_hull(0, 0, 0, 1), 1);
    EQ(fx_has_armor(0, 0, 0, 0), 0); EQ(fx_has_armor(0, 1, 0, 0), 1); EQ(fx_has_armor(2, 2, 3, 1), 1);
    EQ(fx_has_econ(0, 0, 0, 0), 0); EQ(fx_has_econ(1, 0, 0, 0), 1); EQ(fx_has_econ(0, 0, -2, 0), 1);
    EQ(fx_has_stat(0, 0, 0, 0, 0), 0); EQ(fx_has_stat(1, 0, 0, 0, 0), 1); EQ(fx_has_stat(0, 0, 0, 1, 0), 1); EQ(fx_has_stat(0, 0, 0, 0, 1), 1);

    printf("test_fx_rules: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
