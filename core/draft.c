#include "draft.h"
#include "card_rules.h"
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
