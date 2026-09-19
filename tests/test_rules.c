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
    EQ(start_hull(), 20); EQ(start_energy(), 2); EQ(start_vault(), 3); EQ(max_rounds(), 100); EQ(hand_size(), 4); EQ(num_cards(), 105);
    /* base catalog: id = kind*3+tier ; cost 1/2/4 ; power 3/6/10 ; kinds 0 Offense (Red), 1 Operations (Yellow), 2 Defense (Blue) */
    int cost[3] = {1, 2, 4}, power[3] = {3, 6, 10};
    for (int id = 0; id < 9; id++) {
        EQ(card_kind(id), id / 3); EQ(card_tier(id), id % 3);
        EQ(card_cost(id), cost[id % 3]); EQ(card_power(id), power[id % 3]); EQ(card_credit(id), 0);
        EQ(card_fx_b(id), 0);
        if (id >= 3 && id <= 5) { EQ(fx_ch(card_fx_a(id)), 1); EQ(card_keyword(id), 3); }   /* base Operations line: Flank, +1/+2/+3 vs Defense */
        else { EQ(card_fx_a(id), 0); EQ(card_keyword(id), 0); }                             /* base Offense/Defense lines are vanilla */
    }
    { int w = card_fx_a(4);   /* Interceptor: Flank +2 only against a Defense card (kind 2) */
      EQ(fx_amount(w, 6, 0, 1, 2, 2, 20, 20, 3, 3, 3, 3, 1, 0), 2); EQ(fx_amount(w, 6, 0, 1, 0, 2, 20, 20, 3, 3, 3, 3, 1, 0), 0); EQ(fx_amount(w, 6, 0, 1, -1, 0, 20, 20, 3, 3, 3, 3, 1, 0), 0); }
    /* Kinds, hand-typed from docs/CARD_MODE_RULES.md. The three colours are interleaved by id: 35 cards each. */
    static const int OFFENSE[] = {0, 1, 2, 9, 10, 12, 13, 14, 17, 20, 22, 24, 27, 28, 30, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104};
    static const int DEFENSE[] = {6, 7, 8, 31, 32, 33, 35, 38, 39, 41, 42, 43, 44, 45, 47, 49, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 68, 69, 71, 84};
    static const int LOCK[] = {11, 25, 73, 74, 75, 76, 77}, SABOTAGE[] = {15, 34, 36, 40, 48, 70, 82}, FLANK[] = {3, 4, 5, 23, 78, 79, 80},
                     SCAN[] = {16, 18, 19, 26, 37, 51, 81}, SIPHON[] = {21, 29, 46, 50, 67, 72, 83};
    static const int *KW[5] = {LOCK, SABOTAGE, FLANK, SCAN, SIPHON};
    int seen[105] = {0};
    for (unsigned i = 0; i < sizeof OFFENSE / sizeof *OFFENSE; i++) { EQ(card_kind(OFFENSE[i]), 0); EQ(card_keyword(OFFENSE[i]), 0); seen[OFFENSE[i]]++; }
    for (unsigned i = 0; i < sizeof DEFENSE / sizeof *DEFENSE; i++) { EQ(card_kind(DEFENSE[i]), 2); EQ(card_keyword(DEFENSE[i]), 0); seen[DEFENSE[i]]++; }
    for (int k = 0; k < 5; k++) for (int i = 0; i < 7; i++) { EQ(card_kind(KW[k][i]), 1); EQ(card_keyword(KW[k][i]), k + 1); seen[KW[k][i]]++; }
    EQ((int)(sizeof OFFENSE / sizeof *OFFENSE), 35); EQ((int)(sizeof DEFENSE / sizeof *DEFENSE), 35);
    for (int id = 0; id < 105; id++) EQ(seen[id], 1);            /* every card in exactly one colour, and every Operations card has exactly one keyword */
    EQ(card_keyword(-1), 0);
    EQ(card_cost(28), 2); EQ(card_power(28), 6);          /* Iron Dwarf: 2 energy, 6 power (+2 rider) */
    EQ(card_cost(50), 6); EQ(card_cost(36), 5); EQ(card_cost(17), 5); EQ(card_power(17), 14);   /* Total Blackout, Hostile Takeover, Final Spark */
    EQ(card_cost(93), 6); EQ(card_power(93), 14); EQ(card_cost(77), 5); EQ(card_power(77), 9); EQ(card_cost(73), 1); EQ(card_power(73), 3);   /* Extinction Salvo, Sniper Uplink, Targeting Laser */
    EQ(card_cost(104), 2); EQ(card_power(104), 5); EQ(card_cost(84), 3); EQ(card_power(84), 4); EQ(card_cost(80), 1); EQ(card_power(80), 1);   /* Fusion Torch, Point Defense Grid, Drift Strike */
    EQ(card_cost(41), 1); EQ(card_power(41), 3);          /* Quartermaster */
    EQ(card_credit(13), 2); EQ(card_credit(30), 2); EQ(card_credit(51), 3); EQ(card_credit(59), 2); EQ(card_credit(100), 3); EQ(card_credit(28), 0); EQ(card_credit(104), 0);
    EQ(card_tier(9), 1); EQ(card_tier(10), 0); EQ(card_tier(17), 2);   /* Guild tier = cost band: <=1 low, <=3 mid, else high */
    /* triangle OFFENSE(0) > OPERATIONS(1) > DEFENSE(2) > OFFENSE */
    EQ(kind_beats(0, 1), 1); EQ(kind_beats(1, 2), 1); EQ(kind_beats(2, 0), 1);
    EQ(kind_beats(1, 0), 0); EQ(kind_beats(2, 1), 0); EQ(kind_beats(0, 2), 0);
    EQ(kind_beats(0, 0), 0); EQ(kind_beats(1, 1), 0); EQ(kind_beats(2, 2), 0);
    /* damage examples (rules doc): winner deals power, loser 0 */
    EQ(damage_dealt(2, 3), 10);   /* offense t2 beats operations t0 */
    EQ(damage_dealt(3, 2), 0);    /* countered */
    EQ(damage_dealt(5, 6), 10);   /* operations t2 beats defense t0 */
    EQ(damage_dealt(6, 0), 3);    /* defense t0 counters offense t0: returns its power */
    EQ(damage_dealt(0, 6), 0);
    EQ(damage_dealt(2, 1), 10);   /* offense vs offense: both full power */
    EQ(damage_dealt(1, 2), 6);
    EQ(damage_dealt(5, 4), 5);    /* operations vs operations: power/2 */
    EQ(damage_dealt(3, 3), 1);
    EQ(damage_dealt(8, 7), 0);    /* defense vs defense */
    EQ(damage_dealt(2, -1), 10);  /* vs pass: offense/operations unopposed */
    EQ(damage_dealt(5, -1), 10);
    EQ(damage_dealt(8, -1), 0);   /* defense vs pass deals nothing */
    EQ(damage_dealt(-1, 2), 0);   /* pass deals nothing */
    EQ(damage_dealt(-1, -1), 0);
    EQ(damage_dealt(28, 16), 6);  /* Iron Dwarf (offense 6) vs Death Mark (operations): offense wins */
    EQ(damage_dealt(28, 31), 0);  /* ... but Yield Contract is Defense now, and Defense counters Offense (its 2 power returns: see next line) */
    EQ(damage_dealt(31, 28), 2);  /* Yield Contract (defense 2) counters Iron Dwarf */
    EQ(damage_dealt(52, 28), 4);  /* Put Option (defense 4) counters Iron Dwarf */
    EQ(damage_dealt(25, 6), 5);   /* Piercing Round (Lock, operations 5) beats Buckler (defense): the Lock line shatters defenses */
    EQ(damage_dealt(28, 52), 0);
    /* legality: energy AND credits */
    EQ(is_legal_play(2, 4, 0), 1); EQ(is_legal_play(2, 3, 0), 0); EQ(is_legal_play(0, 1, 0), 1); EQ(is_legal_play(0, 0, 0), 0);
    EQ(is_legal_play(-1, 6, 9), 0); EQ(is_legal_play(105, 8, 9), 0); EQ(is_legal_play(104, 6, 0), 1); EQ(is_legal_play(72, 6, 0), 1);
    EQ(is_legal_play(100, 5, 2), 0); EQ(is_legal_play(100, 5, 3), 1); EQ(is_legal_play(100, 4, 9), 0);   /* Doomsday Warhead: 5 energy + 3 credits */
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
    W = card_fx_a(57);  /* Credit Default Swap: +2 energy when the opponent played an Offense card (kind 0) */
    EQ(fx_amount(W, 0, 0, 2, 0, 3, 20, 20, 3, 3, 3, 3, 1, 0), 2); EQ(fx_amount(W, 0, 0, 2, 1, 3, 20, 20, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(66); EQ(fx_ch(W), 20); EQ(fx_amount(W, 0, 0, 2, 1, 2, 20, 20, 3, 3, 3, 3, 1, 0), 8);   /* Bailout Package: heal 8 */
    EQ(fx_ch(card_fx_b(66)), 43);                                                                       /* ... and erase debt */
    W = card_fx_a(36); EQ(fx_ch(W), 7); EQ(fx_ch(card_fx_b(36)), 8);                                     /* Hostile Takeover: cancel + copy */
    W = card_fx_a(32); EQ(fx_phase(fx_ch(W)), 1); EQ(fx_phase(23), 2);
    EQ(card_fx_a(34), 0); EQ(card_fx_a(30), 0);                                                          /* Dark Pool and Bribe carry no words */

    /* new cards, hand-typed */
    W = card_fx_a(74);  /* Bunker Buster (Lock): +4 against a Defense card */
    EQ(fx_ch(W), 1); EQ(fx_amount(W, 6, 0, 1, 2, 2, 20, 20, 3, 3, 3, 3, 1, 0), 4); EQ(fx_amount(W, 6, 0, 1, 0, 2, 20, 20, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(75); EQ(fx_ch(W), 2); EQ(fx_ch(card_fx_b(75)), 3);    /* Phase Lance: pierce + bypass */
    W = card_fx_a(77); EQ(fx_ch(W), 2); W = card_fx_b(77);              /* Sniper Uplink: pierce, +5 if opp hull <= 10 */
    EQ(fx_amount(W, 9, 0, 1, 2, 2, 20, 10, 3, 3, 3, 3, 1, 0), 5); EQ(fx_amount(W, 9, 0, 1, 2, 2, 20, 11, 3, 3, 3, 3, 1, 0), 0);
    W = card_fx_a(78); EQ(fx_amount(W, 3, 0, 1, -1, 0, 20, 20, 3, 3, 3, 3, 1, 0), 5); EQ(fx_amount(W, 3, 0, 1, 0, 2, 20, 20, 3, 3, 3, 3, 1, 0), 0);   /* Blindside: +5 if passed */
    W = card_fx_b(78); EQ(fx_amount(W, 3, 0, 1, 2, 2, 20, 20, 3, 3, 3, 3, 1, 0), 2);                                                                       /* ... +2 vs Defense */
    W = card_fx_a(81); EQ(fx_amount(W, 1, 0, 1, 0, 4, 20, 20, 3, 3, 3, 3, 1, 0), 8); EQ(fx_amount(W, 1, 0, 1, -1, 0, 20, 20, 3, 3, 3, 3, 1, 0), 0);      /* Threat Assessment: 2x their cost */
    W = card_fx_a(82); EQ(fx_ch(W), 32); EQ(fx_amount(W, 2, 0, 1, 2, 2, 20, 20, 3, 3, 3, 3, 1, 0), 50); EQ(fx_amount(W, 0, 2, 1, 2, 2, 20, 20, 3, 3, 3, 3, 1, 0), 0);   /* Logic Bomb: burn 3 x 2 rounds if it lands */
    W = card_fx_a(83); EQ(fx_ch(W), 24); EQ(fx_ch(card_fx_b(83)), 23); EQ(fx_amount(W, 2, 0, 1, 2, 2, 20, 20, 3, 3, 3, 3, 1, 0), 2);                      /* Power Tap: opp -2 energy, me +2 */
    W = card_fx_a(84); EQ(fx_ch(W), 6); EQ(fx_amount(W, 0, 10, 2, 0, 3, 20, 20, 3, 3, 3, 3, 1, 0), 3); EQ(fx_ch(card_fx_b(84)), 28);                  /* Point Defense Grid: reflect 30%, +3 armor */
    W = card_fx_a(85); EQ(fx_amount(W, 4, 0, 0, 1, 2, 20, 20, 5, 3, 3, 3, 1, 0), 5);                                                                    /* Missile Salvo: +1 per energy left */
    W = card_fx_a(94); EQ(fx_amount(W, 3, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 39), 7); EQ(fx_amount(W, 3, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 40), 0);   /* Ricochet: 40% +7 ... */
    W = card_fx_b(94); EQ(fx_amount(W, 3, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 40), 1); EQ(fx_amount(W, 3, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 39), 0);   /* ... else +1 */
    W = card_fx_a(95); EQ(fx_amount(W, 5, 0, 0, 1, 2, 20, 20, 3, 3, 8, 3, 1, 0), 4); EQ(fx_amount(W, 5, 0, 0, 1, 2, 20, 20, 3, 3, -2, 3, 1, 0), 0);   /* Sponsored Barrage: +1 per 2 credits held */
    W = card_fx_a(97); EQ(fx_amount(W, 3, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 4, 0), 4); EQ(fx_amount(W, 3, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 3, 0), 0);   /* Gatling Rounds: every 2nd round */
    W = card_fx_a(99); EQ(fx_amount(W, 8, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 0), 4); EQ(fx_amount(W, 8, 0, 0, 2, 2, 20, 20, 3, 3, 3, 3, 1, 0), 0);   /* Tungsten Rod: +4 vs Operations */
    W = card_fx_a(102); EQ(fx_amount(W, 1, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 49), 5); EQ(fx_amount(W, 1, 0, 0, 1, 2, 20, 20, 3, 3, 3, 3, 1, 50), 0);   /* Dud Round: 50% +5 */

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
        else if (!strcmp(fn, "card_keyword")) got = card_keyword(a[0]);
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
