/* Deterministic card-mode match state machine (docs/CARD_MODE_RULES.md). All numbers and conditions come from the
 * PARENA-generated card_rules; this file only owns state (seeded shuffles, hands, piles, meters, statuses) and the
 * fixed resolution ORDER. A match is fully replayable from (seed, each round's two locked slots): every random draw
 * (shuffles, per-round rolls, random targets) comes from streams seeded by the match seed. */
#ifndef DW_MATCH_H
#define DW_MATCH_H
#include <stdint.h>

#define DW_HAND 4
#define DW_DECK 128               /* capacity; random mode deals the whole catalog, num_cards() must stay <= this */
#define DW_ARMOR_MAX 12

/* effect channels (must match the table at the top of PARENA/stdlib/deadweight/card_rules.prn) */
enum {
    DW_CH_BONUS = 1, DW_CH_PIERCE, DW_CH_BYPASS, DW_CH_IMMUNE, DW_CH_LIFELINE, DW_CH_REFLECT, DW_CH_CANCEL, DW_CH_COPY, DW_CH_HALVE,
    DW_CH_HEAL = 20, DW_CH_SELFDMG, DW_CH_TRUE, DW_CH_ENERGY, DW_CH_OENERGY, DW_CH_NEXTE, DW_CH_NEXTL, DW_CH_ONEXTL, DW_CH_ARMOR,
    DW_CH_VAULT, DW_CH_SPEND, DW_CH_OSPEND, DW_CH_BURN, DW_CH_REGEN, DW_CH_DISABLE, DW_CH_BLIND, DW_CH_SWAP, DW_CH_REDRAW,
    DW_CH_DISCARD, DW_CH_EQUALIZE, DW_CH_CHRONO, DW_CH_CONVERT, DW_CH_CLEANSE, DW_CH_DEBTFREE
};
/* ROUND_RESULT per-seat flag bits */
enum { DW_FX_CANCELLED = 1, DW_FX_IMMUNE = 2, DW_FX_LIFELINE = 4, DW_FX_LOCKED_OPP = 8, DW_FX_SWAPPED = 16,
       DW_FX_DISCARDED_OPP = 32, DW_FX_REDREW = 64, DW_FX_COPIED = 128 };
/* status bits shown to clients */
enum { DW_ST_BURN = 1, DW_ST_REGEN = 2, DW_ST_HIDDEN = 4 };

typedef struct {
    uint32_t seed;
    uint32_t rng[2];                  /* shuffle stream per seat */
    uint32_t rfx[2];                  /* rolls + random targets per seat (separate so decks don't depend on effects) */
    int8_t hull[2];
    uint8_t energy[2], armor[2];
    int8_t vault[2];
    int8_t hull_start[2], hull_prev[2];   /* hull at the start of this / the previous round (Chronobreak) */
    uint8_t burn_amt[2], burn_left[2], regen_amt[2], regen_left[2];
    uint8_t hidden[2];                /* rounds left that this seat's energy/armor/credits are hidden from the opponent */
    uint8_t lock_mask[2], next_lock[2];   /* hand slots unplayable this round / next round */
    int8_t next_e[2];                 /* pending energy delta applied at the next round start */
    uint8_t roll[2];                  /* this round's 0..99 roll per seat */
    int8_t hand[2][DW_HAND];          /* card ids, -1 = empty slot */
    int8_t draw[2][DW_DECK]; int draw_n[2];       /* top of pile = draw[s][draw_n-1] */
    int8_t discard[2][DW_DECK]; int discard_n[2];
    int round;                        /* 0 = not started, 1..max_rounds */
    int locked[2];                    /* 0/1 */
    int8_t lock_slot[2];              /* -1 pass, else hand slot */
    int done;                         /* 1 once decided */
    uint8_t result[2];                /* DW_RES_* per seat, valid when done */
    uint8_t reason;                   /* DW_END_* , valid when done */
} DwMatch;

typedef struct {
    int8_t card[2];                   /* card id declared by each seat, -1 = pass */
    int8_t eff[2];                    /* card actually resolved (Dark Pool result / copied card); -1 = pass or cancelled */
    uint8_t dmg_to[2];                /* total hull lost by each seat this round, all sources */
    uint8_t heal[2];                  /* hull healed by each seat this round */
    uint8_t roll[2];
    uint8_t flags[2];                 /* DW_FX_* */
} DwOutcome;

/* What one seat may see: own hand, both hulls, meters (opponent's energy/armor/credits are sentinel when hidden). */
typedef struct {
    int round; int8_t hull_you, hull_opp; uint8_t energy_you, energy_opp, armor_you, armor_opp; int8_t vault_you, vault_opp;
    int8_t hand[DW_HAND]; uint8_t opp_hand_size, lock_mask, status_you, status_opp;
} DwView;

void dw_match_init(DwMatch *m, uint32_t seed);      /* shuffles, deals 4 each, round = 0 */
/* Same, but each seat plays the given deck (card ids, n <= DW_DECK) instead of the whole catalog (draft mode). */
void dw_match_init_decks(DwMatch *m, uint32_t seed, const int8_t *deck0, int n0, const int8_t *deck1, int n1);
void dw_match_begin_round(DwMatch *m);              /* round++, +2 energy (cap), +1 credit, pending deltas, rolls, clear locks */
/* Lock an action. Returns 0 ok, else a DW_REJ_* reason. slot -1 = pass. */
int dw_match_lock(DwMatch *m, int seat, int slot);
int dw_match_both_locked(const DwMatch *m);
/* Resolve the round; sets m->done/result/reason if the match ended. */
void dw_match_resolve(DwMatch *m, DwOutcome *out);
void dw_match_forfeit(DwMatch *m, int loser_seat);  /* opponent wins, reason FORFEIT */
void dw_match_view(const DwMatch *m, int seat, DwView *v);
/* Post-resolution meters as `seat` may see them (opponent's hidden ones are sentinel). */
typedef struct { uint8_t armor_you, armor_opp; int8_t vault_you, vault_opp; } DwPostView;
void dw_match_post_view(const DwMatch *m, int seat, DwPostView *v);
#endif
