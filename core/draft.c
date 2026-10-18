#include "draft.h"
#include "card_rules.h"
#include <stdlib.h>
#include <string.h>

static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }

static void make_offer(DwDraft *d) {
    if (d->pick_no >= DW_DRAFT_PICKS) { d->offer[0] = d->offer[1] = -1; return; }
    int n = num_cards();
    for (int k = 0; k < 2; k++) {
        int c;
        do { c = (int)(xs(&d->rng) % (uint32_t)n); } while (d->used[c] || (k == 1 && c == d->offer[0]));
        d->offer[k] = (int8_t)c;
    }
}

void dw_draft_init(DwDraft *d, uint32_t seed) {
    memset(d, 0, sizeof *d);
    d->rng = seed * 2654435761u ^ 0xD1B54A35u; if (!d->rng) d->rng = 1;
    for (int i = 0; i < 4; i++) xs(&d->rng);
    d->left[0] = 10; d->left[1] = 5; d->left[2] = 1;
    make_offer(d);
}

int dw_draft_pick(DwDraft *d, int idx, int mult) {
    if (d->pick_no >= DW_DRAFT_PICKS || idx < 0 || idx > 1 || mult < 1 || mult > 3 || !d->left[mult - 1]) return 1;
    int c = d->offer[idx];
    d->used[c] = 1; d->left[mult - 1]--;
    for (int i = 0; i < mult; i++) d->deck[d->deck_n++] = (int8_t)c;
    d->pick_no++;
    make_offer(d);
    return 0;
}

int dw_draft_done(const DwDraft *d) { return d->pick_no >= DW_DRAFT_PICKS; }

/* Real draft legality check for a deck arriving from OUTSIDE the normal pick-by-pick flow (S510
 * DW_C_DRAFT_RESUME) -- a client resuming a persisted deck could otherwise hand the server any 23
 * bytes it likes (e.g. 23 copies of the best card), bypassing the bucket rule dw_draft_pick above
 * enforces one pick at a time. Encodes the same rule: exactly ten 1-ofs, five 2-ofs, one 3-of. */
int dw_draft_deck_valid(const int8_t *deck, int n) {
    if (n != DW_DECK_SIZE) return 0;
    int seen[DW_DRAFT_MAX_CARDS]; memset(seen, 0, sizeof seen);
    int nc = num_cards();
    for (int i = 0; i < n; i++) {
        if (deck[i] < 0 || deck[i] >= nc) return 0;
        seen[deck[i]]++;
    }
    int ones = 0, twos = 0, threes = 0;
    for (int c = 0; c < DW_DRAFT_MAX_CARDS; c++) {
        switch (seen[c]) {
        case 0: break;
        case 1: ones++; break;
        case 2: twos++; break;
        case 3: threes++; break;
        default: return 0; /* more than 3 copies is never legal */
        }
    }
    return ones == 10 && twos == 5 && threes == 1;
}

void dw_random_deck(uint32_t *rng, int8_t out[DW_DECK_SIZE]) {
    int n = num_cards(), idx[DW_DRAFT_MAX_CARDS];
    if (!*rng) *rng = 1;                                  /* xorshift never leaves 0 */
    if (n > DW_DRAFT_MAX_CARDS) n = DW_DRAFT_MAX_CARDS;
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int i = 0; i < DW_DRAFT_PICKS; i++) {            /* partial Fisher-Yates: the first 16 are 16 distinct cards */
        int j = i + (int)(xs(rng) % (uint32_t)(n - i)); int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
    int k = 0;
    for (int i = 0; i < DW_DRAFT_PICKS; i++) {
        int copies = i < 10 ? 1 : i < 15 ? 2 : 3;
        for (int c = 0; c < copies; c++) out[k++] = (int8_t)idx[i];
    }
}

int dw_deck_parse(const char *text, int8_t out[DW_DECK_SIZE]) {
    int n = 0; const char *p = text;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p < '0' || *p > '9') return 0;
        long v = 0; while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); if (v > 1000) return 0; p++; }
        if (n >= DW_DECK_SIZE) return 0;
        out[n++] = (int8_t)(v > 127 ? -1 : v);
    }
    return n == DW_DECK_SIZE && dw_draft_deck_valid(out, n);
}
