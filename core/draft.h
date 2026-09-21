/* Draft mode deck building (docs/CARD_MODE_RULES.md "Draft"). Host-owned (not PARENA): each pick offers 2 cards from the
 * catalog; the drafter takes one and chooses a copy count from three buckets: ten 1-ofs, five 2-ofs, one 3-of = 16 picks
 * = a 23-card deck. Offers are a deterministic function of the seed, and a card already taken is never offered again. */
#ifndef DW_DRAFT_H
#define DW_DRAFT_H
#include <stdint.h>

#define DW_DRAFT_PICKS 16
#define DW_DECK_SIZE 23
#define DW_DRAFT_MAX_CARDS 128

typedef struct {
    uint32_t rng;
    uint8_t pick_no;                       /* 0..DW_DRAFT_PICKS; == DW_DRAFT_PICKS when done */
    uint8_t left[3];                       /* buckets remaining: [0] 1-ofs, [1] 2-ofs, [2] 3-ofs */
    int8_t offer[2];                       /* current offer, -1 when done */
    uint8_t used[DW_DRAFT_MAX_CARDS];
    int8_t deck[DW_DECK_SIZE]; uint8_t deck_n;
} DwDraft;

void dw_draft_init(DwDraft *d, uint32_t seed);
/* Take offer[idx] (0/1) with `mult` copies (1..3). 0 = ok; nonzero = invalid (idx, mult, or that bucket is full). */
int dw_draft_pick(DwDraft *d, int idx, int mult);
int dw_draft_done(const DwDraft *d);
/* 1 iff deck[0..n) is a legal drafted deck (exactly DW_DECK_SIZE cards, ten 1-ofs/five 2-ofs/one
 * 3-of, all valid card ids) -- for validating a deck arriving OUTSIDE the normal pick-by-pick
 * flow (S510 DW_C_DRAFT_RESUME). */
int dw_draft_deck_valid(const int8_t *deck, int n);
#endif
