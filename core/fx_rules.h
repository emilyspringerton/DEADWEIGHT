/* Declarations for the PARENA-generated round-resolution animation/audio DECISION layer
 * (core/fx_rules.c, generated from PARENA/stdlib/deadweight/fx_rules.prn -- do not edit the .c
 * by hand). This is the exact scenario/timeline/cue logic apps/gui/fx.c's fx_begin() used to hand
 * -derive; web/src/generated/FxRules.ts is the same source compiled to TypeScript, so the browser
 * client makes the identical decisions. See docs/ANIMATION_AND_AUDIO.md for what each scenario id
 * (0-10) and cue id (0-16, matching sfx.h's SfxCue enum order) means. */
#ifndef DW_FX_RULES_H
#define DW_FX_RULES_H

int fx_card_id(int eff, int declared, int cancelled);
int fx_kind_of(int id);
int fx_scenario(int kind_a, int kind_b, int cancelled_a, int cancelled_b);
int fx_winner_seat(int kind_a, int kind_b, int cancelled_a, int cancelled_b);
int fx_is_crit(int scenario, int dmg_lose, int power_win, int power_lose, int cost_win);
int fx_clash_duration_ms(int scenario, int crit);
int fx_clash_cue(int scenario, int crit, int kw);

int fx_clash0(void);
int fx_clash1(int clash_dur);
int fx_hull0(int clash_dur);
int fx_hull1(int clash_dur, int has_hull);
int fx_armor0(int clash_dur, int has_hull);
int fx_armor1(int clash_dur, int has_hull, int has_armor);
int fx_econ0(int clash_dur, int has_hull, int has_armor);
int fx_econ1(int clash_dur, int has_hull, int has_armor, int has_econ);
int fx_stat0(int clash_dur, int has_hull, int has_armor, int has_econ);
int fx_stat1(int clash_dur, int has_hull, int has_armor, int has_econ, int has_stat);
int fx_settle0(int clash_dur, int has_hull, int has_armor, int has_econ, int has_stat);
int fx_timeline_total_ms(int clash_dur, int has_hull, int has_armor, int has_econ, int has_stat);

int fx_has_hull(int dmg_you, int dmg_opp, int heal_you, int heal_opp);
int fx_has_armor(int armor_before_you, int armor_after_you, int armor_before_opp, int armor_after_opp);
int fx_has_econ(int energy_delta_you, int energy_delta_opp, int vault_delta_you, int vault_delta_opp);
int fx_has_stat(int new_status_you, int new_status_opp, int disabled_you, int disabled_opp, int swapped);

#endif
