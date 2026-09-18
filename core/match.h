/* Deterministic card-mode match state machine (docs/CARD_MODE_RULES.md). All numbers come from the
 * PARENA-generated card_rules; this file only owns state: seeded shuffles, hands, piles, locks, resolution.
 * A match is fully replayable from (seed, each round's two locked slots). */
#ifndef DW_MATCH_H
#define DW_MATCH_H
#include <stdint.h>

#define DW_HAND 4
#define DW_DECK 9

typedef struct {
    uint32_t seed;
    uint32_t rng[2];
    int8_t hull[2];
    uint8_t energy[2];
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
    int8_t card[2];                   /* card id played by each seat, -1 = pass */
    uint8_t dmg_to[2];                /* damage taken by each seat */
} DwOutcome;

/* What one seat may see: own hand, both hulls/energies, opponent hand SIZE only. */
typedef struct {
    int round; int8_t hull_you, hull_opp; uint8_t energy_you, energy_opp;
    int8_t hand[DW_HAND]; uint8_t opp_hand_size;
} DwView;

void dw_match_init(DwMatch *m, uint32_t seed);      /* shuffles, deals 4 each, round = 0 */
void dw_match_begin_round(DwMatch *m);              /* round++, +2 energy (cap), clear locks */
/* Lock an action. Returns 0 ok, else a DW_REJ_* reason. slot -1 = pass. */
int dw_match_lock(DwMatch *m, int seat, int slot);
int dw_match_both_locked(const DwMatch *m);
/* Resolve the round; sets m->done/result/reason if the match ended. */
void dw_match_resolve(DwMatch *m, DwOutcome *out);
void dw_match_forfeit(DwMatch *m, int loser_seat);  /* opponent wins, reason FORFEIT */
void dw_match_view(const DwMatch *m, int seat, DwView *v);
#endif
