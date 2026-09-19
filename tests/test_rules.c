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
    EQ(start_hull(), 20); EQ(start_energy(), 2); EQ(start_vault(), 3); EQ(max_rounds(), 8); EQ(hand_size(), 4); EQ(num_cards(), 73);
    /* base catalog: id = kind*3+tier ; cost 1/2/4 ; power 3/6/10 */
    int cost[3] = {1, 2, 4}, power[3] = {3, 6, 10};
    for (int id = 0; id < 9; id++) {
        EQ(card_kind(id), id / 3); EQ(card_tier(id), id % 3);
        EQ(card_cost(id), cost[id % 3]); EQ(card_power(id), power[id % 3]); EQ(card_credit(id), 0);
        EQ(card_fx_a(id), 0); EQ(card_fx_b(id), 0);
    }
    /* Guild card blocks: 9-30 BURST, 31-51 TANK, 52-72 SHIELD (hand-typed from docs/CARD_MODE_RULES.md) */
    EQ(card_kind(9), 0); EQ(card_kind(30), 0); EQ(card_kind(31), 1); EQ(card_kind(51), 1); EQ(card_kind(52), 2); EQ(card_kind(72), 2);
    EQ(card_cost(28), 2); EQ(card_power(28), 6);          /* Iron Dwarf: 2 energy, 6 power (+2 rider) */
    EQ(card_cost(50), 6); EQ(card_cost(36), 5); EQ(card_cost(17), 5); EQ(card_power(17), 14);   /* Total Blackout, Hostile Takeover, Final Spark */
    EQ(card_cost(41), 1); EQ(card_power(41), 3);          /* Quartermaster */
    EQ(card_credit(13), 2); EQ(card_credit(30), 2); EQ(card_credit(51), 3); EQ(card_credit(59), 2); EQ(card_credit(28), 0);
    EQ(card_tier(9), 1); EQ(card_tier(10), 0); EQ(card_tier(17), 2);   /* Guild tier = cost band: <=1 low, <=3 mid, else high */
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
    EQ(damage_dealt(28, 31), 6);  /* Iron Dwarf (burst 6) vs Yield Contract (tank) */
    EQ(damage_dealt(52, 28), 4);  /* Put Option (shield 4) counters Iron Dwarf: reflects its own power */
    EQ(damage_dealt(28, 52), 0);
    /* legality: energy AND credits */
    EQ(is_legal_play(2, 4, 0), 1); EQ(is_legal_play(2, 3, 0), 0); EQ(is_legal_play(0, 1, 0), 1); EQ(is_legal_play(0, 0, 0), 0);
    EQ(is_legal_play(-1, 6, 9), 0); EQ(is_legal_play(73, 8, 9), 0); EQ(is_legal_play(72, 6, 0), 1);
    EQ(is_legal_play(2, 4, -3), 1); EQ(is_legal_play(2, 4, -99), 1);   /* debt never blocks cards that cost no credits */
    EQ(is_legal_play(30, 1, -1), 0);
    EQ(is_legal_play(30, 1, 1), 0); EQ(is_legal_play(30, 1, 2), 1);      /* Bribe: 1 energy + 2 credits */
    EQ(is_legal_play(51, 4, 2), 0); EQ(is_legal_play(51, 4, 3), 1); EQ(is_legal_play(51, 3, 9), 0);   /* Realm Warp: 4 energy + 3 credits */
    /* economy */
    EQ(round_start_energy(2), 4); EQ(round_start_energy(5), 6); EQ(round_start_energy(6), 6);
    EQ(round_start_vault(3), 4); EQ(round_start_vault(-2), -1); EQ(round_start_vault(99), 99);
    EQ(energy_after_play(4, 2), 0); EQ(energy_after_play(4, 0), 3);
    EQ(energy_after_play(3, -1), 4); EQ(energy_after_play(6, -1), 6); EQ(energy_after_play(5, 43), 3);   /* Capacitor costs 2 */
    /* end conditions */
    EQ(match_decided(0, 5), 1); EQ(match_decided(5, 0), 1); EQ(match_decided(-3, 4), 1); EQ(match_decided(1, 1), 0);
    EQ(round_winner_by_hull(7, 3), 0); EQ(round_winner_by_hull(3, 7), 1); EQ(round_winner_by_hull(4, 4), 2);
    EQ(round_winner(4, 4, 5, 2), 0); EQ(round_winner(4, 4, 2, 5), 1); EQ(round_winner(4, 4, 3, 3), 2); EQ(round_winner(9, 4, 0, 50), 0);
    EQ(bankrupt(-6), 0); EQ(bankrupt(-7), 1); EQ(bankrupt(0), 0);
    /* Dark Pool (34) becomes Naked Short 35 / Flash Crash 21 / Pump & Dump 20 / Hostile Takeover 36 by roll quartile */
    EQ(card_substitute(34, 0), 35); EQ(card_substitute(34, 24), 35); EQ(card_substitute(34, 25), 21); EQ(card_substitute(34, 50), 20);
    EQ(card_substitute(34, 75), 36); EQ(card_substitute(34, 99), 36); EQ(card_substitute(9, 60), 9); EQ(card_substitute(-1, 60), -1);
    /* effect engine, hand-typed: fx_amount(word, dealt, taken, mk, ok, oc, mh, oh, el, oe, mv, ov, rnd, roll) */
    int W;
    W = card_fx_a(9);   /* Call Option: ENERGY +1 if it landed */
    EQ(fx_ch(W), 23); EQ(fx_amount(W, 4, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 0), 1); EQ(fx_amount(W, 0, 6, 0, 2, 2, 20, 20, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(12);  /* Rageblade: +1 per two rounds, only if landed */
    EQ(fx_ch(W), 1); EQ(fx_amount(W, 3, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 6, 0), 3); EQ(fx_amount(W, 0, 0, 0, 2, 2, 20, 20, 3, 3, 3, 3, 6, 0), 0);
    W = card_fx_a(13);  /* Kraken Slayer: +6 true damage on every 3rd round */
    EQ(fx_ch(W), 22); EQ(fx_amount(W, 5, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 3, 0), 6); EQ(fx_amount(W, 5, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 4, 0), 0);
    W = card_fx_a(16);  /* Death Mark: +8 if opp hull <= 10 */
    EQ(fx_amount(W, 2, 0, 0, 1, 2, 20, 10, 3, 3, 3, 3, 1, 0), 8); EQ(fx_amount(W, 2, 0, 0, 1, 2, 20, 11, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(18);  /* Short Squeeze: +1 per opp energy */
    EQ(fx_amount(W, 1, 0, 0, 1, 2, 20, 20, 3, 5, 3, 3, 1, 0), 5);
    W = card_fx_a(22); EQ(fx_amount(W, 0, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 49), 9); EQ(fx_amount(W, 0, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 50), 0);   /* Yolo Roll */
    W = card_fx_b(22); EQ(fx_amount(W, 0, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 50), 3); EQ(fx_amount(W, 0, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 49), 0);
    W = card_fx_a(32);  /* Thornmail: reflect half of damage taken (10 -> 5), only if hit */
    EQ(fx_ch(W), 6); EQ(fx_amount(W, 0, 10, 1, 0, 4, 20, 20, 3, 3, 3, 3, 1, 0), 5); EQ(fx_amount(W, 0, 0, 1, 2, 2, 20, 20, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(37);  /* Corporate Espionage: 2x the opponent's card cost, only if they played a card */
    EQ(fx_amount(W, 0, 0, 1, 0, 4, 20, 20, 3, 3, 3, 3, 1, 0), 8); EQ(fx_amount(W, 0, 0, 1, -1, 0, 20, 20, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(52);  /* Put Option: heal min(3, taken) */
    EQ(fx_amount(W, 0, 2, 2, 1, 2, 20, 20, 3, 3, 3, 3, 1, 0), 2); EQ(fx_amount(W, 0, 9, 2, 1, 2, 20, 20, 3, 3, 3, 3, 1, 0), 3); EQ(fx_amount(W, 0, 0, 2, 1, 2, 20, 20, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(53);  /* Stasis Frame: immune only at hull <= 6 */
    EQ(fx_ch(W), 4); EQ(fx_amount(W, 0, 5, 2, 1, 2, 6, 20, 3, 3, 3, 3, 1, 0), 1); EQ(fx_amount(W, 0, 5, 2, 1, 2, 7, 20, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(57);  /* Credit Default Swap: +2 energy when the opponent played a BURST (kind 0) */
    EQ(fx_amount(W, 0, 0, 2, 0, 3, 20, 20, 3, 3, 3, 3, 1, 0), 2); EQ(fx_amount(W, 0, 0, 2, 1, 3, 20, 20, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(66); EQ(fx_ch(W), 20); EQ(fx_amount(W, 0, 0, 2, 1, 2, 20, 20, 3, 3, 3, 3, 1, 0), 8);   /* Bailout Package: heal 8 */
    EQ(fx_ch(card_fx_b(66)), 43);                                                                       /* ... and erase debt */
    W = card_fx_a(36); EQ(fx_ch(W), 7); EQ(fx_ch(card_fx_b(36)), 8);                                     /* Hostile Takeover: cancel + copy */
    W = card_fx_a(32); EQ(fx_phase(fx_ch(W)), 1); EQ(fx_phase(23), 2);
    EQ(card_fx_a(34), 0); EQ(card_fx_a(30), 0);                                                          /* Dark Pool and Bribe carry no words */

    /* parity vectors must agree with the C build */
    FILE *f = fopen(vec, "r");
    if (!f) { printf("FAIL cannot open %s\n", vec); return 1; }
    char line[256]; int vlines = 0;
    while (fgets(line, sizeof line, f)) {
        char fn[64]; int a[16] = {0}, want, n = 0;
        char *eq = strstr(line, " = ");
        if (!eq) { fails++; printf("FAIL malformed vector: %s", line); continue; }
        want = atoi(eq + 3);
        *eq = 0;
        char *tok = strtok(line, " ");
        snprintf(fn, sizeof fn, "%s", tok ? tok : "");
        while ((tok = strtok(NULL, " ")) && n < 16) a[n++] = atoi(tok);
        int got;
        if (!strcmp(fn, "start_hull")) got = start_hull();
        else if (!strcmp(fn, "start_energy")) got = start_energy();
        else if (!strcmp(fn, "start_vault")) got = start_vault();
        else if (!strcmp(fn, "max_rounds")) got = max_rounds();
        else if (!strcmp(fn, "hand_size")) got = hand_size();
        else if (!strcmp(fn, "num_cards")) got = num_cards();
        else if (!strcmp(fn, "energy_cap")) got = energy_cap(a[0]);
        else if (!strcmp(fn, "card_kind")) got = card_kind(a[0]);
        else if (!strcmp(fn, "card_tier")) got = card_tier(a[0]);
        else if (!strcmp(fn, "card_cost")) got = card_cost(a[0]);
        else if (!strcmp(fn, "card_power")) got = card_power(a[0]);
        else if (!strcmp(fn, "card_credit")) got = card_credit(a[0]);
        else if (!strcmp(fn, "card_fx_a")) got = card_fx_a(a[0]);
        else if (!strcmp(fn, "card_fx_b")) got = card_fx_b(a[0]);
        else if (!strcmp(fn, "card_substitute")) got = card_substitute(a[0], a[1]);
        else if (!strcmp(fn, "kind_beats")) got = kind_beats(a[0], a[1]);
        else if (!strcmp(fn, "is_legal_play")) got = is_legal_play(a[0], a[1], a[2]);
        else if (!strcmp(fn, "damage_dealt")) got = damage_dealt(a[0], a[1]);
        else if (!strcmp(fn, "round_start_energy")) got = round_start_energy(a[0]);
        else if (!strcmp(fn, "round_start_vault")) got = round_start_vault(a[0]);
        else if (!strcmp(fn, "energy_after_play")) got = energy_after_play(a[0], a[1]);
        else if (!strcmp(fn, "round_winner_by_hull")) got = round_winner_by_hull(a[0], a[1]);
        else if (!strcmp(fn, "round_winner")) got = round_winner(a[0], a[1], a[2], a[3]);
        else if (!strcmp(fn, "match_decided")) got = match_decided(a[0], a[1]);
        else if (!strcmp(fn, "bankrupt")) got = bankrupt(a[0]);
        else if (!strcmp(fn, "fx_ch")) got = fx_ch(a[0]);
        else if (!strcmp(fn, "fx_phase")) got = fx_phase(a[0]);
        else if (!strcmp(fn, "fx_amount")) got = fx_amount(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], a[10], a[11], a[12], a[13]);
        else { fails++; printf("FAIL unknown vector fn %s\n", fn); continue; }
        checks++; vlines++;
        if (got != want) { fails++; printf("FAIL vector %s (%d args): got %d want %d\n", fn, n, got, want); }
    }
    fclose(f);
    if (vlines < 10000) { fails++; printf("FAIL only %d parity vectors read\n", vlines); }
    printf("test_rules: %d checks (%d parity vectors), %d failures\n", checks, vlines, fails);
    return fails ? 1 : 0;
}
