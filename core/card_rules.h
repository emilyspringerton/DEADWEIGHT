/* Declarations for the PARENA-generated card rules (core/card_rules.c, generated from
 * PARENA/stdlib/deadweight/card_rules.prn -- do not edit the .c by hand). */
#ifndef DW_CARD_RULES_H
#define DW_CARD_RULES_H
int start_hull(void);
int start_energy(void);
int start_vault(void);
int max_rounds(void);
int hand_size(void);
int num_cards(void);
int energy_cap(int);
int card_kind(int);
int card_tier(int);
int card_cost(int);
int card_power(int);
int card_credit(int);
int kind_beats(int, int);
int is_legal_play(int, int, int);
int damage_dealt(int, int);
int round_start_energy(int);
int round_start_vault(int);
int energy_after_play(int, int);
int round_winner_by_hull(int, int);
int round_winner(int, int, int, int);
int match_decided(int, int);
int bankrupt(int);
int card_substitute(int, int);
int card_fx_a(int);
int card_fx_b(int);
int fx_ch(int);
int fx_phase(int);
int fx_amount(int, int, int, int, int, int, int, int, int, int, int, int, int, int);
#endif
