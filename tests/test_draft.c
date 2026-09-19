/* Draft deck-building + deck-driven matches (core/draft.c, dw_match_init_decks). */
#include <stdio.h>
#include <string.h>
#include "card_rules.h"
#include "draft.h"
#include "match.h"

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void draft_random(DwDraft *d, uint32_t r) {
    while (!dw_draft_done(d)) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        int idx = (int)(r % 2), mult = 1 + (int)((r >> 8) % 3);
        if (dw_draft_pick(d, idx, mult) != 0) { /* bucket full: try the others */
            for (mult = 1; mult <= 3 && dw_draft_pick(d, idx, mult) != 0; mult++) {}
        }
    }
}

int main(void) {
    for (uint32_t seed = 1; seed <= 300; seed++) {
        DwDraft d; dw_draft_init(&d, seed);
        CHECK(d.offer[0] >= 0 && d.offer[1] >= 0 && d.offer[0] != d.offer[1]);
        draft_random(&d, seed * 7919u);
        CHECK(d.deck_n == DW_DECK_SIZE);
        CHECK(d.left[0] == 0 && d.left[1] == 0 && d.left[2] == 0);
        int cnt[128] = {0}, ones = 0, twos = 0, threes = 0;
        for (int i = 0; i < d.deck_n; i++) { CHECK(d.deck[i] >= 0 && d.deck[i] < num_cards()); cnt[d.deck[i]]++; }
        for (int c = 0; c < num_cards(); c++) { if (cnt[c] == 1) ones++; else if (cnt[c] == 2) twos++; else if (cnt[c] == 3) threes++; else CHECK(cnt[c] == 0); }
        CHECK(ones == 10 && twos == 5 && threes == 1);
        CHECK(dw_draft_pick(&d, 0, 1) != 0);   /* nothing left to pick */
        DwDraft e; dw_draft_init(&e, seed); draft_random(&e, seed * 7919u);
        CHECK(memcmp(d.deck, e.deck, DW_DECK_SIZE) == 0);   /* deterministic */
    }
    { DwDraft d; dw_draft_init(&d, 5); CHECK(dw_draft_pick(&d, 2, 1) != 0); CHECK(dw_draft_pick(&d, 0, 0) != 0); CHECK(dw_draft_pick(&d, 0, 4) != 0);
      for (int i = 0; i < 1; i++) CHECK(dw_draft_pick(&d, 0, 3) == 0);
      CHECK(dw_draft_pick(&d, 0, 3) != 0);   /* only one 3-of bucket */ }
    /* a match played from two drafted decks only ever deals cards from those decks, and is deterministic */
    for (uint32_t seed = 1; seed <= 50; seed++) {
        DwDraft a, b; dw_draft_init(&a, seed); dw_draft_init(&b, seed + 1000); draft_random(&a, seed); draft_random(&b, seed + 5);
        DwMatch m, m2;
        dw_match_init_decks(&m, seed, a.deck, DW_DECK_SIZE, b.deck, DW_DECK_SIZE); dw_match_init_decks(&m2, seed, a.deck, DW_DECK_SIZE, b.deck, DW_DECK_SIZE);
        for (int r = 0; r < 30 && !m.done; r++) {
            dw_match_begin_round(&m); dw_match_begin_round(&m2);
            for (int s = 0; s < 2; s++) {
                /* Swap effects move cards between the seats, so a card in hand may come from either deck */
                for (int h = 0; h < DW_HAND; h++) if (m.hand[s][h] >= 0) { int in = 0; for (int i = 0; i < DW_DECK_SIZE; i++) if (a.deck[i] == m.hand[s][h] || b.deck[i] == m.hand[s][h]) in = 1; CHECK(in); }
            }
            int sl = r % 4; dw_match_lock(&m, 0, sl); dw_match_lock(&m, 1, -1); dw_match_lock(&m2, 0, sl); dw_match_lock(&m2, 1, -1);
            dw_match_resolve(&m, NULL); dw_match_resolve(&m2, NULL);
            CHECK(m.hull[0] == m2.hull[0] && m.hull[1] == m2.hull[1] && m.hand[0][0] == m2.hand[0][0]);
        }
        /* hand + draw + discard across both seats always account for exactly the 46 drafted cards (nothing lost or invented) */
        { int n = 0; for (int s = 0; s < 2; s++) { n += m.draw_n[s] + m.discard_n[s]; for (int h = 0; h < DW_HAND; h++) if (m.hand[s][h] >= 0) n++; } CHECK(n == 2 * DW_DECK_SIZE); }
    }
    printf("test_draft: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
