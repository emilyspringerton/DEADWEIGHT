/* Independent oracle for the card rules: expected values are typed by hand from docs/CARD_MODE_RULES.md, NOT
 * derived from card_rules.c, so a rules regression can't grade itself. Also checks the parity-vector file matches
 * the C build (the Java test checks the same file against the Java build). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "card_rules.h"

static int fails = 0, checks = 0;
#define EQ(expr, want) do { checks++; int got_ = (expr); if (got_ != (want)) { fails++; \
    printf("FAIL %s:%d  %s = %d, want %d\n", __FILE__, __LINE__, #expr, got_, (want)); } } while (0)

int main(int argc, char **argv) {
    const char *vec = argc > 1 ? argv[1] : "tests/parity_vectors.txt";
    /* constants */
    EQ(start_hull(), 20); EQ(start_energy(), 2); EQ(max_rounds(), 8); EQ(hand_size(), 4); EQ(num_cards(), 9);
    /* catalog: id = kind*3+tier ; cost 1/2/4 ; power 3/6/10 */
    int cost[3] = {1, 2, 4}, power[3] = {3, 6, 10};
    for (int id = 0; id < 9; id++) {
        EQ(card_kind(id), id / 3); EQ(card_tier(id), id % 3);
        EQ(card_cost(id), cost[id % 3]); EQ(card_power(id), power[id % 3]);
    }
    /* triangle BURST(0) > TANK(1) > SHIELD(2) > BURST */
    EQ(kind_beats(0, 1), 1); EQ(kind_beats(1, 2), 1); EQ(kind_beats(2, 0), 1);
    EQ(kind_beats(1, 0), 0); EQ(kind_beats(2, 1), 0); EQ(kind_beats(0, 2), 0);
    EQ(kind_beats(0, 0), 0); EQ(kind_beats(1, 1), 0); EQ(kind_beats(2, 2), 0);
    /* damage examples (rules doc): winner deals power, loser 0 */
    EQ(damage_dealt(2, 3), 10);   /* burst t2 beats tank t0 */
    EQ(damage_dealt(3, 2), 0);    /* countered */
    EQ(damage_dealt(5, 6), 10);   /* tank t2 beats shield t0 */
    EQ(damage_dealt(6, 0), 3);    /* shield t0 counters burst t0: reflects its power */
    EQ(damage_dealt(0, 6), 0);
    EQ(damage_dealt(2, 1), 10);   /* burst vs burst: both full power */
    EQ(damage_dealt(1, 2), 6);
    EQ(damage_dealt(5, 4), 5);    /* tank vs tank: power/2 */
    EQ(damage_dealt(3, 3), 1);
    EQ(damage_dealt(8, 7), 0);    /* shield vs shield */
    EQ(damage_dealt(2, -1), 10);  /* vs pass: burst/tank unopposed */
    EQ(damage_dealt(5, -1), 10);
    EQ(damage_dealt(8, -1), 0);   /* shield vs pass deals nothing */
    EQ(damage_dealt(-1, 2), 0);   /* pass deals nothing */
    EQ(damage_dealt(-1, -1), 0);
    /* legality */
    EQ(is_legal_play(2, 4), 1); EQ(is_legal_play(2, 3), 0); EQ(is_legal_play(0, 1), 1); EQ(is_legal_play(0, 0), 0);
    EQ(is_legal_play(-1, 6), 0); EQ(is_legal_play(9, 8), 0);
    /* energy */
    EQ(round_start_energy(2), 4); EQ(round_start_energy(5), 6); EQ(round_start_energy(6), 6);
    EQ(energy_after_play(4, 2), 0); EQ(energy_after_play(4, 0), 3);
    EQ(energy_after_play(3, -1), 4); EQ(energy_after_play(6, -1), 6);
    /* end conditions */
    EQ(match_decided(0, 5), 1); EQ(match_decided(5, 0), 1); EQ(match_decided(-3, 4), 1); EQ(match_decided(1, 1), 0);
    EQ(round_winner_by_hull(7, 3), 0); EQ(round_winner_by_hull(3, 7), 1); EQ(round_winner_by_hull(4, 4), 2);

    /* parity vectors must agree with the C build */
    FILE *f = fopen(vec, "r");
    if (!f) { printf("FAIL cannot open %s\n", vec); return 1; }
    char line[128]; int vlines = 0;
    while (fgets(line, sizeof line, f)) {
        char fn[64]; int a[2] = {0, 0}, want, n;
        char *eq = strstr(line, " = ");
        if (!eq) { fails++; printf("FAIL malformed vector: %s", line); continue; }
        want = atoi(eq + 3);
        *eq = 0;
        n = sscanf(line, "%63s %d %d", fn, &a[0], &a[1]) - 1;
        int got;
        if (!strcmp(fn, "start_hull")) got = start_hull();
        else if (!strcmp(fn, "start_energy")) got = start_energy();
        else if (!strcmp(fn, "max_rounds")) got = max_rounds();
        else if (!strcmp(fn, "hand_size")) got = hand_size();
        else if (!strcmp(fn, "num_cards")) got = num_cards();
        else if (!strcmp(fn, "energy_cap")) got = energy_cap(a[0]);
        else if (!strcmp(fn, "card_kind")) got = card_kind(a[0]);
        else if (!strcmp(fn, "card_tier")) got = card_tier(a[0]);
        else if (!strcmp(fn, "card_cost")) got = card_cost(a[0]);
        else if (!strcmp(fn, "card_power")) got = card_power(a[0]);
        else if (!strcmp(fn, "kind_beats")) got = kind_beats(a[0], a[1]);
        else if (!strcmp(fn, "is_legal_play")) got = is_legal_play(a[0], a[1]);
        else if (!strcmp(fn, "damage_dealt")) got = damage_dealt(a[0], a[1]);
        else if (!strcmp(fn, "round_start_energy")) got = round_start_energy(a[0]);
        else if (!strcmp(fn, "energy_after_play")) got = energy_after_play(a[0], a[1]);
        else if (!strcmp(fn, "round_winner_by_hull")) got = round_winner_by_hull(a[0], a[1]);
        else if (!strcmp(fn, "match_decided")) got = match_decided(a[0], a[1]);
        else { fails++; printf("FAIL unknown vector fn %s\n", fn); continue; }
        (void)n; checks++; vlines++;
        if (got != want) { fails++; printf("FAIL vector %s %d %d: got %d want %d\n", fn, a[0], a[1], got, want); }
    }
    fclose(f);
    if (vlines < 1000) { fails++; printf("FAIL only %d parity vectors read\n", vlines); }
    printf("test_rules: %d checks (%d parity vectors), %d failures\n", checks, vlines, fails);
    return fails ? 1 : 0;
}
