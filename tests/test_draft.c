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
    /* dw_draft_deck_valid (S510 DW_C_DRAFT_RESUME) -- a legitimately drafted deck is always
     * valid, and the obvious cheats (too many copies, wrong size, wrong bucket shape, out-of-
     * range card id) are all rejected. */
    for (uint32_t seed = 1; seed <= 20; seed++) {
        DwDraft d; dw_draft_init(&d, seed); draft_random(&d, seed * 7919u);
        CHECK(dw_draft_deck_valid(d.deck, d.deck_n));
    }
    {
        DwDraft d; dw_draft_init(&d, 1); draft_random(&d, 1);
        CHECK(!dw_draft_deck_valid(d.deck, DW_DECK_SIZE - 1));       /* wrong size */
        int8_t bad[DW_DECK_SIZE]; memcpy(bad, d.deck, DW_DECK_SIZE);
        bad[0] = bad[1];                                             /* same card as another 1-of -- breaks the bucket shape */
        CHECK(!dw_draft_deck_valid(bad, DW_DECK_SIZE));
        int8_t stacked[DW_DECK_SIZE]; for (int i = 0; i < DW_DECK_SIZE; i++) stacked[i] = 0;
        CHECK(!dw_draft_deck_valid(stacked, DW_DECK_SIZE));           /* 23 copies of one card */
        int8_t oob[DW_DECK_SIZE]; memcpy(oob, d.deck, DW_DECK_SIZE); oob[0] = (int8_t)num_cards();
        CHECK(!dw_draft_deck_valid(oob, DW_DECK_SIZE));                /* out-of-range card id */
    }
    /* constructed mode: dw_random_deck always yields a legal deck (16 distinct cards, 10/5/1), differs by seed, and a deck written
     * out as text parses back to itself; dw_deck_parse refuses everything that is not exactly a legal deck */
    {
        uint32_t rng = 12345; int8_t first[DW_DECK_SIZE]; int differ = 0;
        for (int i = 0; i < 2000; i++) {
            int8_t d[DW_DECK_SIZE]; dw_random_deck(&rng, d);
            CHECK(dw_draft_deck_valid(d, DW_DECK_SIZE));
            if (i == 0) memcpy(first, d, sizeof d); else if (memcmp(first, d, sizeof d) != 0) differ++;
        }
        CHECK(differ > 1900);
        uint32_t zero = 0; int8_t z[DW_DECK_SIZE]; dw_random_deck(&zero, z); CHECK(dw_draft_deck_valid(z, DW_DECK_SIZE));   /* a zero seed must not stick */
        /* every card of the catalog can show up in a random deck */
        int seen[DW_DRAFT_MAX_CARDS] = {0}; rng = 99;
        for (int i = 0; i < 4000; i++) { int8_t d[DW_DECK_SIZE]; dw_random_deck(&rng, d); for (int k = 0; k < DW_DECK_SIZE; k++) seen[d[k]] = 1; }
        for (int c = 0; c < num_cards(); c++) CHECK(seen[c]);
        int8_t d[DW_DECK_SIZE], back[DW_DECK_SIZE]; char txt[256], *w = txt; dw_random_deck(&rng, d);
        for (int i = 0; i < DW_DECK_SIZE; i++) w += sprintf(w, i ? ",%d" : "%d", d[i]);
        CHECK(dw_deck_parse(txt, back) && memcmp(d, back, sizeof d) == 0);
        for (char *c = txt; *c; c++) if (*c == ',') *c = ' ';
        CHECK(dw_deck_parse(txt, back) && memcmp(d, back, sizeof d) == 0);          /* spaces work too */
        CHECK(!dw_deck_parse("", back));
        CHECK(!dw_deck_parse("1,2,3", back));                                        /* too short */
        CHECK(!dw_deck_parse("a,b,c", back));                                        /* not numbers */
        CHECK(!dw_deck_parse("0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", back));   /* 23 copies of one card */
        CHECK(!dw_deck_parse("1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24", back));   /* too long */
        CHECK(!dw_deck_parse("9999999999,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1", back));   /* absurd id */
    }
    printf("test_draft: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
