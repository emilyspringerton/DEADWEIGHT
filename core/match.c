#include "match.h"
#include "card_rules.h"
#include "protocol.h"
#include <string.h>

static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x; }

static void shuffle_pile(DwMatch *m, int s, int8_t *a, int n) {
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(xs(&m->rng[s]) % (uint32_t)(i + 1));
        int8_t t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

/* Draw the top card for seat s; reshuffle the discard into the draw pile when empty. -1 if no cards remain. */
static int8_t draw_card(DwMatch *m, int s) {
    if (m->draw_n[s] == 0 && m->discard_n[s] > 0) {
        memcpy(m->draw[s], m->discard[s], (size_t)m->discard_n[s]);
        m->draw_n[s] = m->discard_n[s]; m->discard_n[s] = 0;
        shuffle_pile(m, s, m->draw[s], m->draw_n[s]);
    }
    return m->draw_n[s] ? m->draw[s][--m->draw_n[s]] : (int8_t)-1;
}

void dw_match_init(DwMatch *m, uint32_t seed) {
    memset(m, 0, sizeof *m);
    m->seed = seed;
    for (int s = 0; s < 2; s++) {
        uint32_t r = seed * 2654435761u ^ (uint32_t)(s + 1) * 0x9E3779B9u;
        if (r == 0) r = 1;
        m->rng[s] = r;
        for (int i = 0; i < 4; i++) xs(&m->rng[s]);
        for (int i = 0; i < DW_DECK; i++) m->draw[s][i] = (int8_t)i;
        m->draw_n[s] = num_cards();
        shuffle_pile(m, s, m->draw[s], m->draw_n[s]);
        for (int i = 0; i < hand_size(); i++) m->hand[s][i] = draw_card(m, s);
        m->hull[s] = (int8_t)start_hull();
        m->energy[s] = (uint8_t)start_energy();
        m->lock_slot[s] = -1;
    }
}

void dw_match_begin_round(DwMatch *m) {
    m->round++;
    for (int s = 0; s < 2; s++) {
        m->energy[s] = (uint8_t)round_start_energy(m->energy[s]);
        m->locked[s] = 0; m->lock_slot[s] = -1;
    }
}

int dw_match_lock(DwMatch *m, int seat, int slot) {
    if (m->done || m->round < 1) return DW_REJ_NO_MATCH;
    if (m->locked[seat]) return DW_REJ_ALREADY_LOCKED;
    if (slot < -1 || slot >= DW_HAND) return DW_REJ_BAD_SLOT;
    if (slot >= 0) {
        int id = m->hand[seat][slot];
        if (id < 0) return DW_REJ_BAD_SLOT;
        if (!is_legal_play(id, m->energy[seat])) return DW_REJ_ILLEGAL_CARD;
    }
    m->locked[seat] = 1; m->lock_slot[seat] = (int8_t)slot;
    return 0;
}

int dw_match_both_locked(const DwMatch *m) { return m->locked[0] && m->locked[1]; }

void dw_match_resolve(DwMatch *m, DwOutcome *out) {
    DwOutcome o; memset(&o, 0, sizeof o);
    int card[2];
    for (int s = 0; s < 2; s++) card[s] = m->lock_slot[s] >= 0 ? m->hand[s][m->lock_slot[s]] : -1;
    int d0 = damage_dealt(card[0], card[1]);   /* seat 0 hurts seat 1 */
    int d1 = damage_dealt(card[1], card[0]);
    for (int s = 0; s < 2; s++) {
        o.card[s] = (int8_t)card[s];
        m->energy[s] = (uint8_t)energy_after_play(m->energy[s], card[s]);
        if (card[s] >= 0) {
            m->discard[s][m->discard_n[s]++] = (int8_t)card[s];
            m->hand[s][m->lock_slot[s]] = draw_card(m, s);
        }
    }
    o.dmg_to[1] = (uint8_t)d0; o.dmg_to[0] = (uint8_t)d1;
    m->hull[1] = (int8_t)(m->hull[1] - d0);
    m->hull[0] = (int8_t)(m->hull[0] - d1);
    if (match_decided(m->hull[0], m->hull[1])) {
        m->done = 1; m->reason = DW_END_HULL;
        if (m->hull[0] > 0) { m->result[0] = DW_RES_WIN; m->result[1] = DW_RES_LOSS; }
        else if (m->hull[1] > 0) { m->result[1] = DW_RES_WIN; m->result[0] = DW_RES_LOSS; }
        else { m->result[0] = m->result[1] = DW_RES_DRAW; }
    } else if (m->round >= max_rounds()) {
        int w = round_winner_by_hull(m->hull[0], m->hull[1]);
        m->done = 1; m->reason = DW_END_ROUNDS;
        m->result[0] = w == 0 ? DW_RES_WIN : w == 1 ? DW_RES_LOSS : DW_RES_DRAW;
        m->result[1] = w == 1 ? DW_RES_WIN : w == 0 ? DW_RES_LOSS : DW_RES_DRAW;
    }
    if (out) *out = o;
}

void dw_match_forfeit(DwMatch *m, int loser_seat) {
    if (m->done) return;
    m->done = 1; m->reason = DW_END_FORFEIT;
    m->result[loser_seat] = DW_RES_LOSS; m->result[1 - loser_seat] = DW_RES_WIN;
}

void dw_match_view(const DwMatch *m, int seat, DwView *v) {
    int o = 1 - seat, n = 0;
    v->round = m->round; v->hull_you = m->hull[seat]; v->hull_opp = m->hull[o];
    v->energy_you = m->energy[seat]; v->energy_opp = m->energy[o];
    memcpy(v->hand, m->hand[seat], DW_HAND);
    for (int i = 0; i < DW_HAND; i++) if (m->hand[o][i] >= 0) n++;
    v->opp_hand_size = (uint8_t)n;
}
